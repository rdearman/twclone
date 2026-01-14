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

echo "---------------------------------"
if [ $VIOLATIONS -eq 0 ]; then
    echo "SUMMARY: ALL GATES PASSED"
    exit 0
else
    echo "SUMMARY: $VIOLATIONS GATES FAILED"
    exit 1
fi
