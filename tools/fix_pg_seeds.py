import re

def split_statements(text):
    statements = []
    current = []
    in_string = False
    for char in text:
        if char == "'": in_string = not in_string
        if char == ';' and not in_string:
            statements.append("".join(current).strip())
            current = []
        else:
            current.append(char)
    if current:
        statements.append("".join(current).strip())
    return [s for s in statements if s]

def fix_seeds(filename):
    with open(filename, 'r') as f:
        content = f.read()

    bool_cols = {
        'active', 'can_buy_iss', 'can_long_range_scan', 'can_planet_scan',
        'can_purchase', 'can_rob_ports', 'can_transwarp', 'deleted',
        'enabled', 'ephemeral', 'genesis_flag', 'invisible', 'is_default',
        'is_evil', 'is_frozen', 'is_good', 'is_npc', 'is_percentage',
        'is_primary', 'locked', 'resolved', 'archived', 'loggedin'
    }

    statements = split_statements(content)
    new_statements = []

    for stmt in statements:
        if 'INSERT INTO' not in stmt.upper():
            new_statements.append(stmt)
            continue
        
        match = re.search(r'INSERT INTO (\w+)\s*\((.*?)\)', stmt, re.I | re.S)
        if not match:
            new_statements.append(stmt)
            continue
            
        cols = [c.strip().lower() for c in match.group(2).split(',')]
        bool_indices = [i for i, col in enumerate(cols) if col in bool_cols]
        
        parts_val = re.split(r'\bVALUES\b', stmt, flags=re.I)
        if len(parts_val) < 2:
            new_statements.append(stmt)
            continue
            
        prefix = parts_val[0] + " VALUES "
        suffix = parts_val[1]
        
        parts_conflict = re.split(r'\bON CONFLICT\b', suffix, flags=re.I)
        values_part = parts_conflict[0]
        conflict_part = ("\nON CONFLICT " + parts_conflict[1]) if len(parts_conflict) > 1 else ""
        
        # Extract blocks (val1, ...)
        blocks = []
        depth = 0
        current_block = []
        in_string = False
        for char in values_part:
            if char == "'": in_string = not in_string
            if not in_string:
                if char == '(': 
                    depth += 1
                    if depth == 1: continue
                elif char == ')':
                    depth -= 1
                    if depth == 0:
                        blocks.append("".join(current_block))
                        current_block = []
                        continue
            if depth > 0:
                current_block.append(char)

        new_blocks = []
        for block in blocks:
            # Split values in block
            val_parts = []
            curr_val = []
            b_depth = 0
            b_in_string = False
            for c in block:
                if c == "'": b_in_string = not b_in_string
                if not b_in_string:
                    if c == '(': b_depth += 1
                    elif c == ')': b_depth -= 1
                if c == ',' and b_depth == 0 and not b_in_string:
                    val_parts.append("".join(curr_val).strip())
                    curr_val = []
                else:
                    curr_val.append(c)
            val_parts.append("".join(curr_val).strip())
            
            if len(val_parts) == len(cols):
                for idx in bool_indices:
                    v = val_parts[idx].upper()
                    if v == '1' or v == 'TRUE': val_parts[idx] = 'TRUE'
                    elif v == '0' or v == 'FALSE': val_parts[idx] = 'FALSE'
                new_blocks.append("(" + ", ".join(val_parts) + ")")
            else:
                new_blocks.append("(" + block + ")")
        
        new_stmt = prefix + " " + ",\n  ".join(new_blocks) + conflict_part
        new_statements.append(new_stmt)

    with open(filename, 'w') as f:
        f.write(";\n\n".join(new_statements) + ";")

fix_seeds('sql/pg/030_seeds.sql')