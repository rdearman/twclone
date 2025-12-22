
import socket
import json
import sys

# Verification script for schema harvesting
# Usage: python3 verify_schema_harvest.py [host] [port]

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 1234

def send_request(sock, req):
    line = json.dumps(req) + "\n"
    sock.sendall(line.encode('utf-8'))
    data = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
        if b"\n" in chunk:
            break
    return json.loads(data.decode('utf-8').strip())

def main():
    print(f"Connecting to {HOST}:{PORT}...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((HOST, PORT))
    except Exception as e:
        print(f"Connection failed: {e}")
        sys.exit(1)

    # 1. Get Command List
    print("Fetching command list...")
    list_req = {"command": "system.cmd_list"}
    list_resp = send_request(s, list_req)
    
    if list_resp.get("status") != "ok":
        print(f"Failed to get cmd_list: {list_resp}")
        sys.exit(1)
        
    commands = list_resp["data"]["commands"]
    print(f"Found {len(commands)} commands.")
    
    missing_schemas = []
    stubs = []
    timeouts = []
    
    # 2. Iterate and fetch schemas
    for i, cmd in enumerate(commands):
        name = cmd["cmd"]
        print("SCHEMA:", name, flush=True)
        
        schema_req = {"command": "system.describe_schema", "data": {"name": name}}
        resp = send_request(s, schema_req)
        
        if resp.get("status") == "ok":
            schema = resp["data"]["schema"]
            if schema.get("$comment") == "Not yet implemented":
                stubs.append(name)
        else:
            code = resp.get("error", {}).get("code")
            msg = resp.get("error", {}).get("message")
            print(f"[FAIL] {name}: {code} - {msg}")
            if code == 1104:
                timeouts.append(name)
            else:
                missing_schemas.append(name)

    print("\n\n--- Report ---")
    print(f"Total Commands: {len(commands)}")
    print(f"Fetchable Schemas: {len(commands) - len(missing_schemas) - len(timeouts)}")
    print(f"Not Yet Implemented: {len(stubs)}")
    
    if stubs:
        print(f"Stub List: {stubs}")

    if not missing_schemas and not timeouts:
        print("\nSUCCESS: All advertised commands have fetchable schemas.")
    else:
        if missing_schemas:
            print(f"Missing Schemas ({len(missing_schemas)}): {missing_schemas}")
        if timeouts:
            print(f"Timeouts (1104) ({len(timeouts)}): {timeouts}")
        sys.exit(1)

if __name__ == "__main__":
    main()
