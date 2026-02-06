import socket
import json
import time
import uuid
import ssl
from typing import Optional, Dict, Any, List

class TWClient:
    def __init__(self, host: str = "localhost", port: int = 1234, timeout: int = 25, 
                 use_tls: bool = False, tls_skip_verify: bool = False):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.use_tls = use_tls
        self.tls_skip_verify = tls_skip_verify
        self.sock: Optional[socket.socket] = None
        self.session_token: Optional[str] = None
        self.player_id: Optional[int] = None

    def connect(self):
        """Establishes a connection to the server."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))
        
        # Wrap with TLS if enabled
        if self.use_tls:
            context = ssl.create_default_context()
            if self.tls_skip_verify:
                context.check_hostname = False
                context.verify_mode = ssl.CERT_NONE
            self.sock = context.wrap_socket(self.sock, server_hostname=self.host)

    def close(self):
        """Closes the connection."""
        if self.sock:
            try:
                self.sock.close()
            except:
                pass
            self.sock = None

    def send_json(self, data: Dict[str, Any]):
        """Sends a JSON object as a line."""
        if not self.sock:
            raise ConnectionError("Not connected")
        
        # Inject session if available and not present
        if self.session_token and "auth" not in data:
            data["auth"] = {"session": self.session_token}
        elif self.session_token and "auth" in data and "session" not in data["auth"]:
             data["auth"]["session"] = self.session_token

        line = json.dumps(data, ensure_ascii=False)
        self.sock.sendall(line.encode("utf-8") + b"\n")

    def recv_json(self) -> Dict[str, Any]:
        """Receives a single JSON line."""
        if not self.sock:
            raise ConnectionError("Not connected")
        
        buf = bytearray()
        while True:
            chunk = self.sock.recv(1)
            if not chunk:
                break # Connection closed
            if chunk == b"\n":
                break
            buf.extend(chunk)
            # Safety limit
            if len(buf) > 1024 * 1024: 
                raise ValueError("Response too large")
        
        if not buf:
            return {}
            
        return json.loads(buf.decode("utf-8", errors="replace"))

    def recv_next_non_notice(self, timeout: int = 10) -> Dict[str, Any]:
        """Reads responses until a non-notice type is received."""
        start = time.time()
        while time.time() - start < timeout:
            resp = self.recv_json()
            if not resp:
                raise ConnectionError("Connection closed or empty response")
            
            if resp.get("type") == "system.notice":
                continue
            return resp
        raise TimeoutError("Timed out waiting for non-notice response")

    def register(self, username: str, password: str, fail_if_exists: bool = False) -> bool:
        """Registers a user. Returns True if successful or already exists (unless fail_if_exists)."""
        cmd = {
            "command": "auth.register",
            "data": {"username": username, "passwd": password}
        }
        self.send_json(cmd)
        resp = self.recv_next_non_notice()
        
        if resp.get("status") == "ok":
            return True
        
        err_code = resp.get("error", {}).get("code")
        if err_code == 1105: # ERR_NAME_TAKEN
            return not fail_if_exists
            
        return False

    def login(self, username: str, password: str) -> bool:
        """Logs in and stores the session token."""
        cmd = {
            "command": "auth.login",
            "data": {"username": username, "passwd": password}
        }
        self.send_json(cmd)
        resp = self.recv_next_non_notice()
        
        if resp.get("status") == "ok":
            self.session_token = resp.get("data", {}).get("session_token")
            self.player_id = resp.get("data", {}).get("player_id")
            return True
        return False

    def raw_sql(self, sql: str) -> bool:
        """Executes raw SQL. Requires Admin/Sysop session."""
        cmd = {
            "command": "sys.raw_sql_exec",
            "data": {"sql": sql}
        }
        self.send_json(cmd)
        resp = self.recv_next_non_notice()
        return resp.get("status") == "ok"
