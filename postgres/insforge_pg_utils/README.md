# insforge_pg_utils

PostgreSQL shared-preload library for InsForge database permission hooks.

The first hook lets `insforge.policy_grant_role` manage row-level-security
policies on the comma-separated `insforge.policy_grant_tables` allowlist without
making that role the table owner.

This library must be installed into the Postgres image and listed in
`shared_preload_libraries`. It does not currently need SQL-level
`CREATE EXTENSION` installation because it exposes no SQL objects.
