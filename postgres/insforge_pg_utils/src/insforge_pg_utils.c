#include "postgres.h"

#include <ctype.h>

#include "access/htup_details.h"
#include "access/relation.h"
#include "catalog/namespace.h"
#include "catalog/pg_class.h"
#include "catalog/pg_authid_d.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "nodes/parsenodes.h"
#include "parser/parser.h"
#include "storage/lockdefs.h"
#include "tcop/utility.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/regproc.h"
#include "utils/rel.h"

PG_MODULE_MAGIC;

static ProcessUtility_hook_type prev_ProcessUtility_hook = NULL;

static char *policy_grant_role = NULL;
static char *policy_grant_tables = NULL;
static char *extension_grant_role = NULL;

void _PG_init(void);
void _PG_fini(void);

static void insforge_pg_utils_ProcessUtility(PlannedStmt *pstmt,
                                             const char *queryString,
                                             bool readOnlyTree,
                                             ProcessUtilityContext context,
                                             ParamListInfo params,
                                             QueryEnvironment *queryEnv,
                                             DestReceiver *dest,
                                             QueryCompletion *qc);

static void run_next_ProcessUtility(PlannedStmt *pstmt,
                                    const char *queryString,
                                    bool readOnlyTree,
                                    ProcessUtilityContext context,
                                    ParamListInfo params,
                                    QueryEnvironment *queryEnv,
                                    DestReceiver *dest,
                                    QueryCompletion *qc);

static RangeVar *get_policy_grant_target(Node *utility_stmt);
static RangeVar *get_drop_policy_target(DropStmt *stmt);
static RangeVar *get_rename_policy_target(RenameStmt *stmt);
static bool alter_table_only_updates_rls(AlterTableStmt *stmt);
static bool current_role_matches_policy_grant_role(void);
static bool current_role_matches_extension_grant_role(void);
static bool current_role_matches_configured_role(const char *role_name);
static bool is_extension_utility_statement(Node *utility_stmt);
static bool target_table_is_policy_granted(const RangeVar *target_table,
                                           Oid *target_table_oid);
static void run_as_relation_owner(Oid relid,
                                  PlannedStmt *pstmt,
                                  const char *queryString,
                                  bool readOnlyTree,
                                  ProcessUtilityContext context,
                                  ParamListInfo params,
                                  QueryEnvironment *queryEnv,
                                  DestReceiver *dest,
                                  QueryCompletion *qc);
static void run_as_bootstrap_superuser(PlannedStmt *pstmt,
                                       const char *queryString,
                                       bool readOnlyTree,
                                       ProcessUtilityContext context,
                                       ParamListInfo params,
                                       QueryEnvironment *queryEnv,
                                       DestReceiver *dest,
                                       QueryCompletion *qc);
static char *trim_token(char *token);

void
_PG_init(void)
{
  DefineCustomStringVariable(
      "insforge.policy_grant_role",
      "Role allowed to manage policies on configured managed tables.",
      NULL,
      &policy_grant_role,
      "project_admin",
      PGC_SIGHUP,
      0,
      NULL,
      NULL,
      NULL);

  DefineCustomStringVariable(
      "insforge.policy_grant_tables",
      "Comma-separated managed tables whose policies can be managed by insforge.policy_grant_role.",
      NULL,
      &policy_grant_tables,
      "",
      PGC_SIGHUP,
      0,
      NULL,
      NULL,
      NULL);

  DefineCustomStringVariable(
      "insforge.extension_grant_role",
      "Role allowed to manage installed PostgreSQL extensions without direct superuser privileges.",
      NULL,
      &extension_grant_role,
      "project_admin",
      PGC_SIGHUP,
      0,
      NULL,
      NULL,
      NULL);

  prev_ProcessUtility_hook = ProcessUtility_hook;
  ProcessUtility_hook = insforge_pg_utils_ProcessUtility;
}

void
_PG_fini(void)
{
  ProcessUtility_hook = prev_ProcessUtility_hook;
}

