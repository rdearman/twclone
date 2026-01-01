import socket
import json
import re
import os
import time
import sys
import uuid
from typing import Any, Dict, List, Optional, Tuple

# Import TWClient - assuming it's available in the same directory or PYTHONPATH
from twclient import TWClient

class JsonSuiteRunner:
    def __init__(self, host: str, port: int, timeout: int = 25, macros_path: str = "tests.v2/macros.json"):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.macros = self._load_macros(macros_path)
        self.user_clients: Dict[str, TWClient] = {}
        self.vars = {}
        self._admin_client: Optional[TWClient] = None
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
                    
                    # Don't mangle usernames - use them as-is from the test database
                    # This allows tests to reference pre-created users without modification

                    macro_steps = []
                    for step in self.macros[macro_name]:
                        # Resolved step inherits context from the call
                        resolved_step = json.loads(json.dumps(step))
                        # Expand macro steps with the context provided in the 'setup' call
                        resolved_step = self._expand_vars_with_context(resolved_step, context)
                        macro_steps.append(resolved_step)

                    # Macros can be nested
                    expanded_macro_steps = self._expand_macros(macro_steps)
                    
                    if "save" in test:
                        if expanded_macro_steps:
                            last_step = expanded_macro_steps[-1]
                            if "save" not in last_step: last_step["save"] = {}
                            last_step["save"].update(test["save"])
                    
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
                full_match = m.group(0)
                k = m.group(1)
                
                # 1. Check verbatim in context or vars
                v = context.get(k)
                if v is None: v = self.vars.get(k)
                if v is not None: return str(v)
                
                # 2. Return unresolved for late binding
                return full_match

            val = re.sub(r"@([a-zA-Z0-9_-]+)", repl, val)
            if val.startswith("@") and val[1:].isdigit() and len(val.split()) == 1:
                 return int(val[1:])
            return val
        
        if isinstance(obj, list):
            return [self._expand_vars_with_context(x, context) for x in obj]
        if isinstance(obj, dict):
            # Expand both keys and values
            return {self._expand_vars_with_context(k, context): self._expand_vars_with_context(v, context) for k, v in obj.items()}
        return obj

    def _expand_vars(self, obj: Any) -> Any:
        return self._expand_vars_with_context(obj, {})

    def _get_user_client(self, username: str) -> TWClient:
        if username not in self.user_clients:
            client = TWClient(self.host, self.port)
            client.connect()
            self.user_clients[username] = client
        return self.user_clients[username]

    def _get_admin_client(self) -> TWClient:
        if not self._admin_client:
            self._admin_client = TWClient(self.host, self.port)
            self._admin_client.connect()
            self._admin_client.login("Federation Administrator", "BOT")
        return self._admin_client

    def run_suite(self, suite_path: str) -> bool:
        print(f"Running Suite: {suite_path}")
        if not os.path.exists(suite_path):
            print(f"Error: Suite file {suite_path} not found.")
            return False
        with open(suite_path, "r") as f:
            data = json.load(f)
        
        raw_tests = data.get("tests", [])
        tests = self._expand_macros(raw_tests)
        
        all_passed = True
        for test in tests:
            if not self._run_test(test):
                all_passed = False
                if test.get("stop_on_fail") or data.get("stop_on_fail"):
                    break
        
        for client in self.user_clients.values():
            client.close()
        self.user_clients.clear()
        if self._admin_client:
            self._admin_client.close()
            self._admin_client = None
        return all_passed

    def _run_test(self, test: Dict[str, Any]) -> bool:
        name = test.get("name", "Unnamed")
        print(f"  Test: {name}...", end=" ", flush=True)

        # Expand global variables (@var)
        test = self._expand_vars(test)
        
        if test.get("command") == "delay":
            time.sleep(test.get("data", {}).get("seconds", 0.1))
            print("DELAY")
            return True

        user = test.get("user")
        if user == "admin":
            client = self._get_admin_client()
        elif user:
            # Use user directly without mangling
            client = self._get_user_client(user)
        else:
            client = TWClient(self.host, self.port)
            client.connect()

        cmd_json = {}
        if "command" in test:
            cmd_json["command"] = test["command"]
            if "data" in test:
                cmd_json["data"] = json.loads(json.dumps(test["data"]))

            if client.session_token:
                cmd_json["auth"] = {"session": client.session_token}

        # Send and Receive
        resp = None
        try:
            client.send_json(cmd_json)
            resp = client.recv_next_non_notice()
        except Exception as e:
            print(f"FAIL (Communication Error: {e})")
            return False

        if not resp:
            print("FAIL (No response)")
            return False

        # register_if_missing logic
        if cmd_json.get("command") == "auth.register" and test.get("register_if_missing"):
            if resp.get("status") == "ok" or (resp.get("error") or {}).get("code") == 1105:
                print("PASS (Register ok/exists)")
                return True

        # Assertions
        expect = test.get("expect", {"status": "ok"})
        actual_status = resp.get("status")
        actual_type = resp.get("type")
        expected_status = expect.get("status", "ok")
        
        # If error_code is provided without explicit status, assume 'error' or 'refused'
        if "error_code" in expect and expected_status == "ok":
            expected_status = "error"

        passed_status = (actual_status == expected_status)
        if not passed_status and expected_status not in ["ok", "error", "refused"]:
            if actual_type == expected_status:
                passed_status = True
        
        # Flexible error matching: allow 'error' to match 'refused' if requested
        if not passed_status and expected_status == "error" and actual_status == "refused":
            passed_status = True

        if not passed_status:
            print(f"FAIL (Status: {actual_status} != {expected_status})")
            print(f"  Response: {json.dumps(resp, indent=2)}")
            return False
        
        # Check error code if specified
        if "error_code" in expect:
            actual_err = resp.get("error") or {}
            actual_code = actual_err.get("code")
            if actual_code != expect["error_code"]:
                print(f"FAIL (Error Code: {actual_code} != {expect['error_code']})")
                return False
        
        # Save vars
        if "save" in test:
            for var_name, path in test["save"].items():
                val = self._get_path(resp, path)
                if var_name.startswith("@"):
                    var_name = var_name[1:]
                self.vars[var_name] = val
        
        # Custom assertions
        if "asserts" in test:
            for assertion in test["asserts"]:
                actual = self._get_path(resp, assertion["path"])
                expected = assertion.get("value")
                # Handle sql_int in asserts
                if isinstance(expected, dict) and "sql_int" in expected:
                    sql = self._expand_vars(expected["sql_int"])
                    admin = self._get_admin_client()
                    admin.send_json({"command": "sys.raw_sql_exec", "data": {"sql": sql}})
                    sql_resp = admin.recv_next_non_notice()
                    if sql_resp.get("status") == "ok" and sql_resp["data"]["rows"]:
                        expected = sql_resp["data"]["rows"][0][0]
                
                op = assertion["op"]
                if op == "==":
                    if actual != expected:
                        print(f"FAIL (Assert: {actual} != {expected})")
                        return False
                elif op == "contains":
                    # If actual is a list, check if expected is in it
                    if isinstance(actual, list):
                        found = False
                        for item in actual:
                            if expected == item or str(expected) in str(item):
                                found = True
                                break
                        if not found:
                            print(f"FAIL (Assert: {actual} does not contain {expected})")
                            return False
                    else:
                        if str(expected) not in str(actual):
                            print(f"FAIL (Assert: {actual} does not contain {expected})")
                            return False

        print("PASS")
        # Auto-refresh all sessions after admin SQL
        if cmd_json.get("command") == "sys.raw_sql_exec" and user == "admin":
            for c in self.user_clients.values():
                if c.session_token:
                    c.send_json({"command": "auth.refresh", "data": {"session_token": c.session_token}})
                    r = c.recv_next_non_notice()
                    if r.get("status") == "ok": c.session_token = r.get("data", {}).get("session_token")

        if user is None: client.close()
        return True

    def _get_path(self, obj: Any, path: str) -> Any:
        if not path: return obj
        parts = path.split(".")
        curr = obj
        for p in parts:
            if isinstance(curr, dict):
                curr = curr.get(p)
            elif isinstance(curr, list):
                if p.isdigit():
                    idx = int(p)
                    curr = curr[idx] if idx < len(curr) else None
                elif p.startswith("*[") and p.endswith("]"):
                    # Filter syntax: *[key=val]
                    inner = p[2:-1]
                    if "=" in inner:
                        fk, fv = inner.split("=", 1)
                        found = None
                        for item in curr:
                            if isinstance(item, dict) and str(item.get(fk)) == fv:
                                found = item
                                break
                        curr = found
                    else:
                        return None
                else:
                    return None
            else:
                return None
        return curr

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: json_runner.py <suite.json>")
        sys.exit(1)
    runner = JsonSuiteRunner("localhost", 1234)
    success = runner.run_suite(sys.argv[1])
    sys.exit(0 if success else 1)