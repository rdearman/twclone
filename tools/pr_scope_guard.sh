#!/bin/bash
# tools/pr_scope_guard.sh
# Usage: ./tools/pr_scope_guard.sh [allowed_file1] [allowed_file2] ...
# Verifies that ONLY the files listed (and potentially this script itself) have been modified relative to HEAD.

if [ "$#" -eq 0 ]; then
    echo "Usage: $0 file1 [file2 ...]"
    exit 1
fi

ALLOWED_FILES="$@"
# Always allow this script to be modified (in case we are adding it in the same PR)
ALLOWED_FILES="$ALLOWED_FILES tools/pr_scope_guard.sh"

# Get list of changed files (staged and unstaged)
CHANGED_FILES=$(git diff --name-only HEAD)

VIOLATIONS=0

for file in $CHANGED_FILES; do
    # Check if file is in ALLOWED_FILES
    IS_ALLOWED=0
    for allowed in $ALLOWED_FILES; do
        if [ "$file" == "$allowed" ]; then
            IS_ALLOWED=1
            break
        fi
    done

    if [ "$IS_ALLOWED" -eq 0 ]; then
        echo "SCOPE VIOLATION: Modified file '$file' is not in the allowed list."
        VIOLATIONS=$((VIOLATIONS + 1))
    fi
done

if [ "$VIOLATIONS" -gt 0 ]; then
    echo "FAILED: $VIOLATIONS scope violations found."
    exit 1
else
    echo "PASSED: All modified files are within scope."
    exit 0
fi