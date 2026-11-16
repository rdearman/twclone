#!/usr/bin/env python3
"""Run the lawful port-robbery acceptance across a disposable server restart.

The selected Docker container must be an isolated QA server named with the
``twclone-qa-robbery-parity-`` prefix.  Database credentials are inherited
through PGPASSWORD/PGPASSFILE and are never printed.
"""

from __future__ import annotations

import argparse
import socket
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
TESTS = Path(__file__).resolve().parent
CONTAINER_PREFIX = "twclone-qa-robbery-parity-"


def wait_for_listener(host: str, port: int, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1):
                return
        except OSError as error:
            last_error = error
            time.sleep(0.25)
    raise RuntimeError(
        f"V3 listener {host}:{port} did not return after restart: {last_error}"
    )


def run_suite(args: argparse.Namespace, suite: str, report_name: str,
              transcript_name: str, rig: bool) -> None:
    command = [
        sys.executable,
        str(TESTS / "run_suites_all.py"),
        "--host", args.host,
        "--port", str(args.port),
        "--suite", str(TESTS / suite),
        "--capture-responses", str(args.evidence_dir / transcript_name),
        "--report", str(args.evidence_dir / report_name),
    ]
    if rig:
        command.extend([
            "--rig",
            "--dbname", args.dbname,
            "--pg-host", args.pg_host,
            "--pg-port", str(args.pg_port),
            "--pg-user", args.pg_user,
        ])
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server-container", required=True)
    parser.add_argument("--dbname", required=True)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--pg-host", default="127.0.0.1")
    parser.add_argument("--pg-port", type=int, required=True)
    parser.add_argument("--pg-user", default="postgres")
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--restart-timeout", type=float, default=30)
    args = parser.parse_args()

    if not args.server_container.startswith(CONTAINER_PREFIX):
        parser.error(
            f"--server-container must start with {CONTAINER_PREFIX!r}; "
            "this runner restarts that container"
        )
    if not args.dbname.startswith(("twclone_qa_", "twclone_v3qa_")):
        parser.error("--dbname must be an isolated QA database name")
    if args.port <= 0 or args.port > 65535 or args.pg_port <= 0 or args.pg_port > 65535:
        parser.error("ports must be between 1 and 65535")

    args.evidence_dir.mkdir(parents=True, exist_ok=True)
    run_suite(
        args,
        "acceptance/suite_police_phase_b_restart_seed.json",
        "police-robbery-before-restart.json",
        "police-robbery-before-restart.jsonl",
        rig=True,
    )

    subprocess.run(["docker", "restart", args.server_container], check=True,
                   stdout=subprocess.DEVNULL)
    wait_for_listener(args.host, args.port, args.restart_timeout)

    run_suite(
        args,
        "acceptance/suite_police_phase_b_restart_readback.json",
        "police-robbery-after-restart.json",
        "police-robbery-after-restart.jsonl",
        rig=False,
    )
    print(f"Restart acceptance evidence: {args.evidence_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"BLOCKED/FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
