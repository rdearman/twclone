import re

def split_statements(text):
    text = re.sub(r'\s+', ' ', text).strip()
    stmts = []
    curr = []
    depth = 0
    in_str = False
    for char in text:
        if char == "'": in_str = not in_str
        if not in_str:
            if char == '(': depth += 1
            elif char == ')': depth -= 1
        curr.append(char)
        if char == ';' and depth == 0 and not in_str:
            stmts.append("".join(curr).strip())
            curr = []
    if curr: stmts.append("".join(curr).strip())
    return stmts

def split_columns(body):
    parts = []
    curr = []
    depth = 0
    in_str = False
    for char in body:
        if char == "'": in_str = not in_str
        if not in_str:
            if char == '(': depth += 1
            elif char == ')': depth -= 1
        if char == ',' and depth == 0 and not in_str:
            parts.append("".join(curr).strip())
            curr = []
        else:
            curr.append(char)
    if curr: parts.append("".join(curr).strip())
    return parts

def convert_stmt(s):
    if not s: return None
    if s.upper().startswith('CREATE TRIGGER'): return None
    
    m_table = re.search(r'CREATE TABLE (\w+)', s, re.I)
    table_name = m_table.group(1).lower() if m_table else ""
    if table_name == 'sqlite_sequence': return None

    # Common replacements for THIS statement
    s = s.replace('INTEGER PRIMARY KEY AUTOINCREMENT', 'BIGSERIAL PRIMARY KEY')
    s = s.replace('INTEGER PRIMARY KEY', 'BIGSERIAL PRIMARY KEY')
    s = re.sub(r'\bAUTOINCREMENT\b', '', s, flags=re.I)
    s = s.replace('DATETIME', 'TIMESTAMP').replace('REAL', 'DOUBLE PRECISION').replace('COLLATE NOCASE', '')
    
    s = re.sub(r"strftime\('%Y-%m-%dT%H:%M:%SZ','now'\)", 'CURRENT_TIMESTAMP', s)
    s = re.sub(r"strftime\('%Y-%m-%dT%H:%M:%fZ','now'\)", 'CURRENT_TIMESTAMP', s)
    s = re.sub(r"strftime\('%s','now'\)", "(EXTRACT(EPOCH FROM now())::bigint)", s)
    s = s.replace('DEFAULT (CURRENT_TIMESTAMP)', 'DEFAULT CURRENT_TIMESTAMP')
    s = s.replace('printf(', 'format(').replace('instr(', 'strpos(').replace('GROUP_CONCAT(', 'string_agg(')
    s = re.sub(r'string_agg\(([^,]+),\s*', r'string_agg(\1::text, ', s)
    s = re.sub(r"datetime\(([^,]+),\s*'unixepoch'\)", r"to_timestamp(\1)", s)
    
    # regex anchor fix
    s = s.replace('GLOB', '~')
    s = s.replace("'[A-Za-z0-9]*'", "'^[A-Za-z0-9]*$'")
    
    s = s.replace('REFERENCES corps(', 'REFERENCES corporations(').replace('REFERENCES corps ', 'REFERENCES corporations ')
    s = s.replace('INSERT OR IGNORE', 'INSERT')

    if table_name:
        s_paren = s.find('(')
        e_paren = s.rfind(')')
        if s_paren != -1 and e_paren != -1:
            header = s[:s_paren+1]
            body = s[s_paren+1:e_paren]
            footer = s[e_paren:]
            
            parts = split_columns(body)
            new_parts = []
            
            bool_cols = {
                'is_evil', 'is_good', 'can_buy_iss', 'can_rob_ports', 'enabled', 'active',
                'invisible', 'is_npc', 'is_primary', 'genesis_flag', 'resolved', 'ephemeral',
                'locked', 'archived', 'deleted', 'is_percentage', 'is_frozen', 'is_default',
                'can_transwarp', 'can_purchase', 'can_long_range_scan', 'can_planet_scan', 'loggedin'
            }
            
            manual_pks = {
                'taverns', 'corp_recruiting', 'tavern_loans', 'subspace_cursors', 'msl_sectors',
                'stardock_assets', 'podded_status', 'bank_flags', 'corp_accounts', 'sector_gdp',
                'eligible_tows', 'player_last_rob', 'sessions', 'idempotency', 'locks', 'engine_state', 'config'
            }

            for p in parts:
                is_bool = any(re.search(fr'\b{b}\b', p, re.I) for b in bool_cols)
                if is_bool:
                    p = re.sub(fr'\b(INTEGER|BIGSERIAL|TINYINT)\b', 'BOOLEAN', p, flags=re.I)
                    p = p.replace('DEFAULT 0', 'DEFAULT FALSE').replace('DEFAULT 1', 'DEFAULT TRUE')
                    p = re.sub(r'CHECK\s*\(.*?IN\s*\(0,1\)\)', '', p, flags=re.I)
                    p = re.sub(r'CHECK\s*\(.*?IN\s*\(0,\s*1\)\)', '', p, flags=re.I)

                if table_name in manual_pks and 'PRIMARY KEY' in p.upper() and ',' not in p:
                    pk_type = 'TEXT' if table_name in ['config', 'sessions', 'locks', 'idempotency', 'engine_state'] else 'BIGINT'
                    p = re.sub(r'\b(BIGSERIAL|INTEGER)\b', pk_type, p, flags=re.I)

                if table_name == 'ship_roles':
                    if p.lower().startswith('role '): p = 'role TEXT'
                    if p.lower().startswith('role_description '): p = 'role_description TEXT'

                new_parts.append(p)
            
            s = header + ", ".join(new_parts) + footer
            s = s.replace('CREATE TABLE', 'CREATE TABLE IF NOT EXISTS')

    elif 'INDEX' in s.upper():
        s = s.replace('CREATE INDEX', 'CREATE INDEX IF NOT EXISTS').replace('CREATE UNIQUE INDEX', 'CREATE UNIQUE INDEX IF NOT EXISTS')
    elif 'VIEW' in s.upper():
        s = s.replace('CREATE VIEW', 'CREATE OR REPLACE VIEW').replace('WITH all_sectors', 'WITH RECURSIVE all_sectors')

    return s.strip()

