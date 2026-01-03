# SQL Sentinel Guardrails

Prevents accidental reintroduction of SQLite-specific SQL patterns into the PostgreSQL codebase.

## Usage

```bash
bash tools/sql_sentinels.sh
```

## Exit Codes

- **0**: All checks pass ✓
- **1**: SQL compatibility violations found ✗
  - CHECK 0 failure: immediate exit (hard gate)
  - CHECK 1-4 failures: continue, report all violations

## Checks Performed

### CHECK 0: Hard Gate (PostgreSQL-only enforcement)

**Fails immediately** if any active SQLite code detected in application code (server_*.c, engine_*.c):
- `DB_BACKEND_SQLITE` conditionals (active SQLite support)
- `sqlite3_*` API calls (active SQLite usage)
- `strftime('%s','now')` in SQL (SQLite time function)
- `INSERT OR IGNORE` in SQL (SQLite syntax)

Allowed in modified application code: 0 (zero tolerance)

**Rationale**: Application code is PostgreSQL-only. No new SQLite features.

### CHECK 1: strftime('%s','now') detection

Detects SQLite time function `strftime('%s','now')` throughout src/

**Pattern**: `strftime(pattern, 'now')` where pattern is `'%s'`

### CHECK 2: INSERT OR IGNORE detection

Detects SQLite INSERT variant `INSERT OR IGNORE` throughout src/

**Pattern**: Case-insensitive match for `INSERT ... OR ... IGNORE`

### CHECK 3: sqlite3_* C API detection

Detects raw SQLite C API calls outside backend implementation

**Excluded paths**: `src/db/sqlite/*` (backend implementation, allowed)

### CHECK 4: to_timestamp() detection

Detects PostgreSQL `to_timestamp()` function outside driver layer

**Allowed paths**: 
- `src/db/sql_driver.*` (driver implementation)
- `src/db/postgres/*` (PostgreSQL backend)

## What it detects

1. **strftime('%s','now')** - SQLite time function
2. **INSERT OR IGNORE** - SQLite INSERT variant
3. **sqlite3_** - SQLite C API calls
4. **to_timestamp()** - PostgreSQL function (wrong location)
5. **Active SQLite in app code** - Hard gate (CHECK 0)

## Integration

Run before committing to catch issues early:

```bash
bash tools/sql_sentinels.sh || exit 1
git add .
git commit
```

Or add to CI/CD pipeline as a linting step.

## Why this matters

The codebase was partially migrated from SQLite to PostgreSQL. SQLite patterns periodically resurface via:
- `git restore` accidents
- Copy-paste from old examples
- Incomplete refactoring

This script provides a safety net to prevent production failures due to SQL incompatibilities.

The **hard gate (CHECK 0)** specifically prevents new SQLite features from being introduced into active application code, enforcing a PostgreSQL-only policy for the server logic.
