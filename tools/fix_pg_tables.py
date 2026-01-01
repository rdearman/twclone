import re

def convert_sqlite_to_pg(text):
    # 1. NORMALIZE WHITESPACE FIRST
    # Replace multiple spaces/newlines/tabs with a single space
    text = re.sub(r'\s+', ' ', text).strip()
    
    # 2. Basic Type Conversions (Safe global replace)
    text = text.replace('INTEGER PRIMARY KEY AUTOINCREMENT', 'BIGSERIAL PRIMARY KEY')
    text = text.replace('INTEGER PRIMARY KEY', 'BIGSERIAL PRIMARY KEY')
    text = text.replace('DATETIME', 'TIMESTAMP')
    text = text.replace('REAL', 'DOUBLE PRECISION')
    text = text.replace('COLLATE NOCASE', '')
    
    # 3. Function Conversions
    text = re.sub(r"strftime\('%Y-%m-%dT%H:%M:%SZ','now'\)", 'CURRENT_TIMESTAMP', text)
    text = re.sub(r"strftime\('%Y-%m-%dT%H:%M:%fZ','now'\)", 'CURRENT_TIMESTAMP', text)
    text = re.sub(r"strftime\('%s','now'\)", "(EXTRACT(EPOCH FROM now())::bigint)", text)
    text = text.replace('DEFAULT (CURRENT_TIMESTAMP)', 'DEFAULT CURRENT_TIMESTAMP')
    
    # 4. Global Boolean Type conversion for known columns
    bool_columns = [
        'is_evil', 'is_good', 'can_buy_iss', 'can_rob_ports', 'enabled', 'active',
        'invisible', 'is_npc', 'is_primary', 'genesis_flag', 'resolved', 'ephemeral',
        'locked', 'archived', 'deleted', 'is_percentage', 'is_frozen', 'is_default',
        'can_transwarp', 'can_purchase', 'can_long_range_scan', 'can_planet_scan', 'loggedin'
    ]
    for col in bool_columns:
        # \b ensures we match the whole word
        text = re.sub(fr'\b({col})\b\s+(INTEGER|BIGSERIAL|TINYINT|BOOLEAN)', r'\1 BOOLEAN', text, flags=re.I)

    # 5. Global Boolean Default and Check constraint cleanup
    text = re.sub(r'(BOOLEAN.*?DEFAULT\s+)0\b', r'\1FALSE', text, flags=re.I)
    text = re.sub(r'(BOOLEAN.*?DEFAULT\s+)1\b', r'\1TRUE', text, flags=re.I)
    # Flexible check removal
    text = re.sub(r'CHECK\s*\(.*?\s+IN\s*\(.*?0.*?,.*?1.*?\)\)', '', text, flags=re.I)

    # 6. Manual PK tables fixes (revert from BIGSERIAL if appropriate)
    manual_pk_tables = {
        'config': 'TEXT', 'sessions': 'TEXT', 'locks': 'TEXT', 
        'engine_state': 'TEXT', 'idempotency': 'TEXT',
        'taverns': 'BIGINT', 'corp_recruiting': 'BIGINT', 'tavern_loans': 'BIGINT', 
        'subspace_cursors': 'BIGINT', 'msl_sectors': 'BIGINT', 'stardock_assets': 'BIGINT', 
        'podded_status': 'BIGINT', 'bank_flags': 'BIGINT', 'corp_accounts': 'BIGINT', 
        'sector_gdp': 'BIGINT', 'eligible_tows': 'BIGINT', 'player_last_rob': 'BIGINT'
    }
    
    for table, pk_type in manual_pk_tables.items():
        # Look for the start of the table definition
        pattern = fr'(CREATE TABLE {table} \( [a-zA-Z0-9_]+ )BIGSERIAL PRIMARY KEY'
        text = re.sub(pattern, fr'\1{pk_type} PRIMARY KEY', text, flags=re.I)

    # 7. View and Index fixes
    text = text.replace('CREATE VIEW', 'CREATE OR REPLACE VIEW')
    text = text.replace('WITH all_sectors', 'WITH RECURSIVE all_sectors')
    text = text.replace('instr(', 'strpos(')
    text = text.replace('GROUP_CONCAT(', 'string_agg(')
    # Fix string_agg parameters (assuming second param is delimiter)
    text = re.sub(r'string_agg\(([^,]+),\s*([^)]+)\)', r'string_agg(\1::text, \2)', text)
    
    # 8. IF NOT EXISTS
    text = text.replace('CREATE TABLE', 'CREATE TABLE IF NOT EXISTS')
    text = text.replace('CREATE INDEX', 'CREATE INDEX IF NOT EXISTS')
    text = text.replace('CREATE UNIQUE INDEX', 'CREATE UNIQUE INDEX IF NOT EXISTS')
    
    # 9. Final Cleanup
    text = text.replace('role BOOLEAN', 'role TEXT')
    text = text.replace('role_description TEXT DEFAULT TRUE', 'role_description TEXT')
    text = text.replace('REFERENCES corps(', 'REFERENCES corporations(')
    text = text.replace('REFERENCES corps ', 'REFERENCES corporations ')
    text = text.replace('GLOB', '~')
    
    # 10. Split statements by semicolon for pretty output
    statements = text.split(';')
    return ";\n\n".join([s.strip() for s in statements if s.strip()]) + ";"

with open('sql/sqlite_schema.sql', 'r') as f:
    content = f.read()

pg_content = convert_sqlite_to_pg(content)

with open('sql/pg/000_tables.sql', 'w') as f:
    f.write("-- Complete generated schema v9\n\nBEGIN;\n\n")
    f.write(pg_content)
    f.write("\n\nCOMMIT;\n")
