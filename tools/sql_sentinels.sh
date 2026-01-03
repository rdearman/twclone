#!/bin/bash
#
# sql_sentinels.sh - Detect SQLite-specific SQL patterns in PostgreSQL codebase
#
# Prevents accidental reintroduction of SQLite syntax via git restore or other mistakes.
# Exit 0 if all checks pass, non-zero if violations found.
#
# Forbidden patterns:
#   1. strftime('%s','now') - SQLite time function
#   2. INSERT OR IGNORE - SQLite INSERT variant
#   3. sqlite3_ prefix - SQLite C API
#   4. to_timestamp( - PostgreSQL function (allowed only in src/db/sql_driver.* or src/db/postgres/*)
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$REPO_ROOT/src"

VIOLATIONS=0

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "Running SQL sentinel checks on: $SRC_DIR"
echo ""

# =========================================================================
# CHECK 0: Hard gate - NO active SQLite in application code
# =========================================================================
# Strict gate against ACTIVE SQLite code in application (server_*.c, engine_*.c).
# Ignores comments, old error code references (SQLITE_OK is legacy, not new code).
#
# Fails if found:
#   - db_backend() checks for DB_BACKEND_SQLITE (active SQLite support)
#   - New sqlite3_* API calls (active SQLite usage)
#   - strftime('%s','now') in application code (SQLite time function)
#   - INSERT OR IGNORE in application code (SQLite syntax)
#
# Allowed (not failures):
#   - Comments mentioning "SQLite" (historical)
#   - SQLITE_OK error code constants (legacy, being phased out)
#   - Comments explaining removed SQLite code
#
# Rationale: No new SQLite feature development in business logic.
# =========================================================================
echo -n "CHECK 0: Hard gate - no active SQLite in server code... "

# Check for active SQLite backend conditional logic
ACTIVE_SQLITE=$(grep -rin "DB_BACKEND_SQLITE" "$SRC_DIR"/server_*.c "$SRC_DIR"/engine_*.c 2>/dev/null | \
    grep -v "//" | wc -l)

# Check for new sqlite3_* API calls (not in comments)
SQLITE3_API=$(grep -rin "sqlite3_" "$SRC_DIR"/server_*.c "$SRC_DIR"/engine_*.c 2>/dev/null | \
    grep -v "^[[:space:]]*/*\|^[[:space:]]*//" | wc -l)

# Check for active strftime('%s','now') in server code (SQLite SQL, not C library)
ACTIVE_STRFTIME=$(grep -rin "strftime\s*(\s*['\"]%s['\"]\s*,\s*['\"]now['\"]\s*)" "$SRC_DIR"/server_*.c "$SRC_DIR"/engine_*.c 2>/dev/null | wc -l)

# Check for INSERT OR IGNORE in server code
ACTIVE_INSERT_OR=$(grep -rin "INSERT.*OR.*IGNORE" "$SRC_DIR"/server_*.c "$SRC_DIR"/engine_*.c 2>/dev/null | wc -l)

TOTAL_ACTIVE=$((ACTIVE_SQLITE + SQLITE3_API + ACTIVE_STRFTIME + ACTIVE_INSERT_OR))

if [ "$TOTAL_ACTIVE" -gt 0 ]; then
    echo -e "${RED}FAIL${NC}"
    echo ""
    echo "HARD GATE VIOLATION: Active SQLite found in server code"
    echo ""
    if [ "$ACTIVE_SQLITE" -gt 0 ]; then
        echo "DB_BACKEND_SQLITE checks:"
        grep -rin "DB_BACKEND_SQLITE" "$SRC_DIR"/server_*.c "$SRC_DIR"/engine_*.c 2>/dev/null | grep -v "//"
    fi
    if [ "$SQLITE3_API" -gt 0 ]; then
        echo "sqlite3_* API calls:"
        grep -rin "sqlite3_" "$SRC_DIR"/server_*.c "$SRC_DIR"/engine_*.c 2>/dev/null | grep -v "^[[:space:]]*/*\|^[[:space:]]*//"
    fi
    if [ "$ACTIVE_STRFTIME" -gt 0 ]; then
        echo "Active strftime calls:"
        grep -rin "strftime\s*(\s*['\"]%s['\"]\s*,\s*['\"]now['\"]\s*)" "$SRC_DIR"/server_*.c "$SRC_DIR"/engine_*.c 2>/dev/null
    fi
    if [ "$ACTIVE_INSERT_OR" -gt 0 ]; then
        echo "Active INSERT OR IGNORE:"
        grep -rin "INSERT.*OR.*IGNORE" "$SRC_DIR"/server_*.c "$SRC_DIR"/engine_*.c 2>/dev/null
    fi
    echo ""
    VIOLATIONS=$((VIOLATIONS + TOTAL_ACTIVE))
    exit 1  # Exit immediately - hard gate failure
else
    echo -e "${GREEN}PASS${NC}"
fi

