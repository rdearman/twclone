import sys
import os
import uuid
import json
from twclient import TWClient

# Configuration
HOST = os.getenv("HOST", "127.0.0.1")
PORT = int(os.getenv("PORT", 1234))

def run_test():
    client = TWClient(host=HOST, port=PORT)
    username = f"schema_tester_{str(uuid.uuid4())[:8]}"
    password = "password"

    try:
        client.connect()
        
        # 1. Login
        print(f"Authenticating as {username}...")
        client.register(username, password)
        if not client.login(username, password):
            print("Authentication failed.")
            sys.exit(1)
            
        # 2. Request all available commands
        print("Requesting system.cmd_list...")
        cmd_list_request = {
            "id": "req-cmd-list",
            "command": "system.cmd_list",
            "data": {}
        }
        client.send_json(cmd_list_request)
        
        resp = client.recv_next_non_notice()
        if resp.get("status") != "ok" or resp.get("type") != "system.cmd_list":
            print(f"Failed to get command list: {resp}")
            sys.exit(1)
            
        commands = resp.get("data", {}).get("commands", [])
        command_names = [c.get("cmd") for c in commands]
        print(f"Received {len(command_names)} commands.")
        
        # 3. Request schema for each command
        failures = []
        success_count = 0
        
        # Commands to potentially ignore if we want to mimic ai_player strictly, 
        # but the user said "request the schema for them all".
        # So I will NOT ignore any.
        
        for i, cmd_name in enumerate(command_names):
            req_id = f"req-schema-{i}"
            schema_request = {
                "id": req_id,
                "command": "system.describe_schema",
                "data": {"type": "command", "name": cmd_name}
            }
            
            # print(f"Requesting schema for {cmd_name}...")
            client.send_json(schema_request)
            
            try:
                # We expect a response for this specific request
                # Note: In a busy server, we might get other messages.
                # But in a test environment with a new user, it should be clean.
                # We loop until we get a matching response or error.
                
                got_response = False
                while not got_response:
                    resp = client.recv_next_non_notice(timeout=5)
                    
                    # Check if this response matches our request ID if possible
                    # The server should echo the ID in 'reply_to' if we sent 'id'
                    # But looking at ai_player, it sends 'id', and server returns 'reply_to'.
                    
                    if resp.get("reply_to") == req_id:
                        got_response = True
                        if resp.get("status") == "ok":
                            # Check if schema is actually present
                            if "schema" in resp.get("data", {}):
                                success_count += 1
                            else:
                                failures.append(f"{cmd_name}: OK status but missing 'schema' field in data")
                        else:
                            error_msg = resp.get("error", {}).get("message", "Unknown error")
                            failures.append(f"{cmd_name}: Error {error_msg}")
                    else:
                        # Ignore unsolicited messages or mismatched IDs (though unlikely in single-client test)
                        print(f"Ignored unmatched response: {resp.get('type')} id={resp.get('reply_to')}")
            except TimeoutError:
                failures.append(f"{cmd_name}: Timed out waiting for response")
            except Exception as e:
                failures.append(f"{cmd_name}: Exception during receive: {e}")

        print("-" * 40)
        print(f"Schema Check Complete.")
        print(f"Success: {success_count}")
        print(f"Failures: {len(failures)}")
        
        if failures:
            print("Failed Commands:")
            for f in failures:
                print(f"  - {f}")
            sys.exit(1)
        else:
            print("All commands have valid schemas.")
            sys.exit(0)

    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        # import traceback
        # traceback.print_exc()
        sys.exit(1)
    finally:
        client.close()

if __name__ == "__main__":
    run_test()