static void
insforge_pg_utils_ProcessUtility(PlannedStmt *pstmt,
                                 const char *queryString,
                                 bool readOnlyTree,
                                 ProcessUtilityContext context,
                                 ParamListInfo params,
                                 QueryEnvironment *queryEnv,
                                 DestReceiver *dest,
                                 QueryCompletion *qc)
{
  Node *utility_stmt = pstmt->utilityStmt;
  RangeVar *target_table = NULL;
  Oid target_table_oid = InvalidOid;

  if (!superuser() && current_role_matches_policy_grant_role())
  {
    target_table = get_policy_grant_target(utility_stmt);
    if (target_table != NULL &&
        target_table_is_policy_granted(target_table, &target_table_oid))
    {
      run_as_relation_owner(target_table_oid, pstmt, queryString, readOnlyTree,
                            context, params, queryEnv, dest, qc);
      return;
    }
  }

  if (!superuser() && current_role_matches_extension_grant_role() &&
      is_extension_utility_statement(utility_stmt))
  {
    run_as_bootstrap_superuser(pstmt, queryString, readOnlyTree, context,
                               params, queryEnv, dest, qc);
    return;
  }

  run_next_ProcessUtility(pstmt, queryString, readOnlyTree, context, params,
                          queryEnv, dest, qc);
}

static void
run_next_ProcessUtility(PlannedStmt *pstmt,
                        const char *queryString,
                        bool readOnlyTree,
                        ProcessUtilityContext context,
                        ParamListInfo params,
                        QueryEnvironment *queryEnv,
                        DestReceiver *dest,
                        QueryCompletion *qc)
{
  if (prev_ProcessUtility_hook)
  {
    prev_ProcessUtility_hook(pstmt, queryString, readOnlyTree, context, params,
                             queryEnv, dest, qc);
  }
  else
  {
    standard_ProcessUtility(pstmt, queryString, readOnlyTree, context, params,
                            queryEnv, dest, qc);
  }
}

static RangeVar *
get_policy_grant_target(Node *utility_stmt)
{
  if (utility_stmt == NULL)
  {
    return NULL;
  }

  switch (nodeTag(utility_stmt))
  {
    case T_CreatePolicyStmt:
      return ((CreatePolicyStmt *) utility_stmt)->table;

    case T_AlterPolicyStmt:
      return ((AlterPolicyStmt *) utility_stmt)->table;

    case T_DropStmt:
      return get_drop_policy_target((DropStmt *) utility_stmt);

    case T_RenameStmt:
      return get_rename_policy_target((RenameStmt *) utility_stmt);

    case T_AlterTableStmt:
      if (alter_table_only_updates_rls((AlterTableStmt *) utility_stmt))
      {
        return ((AlterTableStmt *) utility_stmt)->relation;
      }
      return NULL;

    default:
      return NULL;
  }
}

static RangeVar *
get_rename_policy_target(RenameStmt *stmt)
{
  if (stmt->renameType != OBJECT_POLICY)
  {
    return NULL;
  }

  return stmt->relation;
}

static RangeVar *
get_drop_policy_target(DropStmt *stmt)
{
  List *object_names;
  List *table_names = NIL;
  ListCell *lc;
  int index = 0;
  int table_name_count;

  if (stmt->removeType != OBJECT_POLICY || list_length(stmt->objects) != 1)
  {
    return NULL;
  }

  object_names = (List *) linitial(stmt->objects);
  if (object_names == NIL || list_length(object_names) < 2)
  {
    return NULL;
  }

  table_name_count = list_length(object_names) - 1;
  foreach(lc, object_names)
  {
    if (index < table_name_count)
    {
      table_names = lappend(table_names, lfirst(lc));
    }
    index++;
  }

  return makeRangeVarFromNameList(table_names);
}

static bool
alter_table_only_updates_rls(AlterTableStmt *stmt)
{
  ListCell *lc;

  if (stmt->relation == NULL || stmt->cmds == NIL)
  {
    return false;
  }

  foreach(lc, stmt->cmds)
  {
    AlterTableCmd *cmd = (AlterTableCmd *) lfirst(lc);
    if (cmd->subtype != AT_EnableRowSecurity &&
        cmd->subtype != AT_DisableRowSecurity)
    {
      return false;
    }
  }

  return true;
}

static bool
current_role_matches_policy_grant_role(void)
{
  return current_role_matches_configured_role(policy_grant_role);
}

static bool
current_role_matches_extension_grant_role(void)
{
  return current_role_matches_configured_role(extension_grant_role);
}

static bool
current_role_matches_configured_role(const char *role_name)
{
  const char *current_role_name;

  if (role_name == NULL || role_name[0] == '\0')
  {
    return false;
  }

  current_role_name = GetUserNameFromId(GetUserId(), false);
  return strcmp(current_role_name, role_name) == 0;
}

