# insforge_pg_utils

PostgreSQL shared-preload library for InsForge database utility permission
hooks.

The utility hook currently delegates two statement classes. Policy statements
let `insforge.policy_grant_role` manage row-level-security policies on the
comma-separated `insforge.policy_grant_tables` allowlist without making that
role the table owner.

Extension statements let `insforge.extension_grant_role` manage PostgreSQL
extensions without granting that role direct superuser or database-level
`CREATE` privileges. Extension utility statements are executed as the bootstrap
superuser, then the session role is restored.

This library must be installed into the Postgres image and listed in
`shared_preload_libraries`. It does not currently need SQL-level
`CREATE EXTENSION` installation because it exposes no SQL objects.
