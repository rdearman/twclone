#!/bin/bash
# Find functions present in TMP_SRC but missing in src
# Usage: ./check_missing.sh

echo "Scanning for missing functions..."
echo "--------------------------------"

# specific files to check based on your linker errors
FILES="server_bank.c server_players.c server_corporation.c database.c database_cmd.c"

for file in $FILES; do
    if [ -f "@TMP_SRC/$file" ] && [ -f "src/$file" ]; then
        echo "Checking $file..."
        # Extract function names (rough C regex) from backup
        grep -E "^[a-z_]+ [a-z0-9_]+\s*\(" "@TMP_SRC/$file" | cut -d'(' -f1 | awk '{print $NF}' | while read func; do
            # Check if this function exists in the new file
            if ! grep -q "$func" "src/$file"; then
                echo "  [MISSING] $func"
            fi
        done
    fi
done