static bool
is_extension_utility_statement(Node *utility_stmt)
{
  if (utility_stmt == NULL)
  {
    return false;
  }

  switch (nodeTag(utility_stmt))
  {
    case T_CreateExtensionStmt:
    case T_AlterExtensionStmt:
    case T_AlterExtensionContentsStmt:
      return true;

    case T_DropStmt:
      return ((DropStmt *) utility_stmt)->removeType == OBJECT_EXTENSION;

    default:
      return false;
  }
}

static bool
target_table_is_policy_granted(const RangeVar *target_table,
                               Oid *target_table_oid)
{
  char *tables_copy;
  char *table_token;
  char *cursor;

  if (policy_grant_tables == NULL || policy_grant_tables[0] == '\0')
  {
    return false;
  }

  *target_table_oid = RangeVarGetRelid((RangeVar *) target_table,
                                       AccessExclusiveLock,
                                       false);

  tables_copy = pstrdup(policy_grant_tables);
  cursor = tables_copy;

  while ((table_token = strsep(&cursor, ",")) != NULL)
  {
    char *trimmed_table = trim_token(table_token);
    List *qualified_name;
    RangeVar *allowlisted_table;
    Oid allowlisted_table_oid;

    if (trimmed_table[0] == '\0')
    {
      continue;
    }

    qualified_name = stringToQualifiedNameList(trimmed_table);
    if (qualified_name == NIL)
    {
      continue;
    }

    allowlisted_table = makeRangeVarFromNameList(qualified_name);
    allowlisted_table_oid = RangeVarGetRelid(allowlisted_table, NoLock, true);
    if (OidIsValid(allowlisted_table_oid) &&
        allowlisted_table_oid == *target_table_oid)
    {
      pfree(tables_copy);
      return true;
    }
  }

  pfree(tables_copy);
  return false;
}

static void
run_as_relation_owner(Oid relid,
                      PlannedStmt *pstmt,
                      const char *queryString,
                      bool readOnlyTree,
                      ProcessUtilityContext context,
                      ParamListInfo params,
                      QueryEnvironment *queryEnv,
                      DestReceiver *dest,
                      QueryCompletion *qc)
{
  Oid save_userid;
  int save_sec_context;
  Relation relation;
  Oid owner_id;

  relation = relation_open(relid, NoLock);
  owner_id = RelationGetForm(relation)->relowner;
  relation_close(relation, NoLock);

  GetUserIdAndSecContext(&save_userid, &save_sec_context);
  SetUserIdAndSecContext(owner_id,
                         save_sec_context | SECURITY_LOCAL_USERID_CHANGE);

  PG_TRY();
  {
    run_next_ProcessUtility(pstmt, queryString, readOnlyTree, context, params,
                            queryEnv, dest, qc);
  }
  PG_CATCH();
  {
    SetUserIdAndSecContext(save_userid, save_sec_context);
    PG_RE_THROW();
  }
  PG_END_TRY();

  SetUserIdAndSecContext(save_userid, save_sec_context);
}

static void
run_as_bootstrap_superuser(PlannedStmt *pstmt,
                           const char *queryString,
                           bool readOnlyTree,
                           ProcessUtilityContext context,
                           ParamListInfo params,
                           QueryEnvironment *queryEnv,
                           DestReceiver *dest,
                           QueryCompletion *qc)
{
  Oid save_userid;
  int save_sec_context;

  GetUserIdAndSecContext(&save_userid, &save_sec_context);
  SetUserIdAndSecContext(BOOTSTRAP_SUPERUSERID,
                         save_sec_context | SECURITY_LOCAL_USERID_CHANGE);

  PG_TRY();
  {
    run_next_ProcessUtility(pstmt, queryString, readOnlyTree, context, params,
                            queryEnv, dest, qc);
  }
  PG_CATCH();
  {
    SetUserIdAndSecContext(save_userid, save_sec_context);
    PG_RE_THROW();
  }
  PG_END_TRY();

  SetUserIdAndSecContext(save_userid, save_sec_context);
}

static char *
trim_token(char *token)
{
  char *end;

  while (*token != '\0' && isspace((unsigned char) *token))
  {
    token++;
  }

  end = token + strlen(token);
  while (end > token && isspace((unsigned char) *(end - 1)))
  {
    end--;
  }
  *end = '\0';

  return token;
}
