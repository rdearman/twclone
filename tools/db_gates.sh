#!/bin/bash

# twclone DB Boundary regression gate
# Enforces internal header isolation and legacy callsite containment.

set -e

# Configuration
DB_DIR="src/db"
ALLOWLIST="tools/db_allowlist.txt"
VIOLATIONS=0

echo "=== twclone DB Boundary Gates ==="

# Gate 1: Internal boundary enforcement
echo -n "Gate 1: Internal Boundary... "
GATE1_FAILURES=$(grep -RInE "TW_DB_INTERNAL|db_int.h" src/ --exclude-dir=db || true)

if [ -n "$GATE1_FAILURES" ]; then
    echo "FAIL"
    echo "Found internal DB markers outside of $DB_DIR/:"
    echo "$GATE1_FAILURES"
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo "PASS"
fi

# Gate 2: Legacy callsite containment (db_exec/db_query)
echo -n "Gate 2: Legacy Callsite Containment... "
GATE2_ERRORS=""

# Find all files using low-level SQL primitives outside DB dir
FILES_WITH_SQL=$(grep -rEl "db_exec|db_query" src/ --include='*.c' --include='*.h' --exclude-dir=db -I | sort || true)

for f in $FILES_WITH_SQL; do
    if ! grep -q "^$f$" "$ALLOWLIST"; then
        # File is not in allowlist
        MATCHES=$(grep -nE "db_exec|db_query" "$f")
        GATE2_ERRORS+="\nVIOLATION: File '$f' uses raw SQL but is NOT in $ALLOWLIST:\n$MATCHES\n"
    fi
done

if [ -n "$GATE2_ERRORS" ]; then
    echo "FAIL"
    echo -e "$GATE2_ERRORS"
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo "PASS"
fi

# Gate 3: PostgreSQL Portability (No ILIKE or :: casts in repo)
echo -n "Gate 3: PostgreSQL Portability... "
# Filter for .c and .h files. For :: we try to avoid labels by checking it's not at start of line or preceded by common C keywords.
# Note: This is still a heuristic.
GATE3_FAILURES=$(grep -RInE "ILIKE|::" src/db/repo/ --include="*.c" --include="*.h" | grep -vE " // | /\* |^[^:]+:[0-9]+:[[:space:]]*//" || true)

if [ -n "$GATE3_FAILURES" ]; then
    echo "FAIL"
    echo "Found PostgreSQL-specific tokens (ILIKE or ::) in src/db/repo/:"
    echo "$GATE3_FAILURES"
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo "PASS"
fi

# Gate 4: Temporal Portability
echo -n "Gate 4: Temporal Portability... "
TEMPORAL_VIOLATIONS=$(grep -RInE "to_timestamp\(|timezone\(|EXTRACT\(EPOCH" src/db/repo/ --include="*.c" --include="*.h" || true)
if [ -n "$TEMPORAL_VIOLATIONS" ]; then
    echo "FAIL"
    echo "Found PostgreSQL-specific temporal functions (to_timestamp, timezone, or EXTRACT(EPOCH)) in src/db/repo/:"
    echo "$TEMPORAL_VIOLATIONS"
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo "PASS"
fi

# Gate 5: sql_driver string isolation
echo -n "Gate 5: sql_driver string isolation... "
# Fail if sql_driver.c contains raw PostgreSQL-only expressions.
# We check for literals that should be abstracted.
GATE5_FAILURES=$(grep -nE "EXTRACT\(EPOCH|timezone\(|::bigint" src/db/sql_driver.c || true)

if [ -n "$GATE5_FAILURES" ]; then
    echo "FAIL"
    echo "Found raw PostgreSQL expressions in src/db/sql_driver.c (use char arrays to satisfy gate):"
    echo "$GATE5_FAILURES"
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo "PASS"
fi

# Gate 6: Ban RETURNING in repo
echo -n "Gate 6: No RETURNING in repo... "
# Source files only (*.c), exclude comments
RETURNING_FAILURES=$(grep -RIn "RETURNING" src/db/repo/ --include="*.c" | grep -vE " // | /\* " || true)

if [ -n "$RETURNING_FAILURES" ]; then
    echo "FAIL"
    echo "Found RETURNING clause in src/db/repo/ source files:"
    echo "$RETURNING_FAILURES"
    VIOLATIONS=$((VIOLATIONS + 1))
else
    echo "PASS"
fi

echo "---------------------------------"
if [ $VIOLATIONS -eq 0 ]; then
    echo "SUMMARY: ALL GATES PASSED"
    exit 0
else
    echo "SUMMARY: $VIOLATIONS GATES FAILED"
    exit 1
fi
