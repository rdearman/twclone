import socket
import json
import re
import os
import time
import sys
import uuid # Import uuid module for generating UUIDs
from typing import Any, Dict, List, Optional, Tuple

# Import TWClient - assuming it's available in the same directory or PYTHONPATH
from twclient import TWClient 

class JsonSuiteRunner:
    def __init__(self, host: str, port: int, timeout: int = 25, macros_path: str = "tests.v2/macros.json"):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.macros = self._load_macros(macros_path)
        self.user_clients: Dict[str, TWClient] = {} # Store TWClient instances per user
        self.vars = {} # Global variables for macro expansion
        self._admin_client: Optional[TWClient] = None # Dedicated admin client
        self.run_id = f"_{int(time.time())}"
        self.username_map = {}

    def _load_macros(self, path: str) -> Dict[str, List[Dict[str, Any]]]:
        if not os.path.exists(path):
            return {}
        with open(path, "r") as f:
            return json.load(f)

    def _expand_macros(self, tests: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        expanded_tests = []
        for test in tests:
            if "setup" in test:
                macro_name = test["setup"]
                if macro_name in self.macros:
                    context = test.copy()
                    del context["setup"]
                    
                    macro_steps = []
                    for step in self.macros[macro_name]:
                        resolved_step = json.loads(json.dumps(step))
                        resolved_step = self._expand_vars_with_context(resolved_step, context)
                        macro_steps.append(resolved_step)

                    expanded_macro_steps = self._expand_macros(macro_steps)
                    
                    if "save" in test:
                        if expanded_macro_steps:
                            last_step = expanded_macro_steps[-1]
                            if "save" not in last_step:
                                last_step["save"] = {}
                            last_step["save"].update(test["save"])
                        else:
                            print(f"WARNING: Macro '{macro_name}' expanded to no steps, 'save' field ignored.")

                    expanded_tests.extend(expanded_macro_steps)
            else:
                expanded_tests.append(test)
        return expanded_tests

    def _expand_vars_with_context(self, obj: Any, context: Dict[str, Any]) -> Any:
        if isinstance(obj, str):
            val = obj
            if "*uuid*" in val:
                val = val.replace("*uuid*", str(uuid.uuid4()))
            
            def repl(m):
                k = m.group(1)
                v = context.get(k)
                if v is None:
                    v = self.vars.get(k)
                return str(v) if v is not None else m.group(0)
            
            val = re.sub(r"@(\w+)", repl, val)
            if val.isdigit() and obj.startswith("@"):
                 return int(val)
            return val
        
        if isinstance(obj, list):
            return [self._expand_vars_with_context(x, context) for x in obj]
        if isinstance(obj, dict):
            return {k: self._expand_vars_with_context(v, context) for k, v in obj.items()}
        return obj

    def _expand_vars(self, obj: Any) -> Any:
        return self._expand_vars_with_context(obj, {})

    def _recv_line(self, sock: socket.socket) -> str:
        buf = bytearray()
        while True:
            chunk = sock.recv(1)
            if not chunk: # Connection closed
                return ""
            if chunk == b"\n":
                break
            buf.extend(chunk)
            if len(buf) > 1024 * 1024: # Safety limit
                raise ValueError("Response line too large")
        return buf.decode("utf-8", errors="replace")

    def _get_user_client(self, username: str) -> TWClient:
        if username not in self.user_clients:
            client = TWClient(self.host, self.port)
            client.connect()
            self.user_clients[username] = client
        return self.user_clients[username]

    def _get_admin_client(self) -> TWClient:
        if self._admin_client is None:
            admin_client = TWClient(self.host, self.port)
            admin_client.connect()
            admin_client.register("admin", "password") # Register if not exists
            admin_client.login("admin", "password")    # Log in
            self._admin_client = admin_client
        return self._admin_client

    def run_suite(self, suite_path: str, context_vars: Dict[str, Any] = None) -> bool:
        print(f"Running Suite: {suite_path}")
        with open(suite_path, "r") as f:
            doc = json.load(f)
        
        raw_tests = doc.get("tests", []) if isinstance(doc, dict) else doc
        tests = self._expand_macros(raw_tests)
        
        if context_vars:
            self.vars.update(context_vars)

        all_passed = True
        
        for test in tests:
            if not self._run_test(test):
                all_passed = False
                if test.get("stop_on_fail"):
                    print("Stopping suite due to stop_on_fail.")
                    break
        
        # Close all open client connections
        for client in self.user_clients.values():
            client.close()
        self.user_clients.clear() # Clear for next suite
        
        if self._admin_client:
            self._admin_client.close()
            self._admin_client = None

        return all_passed

    def _run_test(self, test: Dict[str, Any]) -> bool:
        name = test.get("name", "Unnamed")
        print(f"  Test: {name}...", end=" ", flush=True)

        ok = True # Initialize ok
        
        test = self._expand_vars(test)
        
        # Handle delay command
        if test.get("command") == "delay":
            delay_seconds = test.get("data", {}).get("seconds", 0.1)
            time.sleep(delay_seconds)
            print(f"DELAY {delay_seconds}s")
            return True

        user = test.get("user")
        if user == "admin":
            client = self._get_admin_client()
        elif user:
            client = self._get_user_client(user)
        else: # For commands like system.hello that don't require auth
            client = TWClient(self.host, self.port) # Temporary client
            client.connect()


        # Prepare command JSON
        cmd_json = {}
        if "command" in test:
            cmd_json["command"] = test["command"]
            if "data" in test:
                # Deep copy data to avoid modifying the original test definition across iterations/retries
                cmd_json["data"] = json.loads(json.dumps(test["data"]))
            
            # --- USERNAME MAPPING FOR ISOLATION ---
            if cmd_json["command"] == "auth.register":
                data = cmd_json.get("data", {})
                if "username" in data:
                    u = data["username"]
                    if u != "admin" and u != "sys_admin": # Don't map admin or sys_admin
                        new_u = u + self.run_id
                        cmd_json["data"]["username"] = new_u
                        self.username_map[u] = new_u
                        # print(f"[DEBUG] Mapped register {u} -> {new_u}")

            if cmd_json["command"] in ["auth.login", "user.create"]:
                data = cmd_json.get("data", {})
                if "username" in data:
                    u = data["username"]
                    if u in self.username_map:
                        cmd_json["data"]["username"] = self.username_map[u]
                        # print(f"[DEBUG] Mapped login {u} -> {self.username_map[u]}")
            # --------------------------------------

            # If the client has a session (i.e., it's a logged-in user client), inject it
            if client.session_token:
                cmd_json["auth"] = {"session": client.session_token}

        # Handle idempotency_key generation
        if test.get("idempotency_key") == "*generate*":
            if "data" not in cmd_json: cmd_json["data"] = {}
            cmd_json["data"]["idempotency_key"] = str(uuid.uuid4())
        elif isinstance(cmd_json.get("data"), dict) and "idempotency_key" in test:
            cmd_json["data"].setdefault("idempotency_key", test["idempotency_key"])
        
        # Send and Receive
        resp = None
        try:
            # Implement retry logic for Database error during auth.login
            if cmd_json.get("command") == "auth.login" and not test.get("stop_on_fail"):
                for attempt in range(3): # Retry up to 3 times
                    client.send_json(cmd_json)
                    resp = client.recv_next_non_notice()
                    if resp and resp.get("status") == "error" and resp.get("error", {}).get("code") == 1500: # Database error
                        print(f" (Attempt {attempt + 1}: Database error on login, retrying...)")
                        time.sleep(0.1 * (attempt + 1)) # Exponential backoff
                        # Client's socket might be closed after error, re-connect or get new client
                        if user: # Re-get persistent client or recreate temporary one
                            client = self._get_user_client(user)
                        else:
                            client = TWClient(self.host, self.port)
                            client.connect()
                        continue
                    else:
                        break
            else:
                client.send_json(cmd_json)
                resp = client.recv_next_non_notice()
        except Exception as e:
            print(f"FAIL (Communication Error: {e})")
            if user is None: client.close() # Close temporary client
            return False

        if resp is None:
            print("FAIL (No response received)")
            if user is None: client.close() # Close temporary client
            return False

        # Special handling for auth.register with register_if_missing
        if cmd_json.get("command") == "auth.register" and test.get("register_if_missing"):
            status = resp.get("status")
            err_code = (resp.get("error") or {}).get("code")
            if status == "ok" or err_code in [1210, 1205]:
                print(f"PASS (Register skipped/ok: {err_code if status != 'ok' else 'ok'})")
                if user is None: client.close() # Close temporary client
                return True

        # Assertions
        expect = test.get("expect", {"status": "ok"})
        print(f"[DEBUG] Expecting: {expect}") # Print expect block for debugging
        
        if resp.get("status") != expect.get("status"):
            print(f"FAIL (Status: {resp.get('status')} != {expect.get('status')})")
            if resp.get("status") == "error" or resp.get("status") == "refused":
                print(f"  Response: {json.dumps(resp, indent=2)}")
            ok = False
        
        # Capture vars
        if "save" in test:
            for k, path in test["save"].items():
                val = self._get_path(resp, path)
                self.vars[k] = val
        
        # Custom assertions
        if "asserts" in test and ok:
            for assertion in test["asserts"]:
                path = assertion["path"]
                op = assertion["op"]
                value = assertion.get("value") # Safely get value

                actual_value = self._get_path(resp, path)
                
                # Handle special case for sql_int in assertions (to resolve dynamic IDs)
                if isinstance(value, dict) and "sql_int" in value:
                    sql = self._expand_vars_with_context(value["sql_int"], self.vars)
                    admin_client = self._get_admin_client()
                    admin_client.send_json({"command": "sys.raw_sql_exec", "data": {"sql": sql}})
                    sql_resp = admin_client.recv_next_non_notice()
                    if sql_resp.get("status") == "ok" and "rows" in sql_resp["data"] and len(sql_resp["data"]["rows"]) > 0:
                        value = sql_resp["data"]["rows"][0][0]
                    else:
                        print(f"FAIL (Assertion SQL failed for path '{path}': {sql_resp})")
                        ok = False
                        break

                if op == "==":
                    if actual_value != value:
                        print(f"FAIL (Assert: '{path}' == '{value}' but got '{actual_value}')")
                        ok = False
                        break
                elif op == "!=":
                    if actual_value == value:
                        print(f"FAIL (Assert: '{path}' != '{value}' but got '{actual_value}')")
                        ok = False
                        break
                elif op == ">":
                    if not (isinstance(actual_value, (int, float)) and isinstance(value, (int, float)) and actual_value > value):
                        print(f"FAIL (Assert: '{path}' > '{value}' but got '{actual_value}')")
                        ok = False
                        break
                elif op == "<":
                    if not (isinstance(actual_value, (int, float)) and isinstance(value, (int, float)) and actual_value < value):
                        print(f"FAIL (Assert: '{path}' < '{value}' but got '{actual_value}')")
                        ok = False
                        break
                elif op == "contains": # For arrays or strings
                    if isinstance(actual_value, list):
                        if value not in actual_value:
                            print(f"FAIL (Assert: '{path}' contains '{value}' but got '{actual_value}')")
                            ok = False
                            break
                    elif isinstance(actual_value, str):
                        if value not in actual_value:
                            print(f"FAIL (Assert: '{path}' contains '{value}' but got '{actual_value}')")
                            ok = False
                            break
                    else:
                        print(f"FAIL (Assert: 'contains' not applicable to type of '{actual_value}')")
                        ok = False
                        break
                elif op == "*array": # Checks if it's an array and not empty
                    if not (isinstance(actual_value, list) and len(actual_value) > 0):
                        print(f"FAIL (Assert: '{path}' is non-empty array but got '{actual_value}')")
                        ok = False
                        break
                elif op == "type": # Checks type
                    if value == "string" and not isinstance(actual_value, str):
                        print(f"FAIL (Assert: '{path}' is string but got '{type(actual_value).__name__}')")
                        ok = False
                        break
                    elif value == "integer" and not isinstance(actual_value, int):
                        print(f"FAIL (Assert: '{path}' is integer but got '{type(actual_value).__name__}')")
                        ok = False
                        break
                    elif value == "boolean" and not isinstance(actual_value, bool):
                        print(f"FAIL (Assert: '{path}' is boolean but got '{type(actual_value).__name__}')")
                        ok = False
                        break
                    elif value == "object" and not isinstance(actual_value, dict):
                        print(f"FAIL (Assert: '{path}' is object but got '{type(actual_value).__name__}')")
                        ok = False
                        break
                elif op == "defined":
                    if actual_value is None:
                        print(f"FAIL (Assert: '{path}' is defined but got None)")
                        ok = False
                        break
                else:
                    print(f"FAIL (Unknown assertion operator: '{op}')")
                    ok = False
                    break


        if ok:
            print("PASS")
            if cmd_json.get("command") == "sys.raw_sql_exec" and resp.get("status") == "ok" and "data" in resp and "rows" in resp["data"]:
                print(f"  SQL Result: {resp['data']['rows']}")
        
        # Close temporary client if it was created for non-authenticated commands
        if user is None and 'client' in locals() and client:
            client.close()

        # Ensure a fresh client for subsequent operations after a 'setup' macro
        if "setup" in test and ok: # Only if the setup itself passed
            if user and user in self.user_clients:
                self.user_clients[user].close()
                del self.user_clients[user]
            # Special handling for admin client if it was the one used in the setup
            if user == "admin" and self._admin_client:
                self._admin_client.close()
                self._admin_client = None

        return ok

    def _do_recv(self, client: TWClient, max_reads: int) -> Optional[Dict[str, Any]]:
        for _ in range(max_reads):
            resp_line = client.recv_next_line() # Use TWClient's recv_next_line
            if not resp_line: # Connection closed or empty line
                return None 
            try:
                r = json.loads(resp_line)
                if r.get("type") == "system.notice": # Skip notices
                    continue
                return r # Return first non-notice response
            except json.JSONDecodeError:
                print(f"WARNING: Invalid JSON received: {resp_line}")
        return None # Max reads reached without valid response

    def _get_path(self, obj, path):
        parts = path.split(".")
        curr = obj
        for p in parts:
            if isinstance(curr, dict):
                curr = curr.get(p)
            elif isinstance(curr, list):
                try:
                    # Handle python list indexing vs JSON pathing
                    if p.startswith('*'): # Wildcard for any item in a list
                        # Return the list itself to be processed by 'contains' or similar
                        return curr
                    else:
                        curr = curr[int(p)]
                except (ValueError, IndexError):
                    return None
            else:
                return None
        return curr

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 json_runner.py <suite_file.json>")
        sys.exit(1)
    
    suite_file = sys.argv[1]
    host = os.getenv("TW_SERVER_HOST", "localhost")
    port = int(os.getenv("TW_SERVER_PORT", 1234))

    runner = JsonSuiteRunner(host, port)
    if not runner.run_suite(suite_file):
        sys.exit(1)