with open('sql/sqlite_schema.sql', 'r') as f:
    raw_text = f.read()

stmts = split_statements(raw_text)
conv = [convert_stmt(s) for s in stmts if convert_stmt(s)]

def get_n(s):
    m = re.search(r'CREATE TABLE IF NOT EXISTS (\w+)', s, re.I)
    return m.group(1).lower() if m else None
def get_r(s):
    return [r.lower() for r in re.findall(r'REFERENCES (\w+)', s, re.I)]

tables = [s for s in conv if 'CREATE TABLE IF NOT EXISTS' in s]
others = [s for s in conv if 'CREATE TABLE IF NOT EXISTS' not in s]

ordered = []
rem = list(tables)
for _ in range(100):
    for i in range(len(rem)-1, -1, -1):
        t = rem[i]; name = get_n(t); refs = [r for r in get_r(t) if r != name]
        if all(r in [get_n(o) for o in ordered] or r in ['sectors', 'players', 'corporations'] for r in refs):
            ordered.append(t); rem.pop(i)
ordered.extend(rem)

with open('sql/pg/000_tables.sql', 'w') as f:
    f.write("-- Definitive generated schema Final v19\n\n")
    f.write("BEGIN;\n\n")
    for s in ordered: f.write(s + ";\n\n")
    for s in others: f.write(s + ";\n\n")
    f.write("\n-- Manual Overrides for persistent parsing issues\n")
    f.write("CREATE TABLE IF NOT EXISTS insurance_policies ( id BIGSERIAL PRIMARY KEY, holder_type TEXT NOT NULL CHECK (holder_type IN ('player','corp')), holder_id INTEGER NOT NULL, subject_type TEXT NOT NULL CHECK (subject_type IN ('ship','cargo','planet')), subject_id INTEGER NOT NULL, premium INTEGER NOT NULL CHECK (premium >= 0), payout INTEGER NOT NULL CHECK (payout >= 0), fund_id INTEGER REFERENCES insurance_funds(id) ON DELETE SET NULL, start_ts TEXT NOT NULL, expiry_ts TEXT, active BOOLEAN NOT NULL DEFAULT TRUE );\n")
    f.write("CREATE TABLE IF NOT EXISTS ship_roles ( role_id BIGSERIAL PRIMARY KEY, role TEXT, role_description TEXT );\n")
    f.write("\nCOMMIT;\n")