# =========================================================================
# CHECK 1: strftime('%s','now') - SQLite time function
# =========================================================================
echo -n "CHECK 1: Scanning for strftime('%s','now')... "
STRFTIME_VIOLATIONS=$(grep -rnE "strftime[[:space:]]*\([[:space:]]*['\"]%s['\"][[:space:]]*,[[:space:]]*['\"]now['\"]" \
    "$SRC_DIR" --include="*.c" --include="*.h" 2>/dev/null | wc -l)

if [ "$STRFTIME_VIOLATIONS" -gt 0 ]; then
    echo -e "${RED}FAIL${NC}"
    grep -rnE "strftime[[:space:]]*\([[:space:]]*['\"]%s['\"][[:space:]]*,[[:space:]]*['\"]now['\"]" \
        "$SRC_DIR" --include="*.c" --include="*.h" 2>/dev/null
    VIOLATIONS=$((VIOLATIONS + STRFTIME_VIOLATIONS))
else
    echo -e "${GREEN}PASS${NC}"
fi

# =========================================================================
# CHECK 2: INSERT OR IGNORE - SQLite INSERT variant
# =========================================================================
echo -n "CHECK 2: Scanning for INSERT OR IGNORE... "
INSERT_OR_VIOLATIONS=$(grep -rnEi "INSERT[[:space:]]+OR[[:space:]]+IGNORE" \
    "$SRC_DIR" --include="*.c" --include="*.h" 2>/dev/null | wc -l)

if [ "$INSERT_OR_VIOLATIONS" -gt 0 ]; then
    echo -e "${RED}FAIL${NC}"
    grep -rnEi "INSERT[[:space:]]+OR[[:space:]]+IGNORE" \
        "$SRC_DIR" --include="*.c" --include="*.h" 2>/dev/null
    VIOLATIONS=$((VIOLATIONS + INSERT_OR_VIOLATIONS))
else
    echo -e "${GREEN}PASS${NC}"
fi

# =========================================================================
# CHECK 3: sqlite3_ prefix - SQLite C API (outside sqlite backend)
# =========================================================================
echo -n "CHECK 3: Scanning for sqlite3_ calls outside sqlite backend... "

SQLITE3_VIOLATIONS=$(grep -rn "sqlite3_" "$SRC_DIR" --include="*.c" --include="*.h" 2>/dev/null | \
    grep -v "src/db/sqlite" | wc -l)

if [ "$SQLITE3_VIOLATIONS" -gt 0 ]; then
    echo -e "${RED}FAIL${NC}"
    grep -rn "sqlite3_" "$SRC_DIR" --include="*.c" --include="*.h" 2>/dev/null | \
        grep -v "src/db/sqlite"
    VIOLATIONS=$((VIOLATIONS + SQLITE3_VIOLATIONS))
else
    echo -e "${GREEN}PASS${NC}"
fi

# =========================================================================
# CHECK 4: to_timestamp( - PostgreSQL function (outside driver layer)
# =========================================================================
# Allowed only in:
#   - src/db/sql_driver.c (future SQL driver layer)
#   - src/db/postgres/* (PostgreSQL-specific backend files)

echo -n "CHECK 4: Scanning for to_timestamp() outside allowed locations... "

TO_TIMESTAMP_VIOLATIONS=$(grep -rn "to_timestamp\s*(" "$SRC_DIR" --include="*.c" --include="*.h" 2>/dev/null | \
    grep -v "src/db/sql_driver" | \
    grep -v "src/db/postgres" | \
    grep -v "^\s*//" | \
    wc -l)

if [ "$TO_TIMESTAMP_VIOLATIONS" -gt 0 ]; then
    echo -e "${RED}FAIL${NC}"
    grep -rn "to_timestamp\s*(" "$SRC_DIR" --include="*.c" --include="*.h" 2>/dev/null | \
        grep -v "src/db/sql_driver" | \
        grep -v "src/db/postgres" | \
        grep -v "^\s*//"
    VIOLATIONS=$((VIOLATIONS + TO_TIMESTAMP_VIOLATIONS))
else
    echo -e "${GREEN}PASS${NC}"
fi

# =========================================================================
# Summary
# =========================================================================
echo ""
echo "======================================================================"
if [ $VIOLATIONS -eq 0 ]; then
    echo -e "${GREEN}✓ All sentinel checks passed!${NC}"
    echo "======================================================================"
    exit 0
else
    echo -e "${RED}✗ VIOLATION: Found $VIOLATIONS SQL compatibility issues${NC}"
    echo "======================================================================"
    echo ""
    echo "To fix:"
    echo "  1. strftime('%s','now') → NOW() or EXTRACT(EPOCH FROM NOW())::bigint"
    echo "  2. INSERT OR IGNORE → ON CONFLICT DO NOTHING"
    echo "  3. sqlite3_* → Use PostgreSQL or database abstraction layer"
    echo "  4. to_timestamp() → Move to src/db/sql_driver.c or use NOW()"
    echo ""
    exit 1
fi
