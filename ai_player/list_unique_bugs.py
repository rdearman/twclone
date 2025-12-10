import json

def list_unique_bugs(file_path):
    unique_bugs = {}
    
    try:
        with open(file_path, 'r') as f:
            for line in f:
                try:
                    entry = json.loads(line)
                    if entry.get('type') == 'response':
                        data = entry.get('data', {})
                        if data.get('type') == 'error':
                            error_info = data.get('error', {})
                            code = error_info.get('code')
                            message = error_info.get('message')
                            data_detail = error_info.get('data')
                            
                            # Create a unique key based on code and message
                            key = (code, message)
                            
                            if key not in unique_bugs:
                                unique_bugs[key] = {
                                    'count': 0,
                                    'example_data': data_detail
                                }
                            unique_bugs[key]['count'] += 1
                except json.JSONDecodeError:
                    continue
                    
        print(f"Found {len(unique_bugs)} unique bugs:")
        for (code, message), info in unique_bugs.items():
            print(f"- Code: {code}, Message: {message}, Count: {info['count']}")
            if info['example_data']:
                print(f"  Example Data: {info['example_data']}")
                
    except FileNotFoundError:
        print(f"File not found: {file_path}")

if __name__ == "__main__":
    list_unique_bugs('bug_reports/qa_log.jsonl')
