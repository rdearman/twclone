import json
import glob
import os

def list_unique_bugs():
    unique_bugs = {}
    base_dir = os.path.dirname(os.path.abspath(__file__))
    log_files = glob.glob(os.path.join(base_dir, 'bot_dir/bug_reports_*/qa_log.jsonl'))
    
    print(f"Scanning {len(log_files)} log files...")
    
    for file_path in log_files:
        try:
            with open(file_path, 'r') as f:
                for line in f:
                    try:
                        entry = json.loads(line)
                        if entry.get('type') == 'response':
                            data = entry.get('data', {})
                            # Check if the response itself indicates an error (status != ok) or if it's an explicit error type
                            # The original script looked for data.get('type') == 'error'
                            # Let's check both standard protocol errors and explicit 'error' types if they exist
                            
                            is_error = False
                            code = "Unknown"
                            message = "Unknown"
                            data_detail = None
                            
                            # Case 1: Standard response with status error
                            if isinstance(data, dict) and data.get('status') == 'error':
                                is_error = True
                                error_info = data.get('error', {})
                                if isinstance(error_info, str): # sometimes error is just a string in older protocols?
                                     message = error_info
                                elif isinstance(error_info, dict):
                                    code = error_info.get('code')
                                    message = error_info.get('message')
                                    data_detail = error_info.get('data')
                            
                            # Case 2: The original script's check (data type is 'error')
                            elif isinstance(data, dict) and data.get('type') == 'error':
                                is_error = True
                                error_info = data.get('error', {})
                                code = error_info.get('code')
                                message = error_info.get('message')
                                data_detail = error_info.get('data')

                            if is_error:
                                # Create a unique key based on code and message
                                key = (code, message)
                                
                                if key not in unique_bugs:
                                    unique_bugs[key] = {
                                        'count': 0,
                                        'example_data': data_detail,
                                        'files': set()
                                    }
                                unique_bugs[key]['count'] += 1
                                unique_bugs[key]['files'].add(os.path.basename(os.path.dirname(file_path)))
                    except json.JSONDecodeError:
                        continue
        except Exception as e:
            print(f"Error reading {file_path}: {e}")

    print(f"Found {len(unique_bugs)} unique bugs:")
    for (code, message), info in unique_bugs.items():
        print(f"- Code: {code}, Message: {message}, Count: {info['count']}")
        print(f"  Found in: {', '.join(list(info['files'])[:5])}...")
        if info['example_data']:
            print(f"  Example Data: {info['example_data']}")

if __name__ == "__main__":
    list_unique_bugs()
