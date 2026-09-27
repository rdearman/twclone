# MySQL backend completion assessment

## Current status

`src/db/mysql/db_mysql.c` is a compile/link skeleton, not a working driver. It
does not include a MySQL client library, open a connection, or execute SQL. Its
open function allocates an empty implementation object, sets the MySQL vtable,
and sets `ERR_NOT_IMPLEMENTED` in the error structure while still returning
that non-null implementation. Since `db_open()` treats a non-null implementation
as success, callers can receive a handle alongside an error; transaction and
gameplay calls then fail with `ERR_NOT_IMPLEMENTED`.

The vtable has placeholders for every operation. Only memory allocation/free
and error-message initialization are implemented. `mysql_close_child()` does
not close a socket because there is no connection. `mysql_res_cancel()` and
`mysql_res_finalize()` are no-ops, and the result accessors return empty values
or `ERR_NOT_IMPLEMENTED`.

## API coverage compared with PostgreSQL

| DB API area | MySQL driver | PostgreSQL driver |
| --- | --- | --- |
| Open/close | No connection attempt; returns an empty handle while setting an error | Uses libpq connect/finish |
| Transactions | Begin, commit, rollback are stubs | Executes BEGIN, COMMIT, ROLLBACK |
| Bound execution | `exec`, affected rows, generated ID are stubs | `PQexecParams`; affected row and insert-ID support |
| Queries/results | Query, stepping, metadata, typed access, blobs are stubs | Materializes `PGresult`; most text/numeric access works |
| Returning rows from writes | Stub | Native `RETURNING` via query path |
| Errors | Generic not-implemented message | Maps PostgreSQL SQLSTATE to DB categories |
| Child close | Frees an empty struct | Closes inherited socket then releases libpq state |
| Domain operation | `ship_repair_atomic` stub | Atomic CTE implementation |

The PostgreSQL backend is the only functioning backend, but it does not fully
cover every declared API accessor: its vtable leaves unsigned integer and blob
accessors unset, and numeric reads use permissive conversions. It should be
treated as the behavioral reference for supported game call sites, not as a
complete conformance implementation. Do not change PostgreSQL behavior while
adding tests or implementing MySQL.

## API and dialect issues to resolve before implementation

1. **Connection configuration:** `db_config_t` exposes `pg_conninfo`; `db_open`
   reuses it for MySQL. Add a backend-neutral connection string or explicit
   MySQL settings while keeping PostgreSQL callers unchanged.
2. **Placeholder identity:** `db_api.c` renders `{N}` through `sql_build()`.
   PostgreSQL receives `$N`; MySQL receives `?`. MySQL's positional prepared
   statements cannot preserve repeated or reordered `{N}` references after
   this rendering loses the parameter indexes. Preserve indexes through
   binding or expand a validated bind vector before enabling MySQL.
3. **Write-returning semantics:** PostgreSQL relies on native `RETURNING`.
   MySQL needs defined behavior for `db_exec_returning` and call sites that
   request returned rows. Do not emulate it with unsafe SQL string rewriting;
   define supported statement forms and handle generated IDs explicitly.
4. **Dialect helper coverage:** `sql_driver.c` has MySQL fragments for some
   operations, while conflict handling, JSON helpers, and other fragments
   remain PostgreSQL-only or commented out. Audit actual call sites and either
   implement equivalent MySQL fragments or reject unsupported paths early.
5. **Operation-level hook:** the internal vtable requires
   `ship_repair_atomic`, although the public operations vtable says optional
   helpers should be nullable with generic SQL fallback. Resolve that mismatch
   before treating the MySQL driver as complete.

## Completion plan

1. Agree on API contracts for configuration, placeholder mapping, write-return
   operations, and optional domain hooks. Keep the current `{N}` application
   convention stable for PostgreSQL; add MySQL-specific rendering/bind tests.
2. Add the MySQL client dependency to the build and CI matrix. Implement
   connection lifecycle, connection-loss detection, child-close behavior, and
   SQLSTATE/native error classification.
3. Implement prepared statements for every bind type, including null, signed
   and unsigned numbers, boolean, text/JSON, blob, and both timestamp forms.
   Validate parameter count/index mapping before sending SQL.
4. Implement transaction begin/commit/rollback, execution, affected-row
   reporting, generated IDs, and the agreed write-returning behavior. Ensure
   failure leaves transaction state and result handles well-defined.
5. Implement result metadata, iteration, NULL checks, checked numeric
   conversions, text and blob lifetime, cancellation, and finalization to the
   `db_api.h` contract.
6. Implement or remove the mandatory MySQL `ship_repair_atomic` hook only after
   confirming the generic fallback contract and transaction semantics.
7. Gate support on backend-neutral API tests against isolated PostgreSQL and
   MySQL instances, plus application repository smoke tests, migrations, and
   seed replay. Tests should cover error classes, transaction rollback,
   placeholder reuse/order, large unsigned values, NULLs, blobs, generated
   IDs, result lifetime, and fork/child close.

## v2 and v3 readiness

MySQL schema/migration portability does not make MySQL a v2-supported runtime.
The runtime backend and DB build integration must be completed and tested first.
The final recent MySQL schema edits also still need an isolated server run; the
previous local disposable instance passed the earlier migration chain, but a
later restart attempt failed on InnoDB file locks.

For v3, add a migration runner that records applied versions and checksums,
orders backend-specific migrations, supports dry-run/preflight and explicit
fresh-install versus upgrade paths, and stops safely on DDL failure. Since
MySQL DDL is not transactionally reversible, pair it with backup/restore
guidance and resumable step tracking. Add a shared backend conformance suite
that runs the same DB API cases against every supported backend, plus CI jobs
using disposable database instances. Keep schema ownership in migrations and
database state as the source of truth.
