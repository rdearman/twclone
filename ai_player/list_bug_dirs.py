import os
import json

def list_unique_bug_reports(root_dir):
    unique_bugs = {}
    
    if not os.path.exists(root_dir):
        print(f"Directory not found: {root_dir}")
        return

    for entry in os.scandir(root_dir):
        if entry.is_dir():
            summary_path = os.path.join(entry.path, 'summary.json')
            if os.path.exists(summary_path):
                try:
                    with open(summary_path, 'r') as f:
                        data = json.load(f)
                        message = data.get('message', 'No message')
                        # You might want to include other fields to differentiate, e.g.
                        # context = data.get('context', '') 
                        
                        key = message 
                        
                        if key not in unique_bugs:
                            unique_bugs[key] = 0
                        unique_bugs[key] += 1
                except Exception as e:
                    print(f"Error reading {summary_path}: {e}")
            else:
                # If no summary.json, maybe use the directory name as a bug type indicator
                # grouping by prefix
                name = entry.name
                if name.startswith('err_'):
                    # extraction of error type from folder name
                    # e.g. err_move.warp_1402 -> err_move.warp
                    # e.g. err_state_inconsistent_... -> err_state_inconsistent
                    parts = name.split('_')
                    if len(parts) > 2 and parts[1] == 'state' and parts[2] == 'inconsistent':
                         key = "err_state_inconsistent"
                    elif '.' in name:
                         key = name.split('_')[1] # heuristic
                    else:
                         key = name
                    
                    if key not in unique_bugs:
                        unique_bugs[key] = 0
                    unique_bugs[key] += 1

    print(f"Found {len(unique_bugs)} unique bug patterns in directories:")
    for message, count in unique_bugs.items():
        print(f"- {message}: {count} occurrences")

if __name__ == "__main__":
    list_unique_bug_reports('bug_reports')
