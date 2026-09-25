# DATABASE_RULES.md

**Database-Agnostic Architecture Specification**
*PostgreSQL baseline, MySQL 8.0+ supported*

---

## 0. Purpose and Scope (Clarified)

### 0.1 What “DB-Agnostic” Means *Here*

“Database-agnostic” **does not** mean:

* “Write generic SQL and hope”
* “Avoid using powerful DB features”
* “Lowest common denominator ANSI SQL only”

It **does mean**:

* **Gameplay logic is completely unaware of database dialects**
* **Repository SQL contains no backend-specific syntax**
* **All dialect differences are isolated to the DB driver layer**
* **Adding a new backend is additive, not invasive**

This project is **PostgreSQL-first**, with **explicit compatibility support for MySQL 8.0+**.
Other databases may be added later *only* by implementing the required primitives and passing the full test suite.

---

## 1. Architectural Layering Model (Strengthened)

### 1.1 Definition of a “Layer”

A *layer* is a compilation boundary with **strict dependency direction**.
Dependencies may flow *downward only*.

### 1.2 Layers and Dependency Rules

| Layer                 | Location                                  | May Depend On              | Must NOT Depend On           |
| --------------------- | ----------------------------------------- | -------------------------- | ---------------------------- |
| **Domain / Gameplay** | `src/*.c`, `src/server_*`, `src/engine_*` | repo_* APIs                | SQL, db_api, db_int, drivers |
| **Repository Layer**  | `src/db/repo/*.c`                         | db_api, db_int, sql_driver | Backend drivers, dialect SQL |
| **DB API & Dialect**  | `src/db/db_api.*`, `src/db/sql_driver.*`  | Backend drivers            | Gameplay logic               |
| **Backend Drivers**   | `src/db/pg/*`, `src/db/mysql/*`           | DB SDK                     | Anything above               |

**Violation of layer boundaries is a hard architectural failure.**

---

## 2. Additive Backend Definition (Enforceable)

### 2.1 What “Additive” Means

Adding a database backend **must not**:

* Change gameplay logic
* Add `if (db == X)` branches outside DB code
* Modify repository SQL

### 2.2 Required Components for a New Backend

A backend is considered supported **only if all are present**:

1. **Driver implementation**

   * `src/db/<backend>/db_<backend>.c`
2. **Dialect support**

   * Placeholder conversion in `sql_build`
   * All required primitives implemented
3. **Schema**

   * DDL producing equivalent tables, indexes, constraints
4. **Migration scripts**

   * Deterministic and repeatable
5. **Test execution**

   * Full regression suite passes unchanged
6. **Compatibility declaration**

   * Documented deviations (if any)

If any of these require touching gameplay code, the backend is rejected.

---

## 3. SQL Feature Policy (New)

### 3.1 Allowed SQL Feature Set

Repository SQL is limited to a **portable core**:

* ANSI SELECT / INSERT / UPDATE / DELETE
* WHERE, JOIN, GROUP BY, ORDER BY
* LIMIT (OFFSET discouraged; see §3.3)
* Basic aggregates

### 3.2 Forbidden SQL Features in Repositories

Explicitly forbidden in `src/db/repo/**`:

* Dialect-specific clauses (`RETURNING`, `ON CONFLICT`, `ON DUPLICATE KEY`)
* Dialect operators (`ILIKE`, `::type`)
* Backend functions (`NOW`, `to_timestamp`, `FROM_UNIXTIME`, `EXTRACT`)
* Locking syntax (`FOR UPDATE`, `SKIP LOCKED`)
* JSON operators/functions

If it differs across DBs, it **does not belong in repo SQL**.

### 3.3 Paging Policy

* `LIMIT` is allowed
* `OFFSET` is allowed but discouraged for deep paging
* Prefer **keyset pagination** for large datasets
* If OFFSET semantics differ across DBs in future, paging becomes a driver concern

---

## 4. Dialect Escape Hatch (Explicitly Defined)

### 4.1 When Backend-Specific SQL Is Allowed

Backend-specific SQL is allowed **only** when:

* It is required for correctness or performance
* There is no portable equivalent
* It is isolated to the driver layer

### 4.2 Where It Must Live

* `src/db/pg/*`
* `src/db/mysql/*`

Never in:

* Repository code
* Gameplay code

### 4.3 Fallback Requirements

If backend-specific behavior exists:

* A portable fallback must be documented
* Tests must verify equivalent behavior

---

## 5. Migration Policy (New)

### 5.1 Migration Requirements

* Migrations must be **backend-specific but semantically equivalent**
* Schema outcome must match across DBs:

  * Tables
  * Indexes
  * Constraints
  * Defaults
* No migration may assume implicit DB behavior

### 5.2 Verification

* Schema diffs must be validated via tooling or tests
* Migrations must be repeatable on a clean DB

---

## 6. Testing Requirements (New)

### 6.1 Mandatory Test Coverage

* All supported backends must run:

  * Full regression suite
  * Economy / banking integrity tests
  * Idempotency tests

### 6.2 Test Restrictions

Tests must not:

* Assume backend-specific ordering
* Depend on implicit collation behavior
* Rely on dialect-specific error codes

Backend-specific tests must be clearly labelled and isolated.

---

## 7. Ambiguous Comparison Policy (Expanded)

### 7.1 Case Sensitivity Rules

* All string comparisons are **assumed case-sensitive**
* Case-insensitivity must be provided via:

  * Schema collation (preferred)
  * DB API helper (never raw SQL functions)

Forbidden in repo SQL:

* `ILIKE`
* `LOWER(col)`
* `UPPER(col)`

### 7.2 Padding / Whitespace

* No SQL must rely on implicit trimming or padding
* Input normalization happens in C, not SQL

---

## 8. Boolean Columns and Queries (Mandatory)

### 8.1 Boolean Representation

* All boolean columns must be defined as **native booleans** in the schema.
  * PostgreSQL: `BOOLEAN`
  * MySQL: `BOOLEAN` (or `TINYINT(1)` treated as boolean)
  * MSSQL: `BIT`

### 8.2 Query Rules

**All boolean comparisons must be explicit.**

* **Allowed:**
  * `WHERE active = TRUE`
  * `WHERE is_admin = FALSE`

* **Forbidden:**
  * `WHERE active` (Implicit truthiness)
  * `WHERE active = 1` (Numeric comparison)
  * `WHERE active != 0`

The DB layer **must not** rewrite boolean expressions. Intent must be explicit in the repository SQL.

---

## 9. Dynamic SQL & Injection Safety (Reinforced)

* No manual SQL concatenation
* No `sprintf`/`snprintf` SQL assembly
* Dynamic predicates must use a structured SQL builder
* Violations are automatic rejection

---

## 10. Final Enforcement Rules

* All rules are enforced by CI gates
* Violations block merges
* “It works on Postgres” is not an excuse

---

## 11. AI / Human Cheat Sheet (Final)

> **If you remember nothing else:**
>
> 1. **No dialect SQL in repo files**
> 2. **Time comes from C, not SQL**
   * **Columns may use native time types** (e.g., Postgres `timestamptz`). The *value* is produced in C and bound via driver conversions; repo SQL must not call backend time functions or casts.
> 3. **Upserts, IDs, locking go through DB primitives**
> 4. **Booleans are `TRUE`/`FALSE`, never `1`/`0` or implicit**
>
> If you feel clever, stop — you are probably breaking portability.
