import socket
import json
import re
import os
import time
import sys
import uuid
import argparse
import threading
from decimal import Decimal, InvalidOperation
from typing import Any, Dict, List, Optional, Tuple

# Import TWClient - assuming it's available in the same directory or PYTHONPATH
from twclient import TWClient

_MISSING = object()

class JsonSuiteRunner:
    def __init__(self, host: str, port: int, timeout: int = 25, macros_path: str = "tests.v2/macros.json",
                 use_tls: bool = False, tls_skip_verify: bool = False,
                 capture_responses: Optional[str] = None,
                 client_idle_timeout_ms: int = 8000):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.client_idle_timeout_seconds = max(0.025, client_idle_timeout_ms / 1000.0)
        self.default_use_tls = use_tls
        self.default_tls_skip_verify = tls_skip_verify
        self.use_tls = use_tls
        self.tls_skip_verify = tls_skip_verify
        self.macros = self._load_macros(macros_path)
        self.user_clients: Dict[str, TWClient] = {}
        self.user_credentials: Dict[str, Tuple[str, str]] = {}
        self.vars = {}
        self._admin_client: Optional[TWClient] = None
        self.run_id = f"_{int(time.time())}"
        self.username_map = {}
        self.capture_responses = capture_responses
        self.active_suite = None
        self.last_suite_report = None
        self.case_results = []
        self._current_response = None
        self._current_failure_reason = None
        self._suite_case_index = 0
        self._current_case_id = None

    @staticmethod
    def _redact_response(value):
        if isinstance(value, dict):
            return {
                key: "<redacted>" if key.lower() in ("session_token", "access_token")
                else JsonSuiteRunner._redact_response(item)
                for key, item in value.items()
            }
        if isinstance(value, list):
            return [JsonSuiteRunner._redact_response(item) for item in value]
        return value

    def _capture_response(self, test_name, command, response):
        if not self.capture_responses:
            return
        record = {
            "suite": self.active_suite,
            "id": self._current_case_id,
            "test": test_name,
            "command": command,
            "response": self._redact_response(response),
        }
        with open(self.capture_responses, "a", encoding="utf-8") as output:
            output.write(json.dumps(record, separators=(",", ":"), ensure_ascii=False))
            output.write("\n")

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
                if isinstance(macro_name, str) and macro_name in self.macros:
                    context = test.copy()
                    del context["setup"]
                    
                    macro_steps = []
                    for step in self.macros[macro_name]:
                        # Resolved step inherits context from the call
                        resolved_step = json.loads(json.dumps(step))
                        # Merge call context (like username/password) into the step WITHOUT expanding
                        for k, v in context.items():
                            if k not in resolved_step:
                                resolved_step[k] = v
                        macro_steps.append(resolved_step)

                    # Macros can be nested
                    expanded_macro_steps = self._expand_macros(macro_steps)
                    
                    if "save" in test:
                        if expanded_macro_steps:
                            last_step = expanded_macro_steps[-1]
                            if "save" not in last_step: last_step["save"] = {}
                            last_step["save"].update(test["save"])
                    
                    expanded_tests.extend(expanded_macro_steps)
                elif macro_name == []:
                    # Some historical transport suites used an empty setup
                    # array to mean "no setup". Do not use that list as a
                    # dictionary key (and do not silently treat its request
                    # DSL as a passing command; run_suite validates that next).
                    normalized = dict(test)
                    normalized.pop("setup", None)
                    expanded_tests.append(normalized)
                else:
                    expanded_tests.append(test)
            else:
                expanded_tests.append(test)
        return expanded_tests

    def _expand_vars_with_context(self, obj: Any, context: Dict[str, Any], use_globals: bool = True) -> Any:
        if isinstance(obj, str):
            original_value = obj
            val = obj
            if "*uuid*" in val:
                val = val.replace("*uuid*", str(uuid.uuid4()))
            if "@{timestamp}" in val:
                val = val.replace("@{timestamp}", str(int(time.time())))
            
            # 1. Pattern-based expansion for macro arguments (local context)
        
            # Support both @{var} and @var syntax
            def context_repl(m):
                k = m.group(1) or m.group(2)
                # If k is in context, replace it. Macro args like @username are strings here.
                if k in context:
                    return str(context[k])
                return m.group(0)
            
            val = re.sub(r"@\{([a-zA-Z0-9_-]+)\}|@([a-zA-Z0-9_-]+)", context_repl, val)

            # 2. Pattern-based expansion for global variables
            if use_globals:
                limit = 5
                while "@" in val and limit > 0:
                    limit -= 1
                    new_val = val
                    def global_repl(m):
                        k = m.group(1) or m.group(2)
                        if k in self.vars:
                            return str(self.vars[k])
                        return m.group(0)
                    
                    new_val = re.sub(r"@\{([a-zA-Z0-9_-]+)\}|@([a-zA-Z0-9_-]+)", global_repl, val)
                    if new_val == val: break
                    val = new_val

            # Handle integer conversion for resolved values
            if isinstance(val, str) and "@" in original_value:
                # If it's a pure numeric string (resolved from a var), return as int
                if val.isdigit() and (len(val) == 1 or val[0] != '0'):
                    return int(val)

            return val
        
        if isinstance(obj, list):
            return [self._expand_vars_with_context(x, context, use_globals) for x in obj]
        if isinstance(obj, dict):
            # Expand both keys and values
            new_obj = {}
            for k, v in obj.items():
                new_k = self._expand_vars_with_context(k, context, use_globals)
                new_v = self._expand_vars_with_context(v, context, use_globals)
                new_obj[str(new_k)] = new_v
            return new_obj
        return obj

    def _expand_vars(self, obj: Any) -> Any:
        return self._expand_vars_with_context(obj, {}, use_globals=True)

    @staticmethod
    def _fixture_sql(sql: str) -> str:
        """Keep legacy fixture positioning atomic under V3 ship invariants.

        Older JSON fixtures moved a player and then moved that player's active
        ship in a second request.  V3 deliberately validates the relationship
        at each transaction boundary.  Preserve the fixture's public sysop
        SQL path while making the player-positioning statement move the active
        ship in the same transaction.  The following legacy ship statement is
        harmless and remains valid, so existing cases and assertions are
        preserved.
        """
        if not isinstance(sql, str):
            return sql
        match = re.match(
            r"^\s*UPDATE\s+players\s+SET\s+(?P<set>.+?)\s+WHERE\s+(?P<where>.+?)\s*;?\s*$",
            sql,
            re.IGNORECASE | re.DOTALL,
        )
        if not match or not re.search(r"\bsector_id\s*=", match.group("set"), re.IGNORECASE):
            return sql
        if re.search(r"\bship_id\b", match.group("where"), re.IGNORECASE):
            return sql
        sector = re.search(r"\bsector_id\s*=\s*([^,]+)", match.group("set"), re.IGNORECASE)
        if not sector:
            return sql
        target_sector = sector.group(1).strip()
        predicate = match.group("where").strip()
        return (
            "WITH moved_active_ship AS ("
            f"UPDATE ships SET sector_id = {target_sector} "
            f"WHERE ship_id IN (SELECT ship_id FROM players WHERE {predicate})"
            ") "
            f"UPDATE players SET {match.group('set').strip()} WHERE {predicate};"
        )

    def _get_user_client(self, username: str, reauthenticate: bool = True) -> TWClient:
        client = self.user_clients.get(username)
        if client is None or not client.is_connected(self.client_idle_timeout_seconds):
            previous_token = client.session_token if client is not None else None
            if client is not None:
                client.close()
            client = TWClient(self.host, self.port, timeout=self.timeout,
                            use_tls=self.use_tls, tls_skip_verify=self.tls_skip_verify)
            client.connect()
            credentials = self.user_credentials.get(username)
            if reauthenticate and credentials and not client.login(*credentials):
                client.close()
                raise ConnectionError(f"could not re-authenticate JSON test user {username}")
            if not reauthenticate and previous_token:
                # Some acceptance cases must prove that a revoked token fails
                # on a fresh transport. Preserve that token without silently
                # converting the check into a successful credential login.
                client.session_token = previous_token
            self.user_clients[username] = client
        return self.user_clients[username]

    def _forget_client(self, username: Optional[str], client: Optional[TWClient]) -> None:
        if client is None:
            return
        if username is not None and self.user_clients.get(username) is client:
            self.user_clients.pop(username, None)
        if self._admin_client is client:
            self._admin_client = None
        client.close()

    def _close_suite_clients(self) -> None:
        """Best-effort cleanup, isolated from the next suite's transport mode."""
        clients = list(self.user_clients.values())
        self.user_clients.clear()
        self.user_credentials.clear()
        if self._admin_client is not None:
            clients.append(self._admin_client)
            self._admin_client = None
        for client in clients:
            try:
                client.close()
            except OSError:
                pass

    def _get_admin_client(self) -> TWClient:
        if (self._admin_client is not None
                and not self._admin_client.is_connected(self.client_idle_timeout_seconds)):
            self._admin_client.close()
            self._admin_client = None
        if not self._admin_client:
            self._admin_client = TWClient(self.host, self.port, timeout=self.timeout,
                                        use_tls=self.use_tls, tls_skip_verify=self.tls_skip_verify)
            self._admin_client.connect()
            # Try System/BOT first as it's the standard bootstrap admin
            if not self._admin_client.login("System", "BOT"):
                # Fallback to admin/password
                self._admin_client.login("admin", "password")
        return self._admin_client

    def run_suite(self, suite_path: str) -> bool:
        print(f"Running Suite: {suite_path}")
        self.active_suite = suite_path
        self.case_results = []
        self._suite_case_index = 0
        self._current_case_id = None
        self.use_tls = self.default_use_tls
        self.tls_skip_verify = self.default_tls_skip_verify
        self._close_suite_clients()
        if not os.path.exists(suite_path):
            print(f"Error: Suite file {suite_path} not found.")
            self.last_suite_report = {
                "suite": suite_path,
                "status": "blocked",
                "reason": "suite file not found",
                "cases": [],
            }
            return False
        with open(suite_path, "r") as f:
            data = json.load(f)

        legacy_request_tests = [
            test for test in data.get("tests", [])
            if isinstance(test, dict)
            and ("request" in test or "requests" in test)
            and "command" not in test
        ]
        if legacy_request_tests:
            self.last_suite_report = {
                "suite": suite_path,
                "status": "blocked",
                "reason": "legacy request/sequence test DSL is not normalized to JSON-RPC cases",
                "cases": [],
                "total_cases": len(legacy_request_tests),
            }
            print("Suite requires legacy request DSL normalization")
            return False
        
        # Transport configuration is scoped to this suite.  A TLS suite must
        # never leave subsequent plaintext suites using TLS (or vice versa).
        config = data.get("config", {})
        self.use_tls = bool(config.get("tls", self.default_use_tls))
        self.tls_skip_verify = bool(
            config.get("tls_skip_verify", self.default_tls_skip_verify)
        )
        
        raw_tests = data.get("tests", [])
        try:
            tests = self._expand_macros(raw_tests)
        except Exception as error:
            self._close_suite_clients()
            print(f"Suite cannot be expanded by json_runner.py: {error}")
            self.last_suite_report = {
                "suite": suite_path,
                "status": "blocked",
                "reason": "runner incompatibility",
                "error": str(error),
                "cases": [],
                "total_cases": len(raw_tests),
            }
            return False
        
        all_passed = True
        try:
            for test in tests:
                if not self._run_test(test):
                    all_passed = False
                    if test.get("stop_on_fail") or data.get("stop_on_fail"):
                        break
        finally:
            self._close_suite_clients()
        self.last_suite_report = {
            "suite": suite_path,
            "status": "passed" if all_passed else "failed",
            "cases": self.case_results,
            "total_cases": len(self.case_results),
        }
        return all_passed

    def _run_test(self, test: Dict[str, Any]) -> bool:
        self._suite_case_index += 1
        case_id = test.get("id") or (
            f"{os.path.basename(self.active_suite or 'suite')}"
            f"::case-{self._suite_case_index:04d}"
        )
        self._current_case_id = case_id
        self._current_response = None
        self._current_failure_reason = None
        runner_error = None
        try:
            passed = self._run_test_impl(test)
        except Exception as error:
            print(f"FAIL (Runner exception: {error})")
            runner_error = f"{type(error).__name__}: {error}"
            self._current_failure_reason = runner_error
            passed = False
        expect = test.get("expect", {"status": "ok"})
        response = self._current_response
        self.case_results.append({
            "id": case_id,
            "name": test.get("name", "Unnamed"),
            "command": test.get("command"),
            "expected": expect,
            "assertions": test.get("asserts", []),
            "actual": self._redact_response(response),
            "status": "passed" if passed else "failed",
            "classification": "passed" if passed else (
                "infrastructure_error" if response is None else "failed_assertion"
            ),
        })
        if self._current_failure_reason:
            self.case_results[-1]["failure_reason"] = self._current_failure_reason
        if runner_error:
            self.case_results[-1]["runner_error"] = runner_error
        return passed

    def _run_test_impl(self, test: Dict[str, Any]) -> bool:
        name = test.get("name", "Unnamed")
        print(f"  Test: {name}...", end=" ", flush=True)
        xfail = bool(test.get("xfail", False))


        # Expand variables in two passes
        # Pass 1: Resolve local macro context (no globals)
        test = self._expand_vars_with_context(test, test, use_globals=False)
        # Pass 2: Resolve remaining globals
        test = self._expand_vars(test)

        # Database fixtures are part of the existing JSON suite contract.
        # Apply them through the real authenticated sysop protocol rather
        # than silently treating the fixture step as documentation.  This
        # keeps setup observable and ensures the suite still exercises the
        # actual server/database boundary.
        if "db_setup" in test:
            setup = test.get("db_setup") or {}
            statements = setup.get("sql", [])
            if not isinstance(statements, list):
                print("FAIL (db_setup.sql must be an array)")
                self._current_failure_reason = "db_setup.sql must be an array"
                return False
            admin = self._get_admin_client()
            for statement in statements:
                if not isinstance(statement, str) or not statement.strip():
                    print("FAIL (db_setup contains an empty SQL statement)")
                    self._current_failure_reason = "db_setup contains an empty SQL statement"
                    return False
                try:
                    admin.send_json({
                        "command": "sys.raw_sql_exec",
                        "data": {"sql": self._fixture_sql(statement)},
                        "auth": {"session": admin.session_token},
                    })
                    response = admin.recv_next_non_notice()
                except Exception as error:
                    print(f"FAIL (database fixture communication error: {error})")
                    raise
                self._current_response = response
                if not response or response.get("status") != "ok":
                    print(f"FAIL (database fixture statement rejected: {response})")
                    self._current_failure_reason = f"database fixture statement rejected: {response}"
                    return False
            print("PASS (database fixture)")
            # A fixture is setup for this case, not the case itself.  Continue
            # so the command, response assertions and save mappings on the
            # same test object are executed as authored.

        # Run independent requests from separate TCP connections at the same
        # barrier.  This is used for server/database concurrency acceptance;
        # normal JSON cases remain sequential and retain their existing shape.
        parallel_requests = test.get("parallel_requests")
        if parallel_requests is not None:
            if not isinstance(parallel_requests, list) or len(parallel_requests) < 2:
                print("FAIL (parallel_requests must contain at least two requests)")
                self._current_failure_reason = "parallel_requests must contain at least two requests"
                return False
            clients = []
            for request in parallel_requests:
                if not isinstance(request, dict) or not isinstance(request.get("command"), str):
                    print("FAIL (each parallel request requires a command)")
                    self._current_failure_reason = "invalid parallel request"
                    return False
                user = request.get("user")
                if user == "admin":
                    session = self._get_admin_client().session_token
                elif user:
                    session = self._get_user_client(user).session_token
                else:
                    session = None
                client = TWClient(
                    request.get("host", self.host),
                    int(request.get("port", self.port)),
                    timeout=int(request.get("timeout", self.timeout)),
                    use_tls=self.use_tls, tls_skip_verify=self.tls_skip_verify,
                )
                client.session_token = session
                clients.append((client, request))

            barrier = threading.Barrier(len(clients))
            responses = [None] * len(clients)
            errors = [None] * len(clients)

            def execute_parallel(index, client, request):
                try:
                    client.connect()
                    request_timeout = int(request.get("timeout", self.timeout))
                    barrier.wait(timeout=request_timeout)
                    delay_ms = int(request.get("delay_before_send_ms", 0))
                    if delay_ms > 0:
                        client.close()
                        time.sleep(delay_ms / 1000.0)
                        client.connect()
                    command = {"command": request["command"]}
                    if "data" in request:
                        command["data"] = request["data"]
                    client.send_json(command)
                    responses[index] = client.recv_next_non_notice()
                except Exception as error:
                    errors[index] = f"{type(error).__name__}: {error}"
                finally:
                    client.close()

            threads = [
                threading.Thread(target=execute_parallel,
                                 args=(index, client, request), daemon=True)
                for index, (client, request) in enumerate(clients)
            ]
            for thread in threads:
                thread.start()
            join_timeout = max(
                int(request.get("timeout", self.timeout))
                for _, request in clients
            ) + 2
            for thread in threads:
                thread.join(join_timeout)
            if any(thread.is_alive() for thread in threads):
                errors = [error or "concurrent request thread did not finish"
                          for error in errors]
            expected_each = test.get("expect_each", {"status": "ok"})
            statuses_ok = all(
                isinstance(response, dict)
                and response.get("status") == expected_each.get("status", "ok")
                and ("error_code" not in expected_each
                     or (response.get("error") or {}).get("code")
                     == expected_each["error_code"])
                for response in responses
            )
            self._current_response = {
                "id": "test-concurrent",
                "status": "ok" if statuses_ok and not any(errors) else "error",
                "type": "test.concurrent_requests",
                "data": {"responses": responses, "errors": errors},
            }
            self._capture_response(name, "parallel_requests", self._current_response)
            if not statuses_ok or any(errors):
                print("FAIL (one or more concurrent requests failed)")
                self._current_failure_reason = str(errors)
                return False
            print(f"PASS ({len(responses)} concurrent requests)")
            return True

        if "command" not in test:
            if "comment" in test:
                pass # Just a documentation step
            print("PASS (Doc)")
            return True

        if test.get("command") == "delay":
            time.sleep(test.get("data", {}).get("seconds", 0.1))
            print("DELAY")
            return True

        user = test.get("user")
        if user == "admin":
            client = self._get_admin_client()
        elif user:
            client = self._get_user_client(
                user, reauthenticate=not test.get("reuse_session_token", False)
            )
        else:
            client = TWClient(self.host, self.port, timeout=self.timeout,
                            use_tls=self.use_tls, tls_skip_verify=self.tls_skip_verify)
            client.connect()

        cmd_json = {}
        if "command" in test:
            cmd_json["command"] = test["command"]
            if "data" in test:
                cmd_json["data"] = json.loads(json.dumps(test["data"]))
                if cmd_json["command"] == "sys.raw_sql_exec":
                    sql = cmd_json["data"].get("sql")
                    if isinstance(sql, str):
                        cmd_json["data"]["sql"] = self._fixture_sql(sql)

            if client.session_token:
                cmd_json["auth"] = {"session": client.session_token}

        # Send and Receive
        resp = None
        try:
            client.send_json(cmd_json)
            resp = client.recv_next_non_notice()
        except Exception as e:
            print(f"FAIL (Communication Error: {e})")
            self._forget_client(user, client)
            raise
            return False

        if not resp:
            print("FAIL (No response)")
            self._current_failure_reason = "No protocol response received"
            return False

        self._current_response = resp

        self._capture_response(name, cmd_json.get("command"), resp)

        # system.disconnect is an intentional per-connection shutdown.  Do
        # not retain the now-closed socket in the username cache; a later
        # test for the same user must create a fresh connection and replay
        # its known login credentials.
        if cmd_json.get("command") in ("system.disconnect", "session.disconnect"):
            self._forget_client(user, client)

        if cmd_json.get("command") == "auth.login":
            data = cmd_json.get("data") or {}
            username = data.get("username")
            password = data.get("passwd")
            if isinstance(username, str) and isinstance(password, str):
                self.user_credentials[username] = (username, password)

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
        
        if "error_code" in expect and expected_status == "ok":
            expected_status = "error"

        passed_status = (actual_status == expected_status)
        if not passed_status and expected_status not in ["ok", "error", "refused"]:
            if actual_type == expected_status:
                passed_status = True
        
        if not passed_status and expected_status == "error" and actual_status == "refused":
            passed_status = True

        if not passed_status:
            msg = f"Status: {actual_status} != {expected_status}"
            if xfail:
                print(f"XFAIL ({msg})")
                return True
            print(f"FAIL ({msg})")
            print(f"  Response: {json.dumps(resp, indent=2)}")
            self._current_failure_reason = msg
            return False

        if "error_code" in expect:
            actual_err = resp.get("error") or {}
            actual_code = actual_err.get("code")
            if actual_code != expect["error_code"]:
                msg = f"Error Code: {actual_code} != {expect['error_code']}"
                if xfail:
                    print(f"XFAIL ({msg})")
                    return True
                print(f"FAIL ({msg})")
                self._current_failure_reason = msg
                return False
        
        # Save vars
        if "save" in test:
            for raw_var_name, path in test["save"].items():
                var_name = self._expand_vars_with_context(raw_var_name, test, use_globals=False)
                var_name = self._expand_vars(var_name)
                val = self._get_path(resp, path)
                if isinstance(var_name, str) and var_name.startswith("@"):
                    var_name = var_name[1:]
                var_name = str(var_name)
                self.vars[var_name] = val
                print(f"DEBUG: saved var {var_name}={val}")
                if var_name == "session" or path == "data.session_token":
                    client.session_token = val
        
        # Auto-update session on login even if not explicitly saved
        if cmd_json.get("command") == "auth.login" and actual_status == "ok":
            session = self._get_path(resp, "data.session_token")
            if session:
                client.session_token = session

        # Custom assertions
        if "asserts" in test:
            for assertion in test["asserts"]:
                path = assertion["path"]
                actual, found = self._resolve_assertion_path(resp, path)
                expected = self._expand_vars(assertion.get("value"))
                if isinstance(expected, dict) and "sql_int" in expected:
                    sql = self._expand_vars(expected["sql_int"])
                    admin = self._get_admin_client()
                    admin.send_json({"command": "sys.raw_sql_exec", "data": {"sql": self._fixture_sql(sql)}})
                    sql_resp = admin.recv_next_non_notice()
                    if sql_resp.get("status") == "ok" and sql_resp["data"]["rows"]:
                        expected = sql_resp["data"]["rows"][0][0]

                op = assertion["op"]
                passed, detail = self._evaluate_assertion(op, actual, expected, found)
                if not passed:
                    msg = assertion.get("msg") or detail
                    print(f"FAIL (Assert {op} at {path}: {msg})")
                    self._current_failure_reason = f"assertion failed at {path}: {detail}"
                    if xfail:
                        print("XFAIL")
                        return True
                    return False

        if xfail:
            print("XPASS (unexpected pass)")
        else:
            print("PASS")

        if user is None: client.close()
        return True

    def _resolve_assertion_path(self, response: Any, path: str):
        """Resolve a response or saved-variable path, preserving absent/null."""
        if path in self.vars:
            return self.vars[path], True
        for name in sorted(self.vars, key=len, reverse=True):
            if path.startswith(name + "."):
                return self._get_path_raw(self.vars[name], path[len(name) + 1:])
        return self._get_path_raw(response, path)

    def _get_path(self, obj: Any, path: str) -> Any:
        value, found = self._get_path_raw(obj, path)
        return value if found else None

    @staticmethod
    def _get_path_raw(obj: Any, path: str):
        if not path:
            return obj, True
        parts = re.split(r'\.(?![^\[]*\])', path)
        curr = obj
        for part in parts:
            if part.startswith("*[") and part.endswith("]"):
                if not isinstance(curr, list) or "=" not in part[2:-1]:
                    return None, False
                key, expected = part[2:-1].split("=", 1)
                curr = next((item for item in curr
                             if isinstance(item, dict) and str(item.get(key)) == expected),
                            _MISSING)
                if curr is _MISSING:
                    return None, False
                continue
            base, bracket, suffix = part.partition("[")
            if base:
                if isinstance(curr, dict) and base in curr:
                    curr = curr[base]
                elif isinstance(curr, list) and base.isdigit() and int(base) < len(curr):
                    curr = curr[int(base)]
                else:
                    return None, False
            rest = ("[" + suffix) if bracket else ""
            while rest:
                match = re.match(r"^\[([^\]]+)\](.*)$", rest)
                if not match:
                    return None, False
                token, rest = match.groups()
                if not isinstance(curr, list):
                    return None, False
                if token.isdigit():
                    index = int(token)
                    if index >= len(curr):
                        return None, False
                    curr = curr[index]
                elif token.startswith("?(") and token.endswith(")"):
                    condition = re.fullmatch(
                        r"\?\(@\.([A-Za-z0-9_]+)\s*==\s*'([^']*)'\)", token
                    )
                    if not condition:
                        return None, False
                    key, expected = condition.groups()
                    curr = [item for item in curr
                            if isinstance(item, dict) and str(item.get(key)) == expected]
                elif token.startswith("*") and "=" in token:
                    key, expected = token[1:].split("=", 1)
                    curr = next((item for item in curr
                                 if isinstance(item, dict) and str(item.get(key)) == expected),
                                _MISSING)
                    if curr is _MISSING:
                        return None, False
                else:
                    return None, False
        return curr, True

    @staticmethod
    def _numeric(value):
        if isinstance(value, bool):
            return None
        if isinstance(value, (int, float, Decimal)):
            return Decimal(str(value))
        if isinstance(value, str):
            try:
                return Decimal(value)
            except InvalidOperation:
                return None
        return None

    @classmethod
    def _evaluate_assertion(cls, op, actual, expected, found):
        if op == "exists":
            passed = found
        elif op == "not_exists":
            passed = not found
        elif not found:
            return False, "path is missing from the response and saved variables"
        elif op in ("==", "equals"):
            left, right = cls._numeric(actual), cls._numeric(expected)
            passed = left == right if left is not None and right is not None else actual == expected
        elif op in (">", ">=", "<", "<=", "greater_than"):
            left, right = cls._numeric(actual), cls._numeric(expected)
            if left is None or right is None:
                passed = False
            else:
                operator = ">" if op == "greater_than" else op
                passed = {">": left > right, ">=": left >= right,
                          "<": left < right, "<=": left <= right}[operator]
        elif op == "contains":
            if isinstance(actual, list):
                passed = any(expected == item or str(expected) in str(item) for item in actual)
            elif isinstance(actual, (tuple, dict, str)):
                passed = expected in actual
            else:
                passed = False
        elif op == "not_contains":
            if isinstance(actual, list):
                passed = not any(expected == item or str(expected) in str(item) for item in actual)
            elif isinstance(actual, (tuple, dict, str)):
                passed = expected not in actual
            else:
                passed = False
        elif op == "keys_eq":
            passed = isinstance(actual, dict) and set(actual) == set(expected)
        elif op == "is_string_array":
            passed = isinstance(actual, list) and all(isinstance(item, str) for item in actual)
        elif op == "is_array":
            passed = isinstance(actual, list)
        elif op == "is_empty":
            passed = isinstance(actual, (list, dict, str)) and len(actual) == 0
        elif op == "len_gt":
            count = cls._numeric(len(actual)) if isinstance(actual, (list, dict, str)) else None
            right = cls._numeric(expected)
            passed = count is not None and right is not None and count > right
        else:
            return False, f"unsupported assertion operator {op!r}"
        return passed, ("passed" if passed else
                        f"actual value {actual!r} did not satisfy {op} {expected!r}")

def main(argv=None):
    parser = argparse.ArgumentParser(description="Run one JSON RPC integration suite")
    parser.add_argument("suite", help="Suite JSON file")
    parser.add_argument(
        "--host",
        default=os.environ.get("TWCLONE_TEST_HOST", "localhost"),
        help="Game server host (default: TWCLONE_TEST_HOST or localhost)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("TWCLONE_TEST_PORT", "1234")),
        help="Game server port (default: TWCLONE_TEST_PORT or 1234)",
    )
    parser.add_argument("--timeout", type=int, default=25)
    parser.add_argument("--capture-responses", help="Append redacted RPC responses as JSONL")
    args = parser.parse_args(argv)

    runner = JsonSuiteRunner(args.host, args.port, timeout=args.timeout,
                             capture_responses=args.capture_responses)
    return 0 if runner.run_suite(args.suite) else 1


if __name__ == "__main__":
    sys.exit(main())
