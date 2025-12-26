import os
import re
import json

TARGET_FUNCTIONS = [
    "player_is_sysop",
    "cmd_auth_session",
    "cmd_auth_mfa_totp_setup",
    "cmd_auth_mfa_totp_verify",
    "cmd_auth_check_username",
    "cmd_auth_reset_password",
    "is_category_in_filter",
    "news_post",
    "h_handle_npc_encounters",
    "fer_init",
    "fer_tick",
    "fer_create_new",
    "parse_neighbors_csv",
    "db_bank_apply_interest",
    "db_bank_process_orders",
    "h_get_system_account_id_unlocked",
    "h_get_account_id_unlocked",
    "h_create_bank_account_unlocked",
    "h_get_config_int_unlocked",
    "h_get_account_alert_threshold_unlocked",
    "h_add_credits_unlocked",
    "h_deduct_credits_unlocked",
    "h_add_credits",
    "h_deduct_credits",
    "calculate_fees",
    "h_update_planet_stock",
    "h_get_bank_balance",
    "db_get_player_bank_balance",
    "db_get_corp_bank_balance",
    "db_get_npc_bank_balance",
    "db_get_port_bank_balance",
    "db_get_planet_bank_balance",
    "db_bank_account_exists",
    "db_bank_create_account",
    "db_bank_deposit",
    "db_bank_withdraw",
    "db_bank_transfer",
    "db_port_get_active_busts",
    "h_create_personal_bank_alert_notice",
    "h_bank_transfer_unlocked",
    "db_apply_lock_policy_for_pilot"
]

SRC_DIR = 'src'

results = []
summary = {"found": 0, "renamed": 0, "missing": 0}

# Pre-compile regexes
# Match definition: start of line (allowing for return type on prev line approx), return type, name, (
# We'll use a slightly looser match to catch multi-line return types: 
# "Any alphanumeric/pointer/space stuff" then "func_name" then "("
# And we ensure it's not a prototype by looking for "{" before ";" or assuming .c file definitions.
def_patterns = {}
for func in TARGET_FUNCTIONS:
    # A simplified C function definition regex:
    # ^ [static]? [return type]? func ( ... )
    # Note: This is heuristic.
    def_patterns[func] = re.compile(r'(?:^|\n)\s*(?:static\s+)?[[\w\s\*]+\s+' + re.escape(func) + r'\s*\(', re.MULTILINE)

files_content = {}

# 1. Read all files
for root, dirs, files in os.walk(SRC_DIR):
    for file in files:
        if file.endswith('.c') or file.endswith('.h'):
            path = os.path.join(root, file)
            try:
                with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                    files_content[path] = f.read()
            except Exception as e:
                pass

# 2. Search
for func in TARGET_FUNCTIONS:
    found = False
    func_result = {
        "name": func,
        "status": "MISSING",
        "definition": {},
        "rename": {},
        "references": [],
        "notes": ""
    }
    
    # Check for definition
    for path, content in files_content.items():
        if path.endswith('.h'): continue # Prefer .c definitions
        
        match = def_patterns[func].search(content)
        if match:
            # Verify it's not a prototype (simple check: does line end with ;?)
            # Or scan ahead for {
            start_idx = match.start()
            end_idx = match.end()
            
            # naive check: look ahead for { or ;
            chunk = content[end_idx:end_idx+200]
            if ';' in chunk and ('{' not in chunk or chunk.index(';') < chunk.index('{')):
                # Likely prototype
                continue
                
            line_no = content[:start_idx].count('\n') + 1
            line_str = match.group(0).strip()
            
            is_static = "static" in line_str
            
            func_result["status"] = "FOUND"
            func_result["definition"] = {
                "file": path,
                "line_start": line_no,
                "static": is_static,
                "signature": line_str.split('\n')[-1] # approximate
            }
            found = True
            break
            
    if found:
        summary["found"] += 1
    else:
        # Check for refs
        refs = []
        for path, content in files_content.items():
            # Find all occurrences that are NOT definitions
            # Just simple word match for now
            if func in content:
                for line_idx, line in enumerate(content.splitlines()):
                    if func in line:
                        # Exclude obvious false positives if needed
                        refs.append({
                            "file": path,
                            "line": line_idx + 1,
                            "context": line.strip()[:100]
                        })
        func_result["references"] = refs
        
        if len(refs) == 0:
             # Try to find renames? (Very hard programmatically without AST, but we can guess)
             # e.g. check for same prefix
             pass
        
        summary["missing"] += 1

    results.append(func_result)

output = {
    "summary": summary,
    "results": results
}

print(json.dumps(output, indent=2))
