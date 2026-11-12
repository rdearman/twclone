#!/usr/bin/env python3
"""Bounded adversarial transport acceptance against the real QA server.

Uses the existing disposable PostgreSQL/server instance runner. The server is
TLS-only and loopback-bound; no listener is published outside localhost.
"""

import json
import os
import socket
import ssl
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests.v2"))
import run_regression_instance as runner


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def tls_client(context, host, port, timeout=3):
    raw = socket.create_connection((host, port), timeout=timeout)
    raw.settimeout(timeout)
    return context.wrap_socket(raw, server_hostname="localhost")


def recv_line(sock):
    data = bytearray()
    while b"\n" not in data:
        chunk = sock.recv(1)
        if not chunk:
            break
        data.extend(chunk)
    if not data:
        return None
    line, _, _ = data.partition(b"\n")
    return json.loads(line)


def request(sock, request_id):
    body = {"id": request_id, "command": "system.hello",
            "data": {"client_version": "security-acceptance"}}
    sock.sendall(json.dumps(body, separators=(",", ":")).encode() + b"\n")
    return recv_line(sock)


def run_probes(host, port, context):
    completed = []

    # A stalled TLS client must not hold the accept loop: another TLS
    # handshake must complete before the stalled client's deadline.
    stalled = socket.create_connection((host, port), timeout=2)
    started = time.monotonic()
    with tls_client(context, host, port) as live:
        handshake_time = time.monotonic() - started
        assert handshake_time < 0.75, f"TLS accept loop stalled for {handshake_time:.3f}s"
        assert request(live, "tls-responsive") ["status"] == "ok"
    completed.append("slow TLS handshake does not block accept loop")
    stalled.settimeout(3)
    assert stalled.recv(1) == b"", "stalled handshake was not closed at deadline"
    stalled.close()

    # Absolute deadline applies to a partially delivered request frame.
    with tls_client(context, host, port) as slow:
        slow.sendall(b'{"command":"system.hello"')
        response = recv_line(slow)
        assert response and response.get("status") == "error"
        assert response.get("error", {}).get("code") == 505
    completed.append("slow partial request is timed out")

    # Oversized frames receive a protocol error and are closed.
    with tls_client(context, host, port) as large:
        large.sendall(b"x" * 4097)
        response = recv_line(large)
        assert response and response.get("status") == "error"
        assert response.get("error", {}).get("code") == 405
    completed.append("oversized request is bounded and rejected")

    # Malformed JSON is rejected without killing the connection/server.
    with tls_client(context, host, port) as malformed:
        malformed.sendall(b"{bad}\n")
        response = recv_line(malformed)
        assert response and response.get("status") == "error"
        assert request(malformed, "after-malformed") ["status"] == "ok"
    completed.append("malformed JSON rejected; connection remains usable")

    # Real per-connection rate rejection (four requests/minute in this run).
    with tls_client(context, host, port) as limited:
        for index in range(4):
            assert request(limited, f"within-limit-{index}")["status"] == "ok"
        response = request(limited, "over-limit")
        assert response and response.get("status") == "error"
        assert response.get("error", {}).get("code") == 406
    completed.append("request-rate limit actively rejects excess request")

    # Total concurrent connection cap is set to four for this test process.
    held = [tls_client(context, host, port) for _ in range(4)]
    refused = False
    try:
        try:
            extra = tls_client(context, host, port, timeout=1)
            extra.close()
        except (OSError, ssl.SSLError, socket.timeout):
            refused = True
        assert refused, "connection above configured cap completed TLS"
    finally:
        for sock in held:
            sock.close()
    completed.append("connection limit refuses excess concurrent client")
    return completed


def main():
    with tempfile.TemporaryDirectory(prefix="twclone-security-qa-") as temp:
        cert = Path(temp) / "test.crt"
        key = Path(temp) / "test.key"
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", str(key), "-out", str(cert), "-days", "1",
            "-subj", "/CN=localhost",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

        os.environ.update({
            "TWCLONE_LISTEN_ADDRESS": "127.0.0.1",
            "TWCLONE_MAX_CONNECTIONS": "4",
            "TWCLONE_MAX_REQUEST_BYTES": "4096",
            "TWCLONE_REQUEST_TIMEOUT_MS": "1500",
            "TWCLONE_REQUESTS_PER_MINUTE": "4",
            "TWCLONE_TLS_ENABLED": "1",
            "TWCLONE_TLS_REQUIRED": "1",
            "TWCLONE_TLS_CERT_PATH": str(cert),
            "TWCLONE_TLS_KEY_PATH": str(key),
        })
        host, port = "127.0.0.1", free_port()
        s2s_port = free_port()
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        original_wait = runner.wait_for_port
        original_run = subprocess.run
        results = []

        def wait_and_test(server_host, server_port, process, timeout=30):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(f"server exited with status {process.returncode}")
                try:
                    with tls_client(context, server_host, server_port, timeout=1):
                        break
                except OSError:
                    time.sleep(0.1)
            else:
                raise TimeoutError("TLS QA server did not become ready")
            results.extend(run_probes(server_host, server_port, context))

            # The probe set above is the acceptance suite. Avoid running the
            # ordinary plaintext JSON runner against a TLS-only listener.
            def skip_plain_runner(command, *args, **kwargs):
                if any("run_suites_all.py" in str(arg) for arg in command):
                    return subprocess.CompletedProcess(command, 0)
                return original_run(command, *args, **kwargs)
            runner.subprocess.run = skip_plain_runner

        runner.wait_for_port = wait_and_test
        try:
            rc = runner.main([
                "--pg-host", "127.0.0.1", "--pg-port", "55438",
                "--pg-user", os.environ.get("QA_PGUSER", "twclone_qa_runner"),
                "--host", host, "--port", str(port), "--s2s-port", str(s2s_port),
                "--skip-rig-ports", "--keep-artifacts",
                "--report", os.environ.get(
                    "TWCLONE_SECURITY_REPORT",
                    str(ROOT / "tests.v2" / "security_adversarial_report.json")),
            ])
        finally:
            runner.wait_for_port = original_wait
            runner.subprocess.run = original_run
        if rc != 0:
            raise SystemExit(rc)
        print(f"ADVERSARIAL QA PASS: {len(results)}/{len(results)}")
        for result in results:
            print(f"PASS: {result}")


if __name__ == "__main__":
    main()
