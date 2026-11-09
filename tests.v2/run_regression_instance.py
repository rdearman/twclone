#!/usr/bin/env python3
"""Run the complete JSON regression suite against a disposable real server.

This is orchestration only.  BigBang, PostgreSQL, the built server binary and
the existing JSON runner remain the implementation under test.
"""

import argparse
import json
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests.v2"))
from rig_db import QA_DEFAULT_PGPORT, validate_qa_target


def conninfo_value(conninfo, key):
    match = re.search(rf"(?:^|\s){re.escape(key)}=(?:'([^']*)'|([^\s]+))", conninfo)
    return (match.group(1) or match.group(2)) if match else None


def build_qa_conninfos(dbname, host, port, user, password=None, admin_conn=None):
    """Build DB connections that are provably scoped to the disposable QA host."""
    validate_qa_target(dbname, host, port)
    auth = f" user={user} host={host} port={port}"
    if password:
        escaped = password.replace("\\", "\\\\").replace("'", "\\'")
        auth += f" password='{escaped}'"
    app_conn = f"dbname=%DB%{auth}"

    if admin_conn:
        admin_host = conninfo_value(admin_conn, "host") or host
        admin_port = conninfo_value(admin_conn, "port") or str(port)
        admin_db = conninfo_value(admin_conn, "dbname")
        if admin_host not in ("localhost", "127.0.0.1", "::1"):
            raise ValueError("explicit QA admin connection must target loopback")
        if str(admin_port) != str(port) or admin_db != "postgres":
            raise ValueError("explicit QA admin connection must target postgres on the configured QA port")
        return admin_conn, app_conn

    return f"dbname=postgres{auth}", app_conn


def wait_for_port(host, port, process, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"server exited with status {process.returncode}")
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.25)
    raise TimeoutError(f"server did not listen on {host}:{port}")


def terminate_process(process):
    if process is None or process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=15)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=5)

def run(command, *, cwd=None, env=None, log=None):
    print("REGRESSION EXEC:", " ".join(str(part) for part in command), flush=True)
    return subprocess.run(command, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                          text=True, check=False)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", default="tests.v2/regression_report.json")
    parser.add_argument("--capture-responses")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=12483)
    parser.add_argument("--s2s-port", type=int, default=43218)
    parser.add_argument("--pg-host", default=os.environ.get("QA_PGHOST", "127.0.0.1"))
    parser.add_argument("--pg-port", type=int, default=int(os.environ.get("QA_PGPORT", str(QA_DEFAULT_PGPORT))))
    parser.add_argument("--pg-user", default=os.environ.get("QA_PGUSER"))
    parser.add_argument("--admin-conn", default=os.environ.get("TWCLONE_TEST_PG_ADMIN"))
    parser.add_argument("--template", default=str(ROOT / "bin" / "bigbang.json"))
    parser.add_argument("--sectors", type=int, default=int(os.environ.get("TWCLONE_REGRESSION_SECTORS", "500")))
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args(argv)

    template = json.load(open(args.template, encoding="utf-8"))
    app_conn = template.get("app")
    if not app_conn:
        parser.error("a PostgreSQL app connection template is required")

    pg_user = args.pg_user or conninfo_value(app_conn, "user")
    if not pg_user:
        parser.error("--pg-user or an app connection user is required")
    password = os.environ.get("QA_PGPASSWORD") or conninfo_value(
        args.admin_conn or "", "password"
    )

    dbname = f"twclone_v3qa_regression_{os.getpid()}_{int(time.time())}"
    if not dbname.startswith("twclone_v3qa_"):
        raise RuntimeError("internal database safety guard failed")

    try:
        admin_conn, qa_app_conn = build_qa_conninfos(
            dbname, args.pg_host, args.pg_port, pg_user, password,
            args.admin_conn,
        )
    except ValueError as error:
        parser.error(str(error))

    artifact_dir = Path(tempfile.mkdtemp(prefix="twclone-v3-regression-"))
    config_path = artifact_dir / "bigbang.json"
    server_log = artifact_dir / "server.log"
    report_path = Path(args.report).resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    capture_path = Path(args.capture_responses).resolve() if args.capture_responses else artifact_dir / "responses.jsonl"

    config = dict(template)
    # When an explicit QA connection is supplied, keep BigBang and the server
    # on that disposable PostgreSQL endpoint.  The checked-in template points
    # at the production-style host; inheriting it would defeat the isolation
    # contract of regression-full.
    config["admin"] = admin_conn
    config["app"] = qa_app_conn
    config.update({
        "db": dbname,
        "sql_dir": str(ROOT / "sql" / "pg"),
        "sectors": args.sectors,
        "server_port": args.port,
        "s2s_port": args.s2s_port,
        "include_v3_migrations": True,
    })
    config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")

    env = os.environ.copy()
    if password:
        env["PGPASSWORD"] = password
    else:
        # Never inherit a password from the production-style BigBang template.
        env.pop("PGPASSWORD", None)

    server = None
    result_code = 2
    try:
        print(f"REGRESSION DATABASE: {dbname}")
        print(f"REGRESSION ARTIFACTS: {artifact_dir}")

        bigbang = run([str(ROOT / "bin" / "bigbang")], cwd=artifact_dir, env=env)
        if bigbang.returncode != 0:
            raise RuntimeError(f"BigBang failed with status {bigbang.returncode}; see {artifact_dir}")

        rig = run([
            sys.executable, str(ROOT / "tests.v2" / "rig_db.py"),
            "--config", str(ROOT / "tests.v2" / "test_rig.json"),
            "--reset", "--dbname", dbname, "--host", args.pg_host,
            "--port", str(args.pg_port), "--user", pg_user,
        ], cwd=ROOT, env=env)
        if rig.returncode != 0:
            raise RuntimeError(f"fixture rigging failed with status {rig.returncode}")

        log = open(server_log, "w", encoding="utf-8")
        server = subprocess.Popen(
            [str(ROOT / "bin" / "server")], cwd=artifact_dir, env=env,
            stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
        )
        wait_for_port(args.host, args.port, server)

        runner = [
            sys.executable, str(ROOT / "tests.v2" / "run_suites_all.py"),
            "--host", args.host, "--port", str(args.port),
            "--report", str(report_path),
            "--capture-responses", str(capture_path),
        ]
        result_code = subprocess.run(runner, cwd=ROOT, env=env, check=False).returncode
    except Exception as error:
        print(f"REGRESSION BLOCKED: {error}", file=sys.stderr)
        blocked_path = report_path.with_name(report_path.name + ".tmp")
        blocked_path.write_text(json.dumps({
            "status": "blocked",
            "database": dbname,
            "artifacts": str(artifact_dir),
            "error": str(error),
        }, indent=2) + "\n", encoding="utf-8")
        result_code = 2
    finally:
        terminate_process(server)
        try:
            subprocess.run(["psql", admin_conn, "-v", "ON_ERROR_STOP=1", "-c",
                            f'DROP DATABASE IF EXISTS "{dbname}"'],
                           env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                           text=True, check=False)
        except OSError as error:
            print(f"WARNING: could not remove disposable database: {error}", file=sys.stderr)
        if not args.keep_artifacts:
            # Keep the report and server log when a run fails; remove only
            # transient configuration and response files after success.
            if result_code == 0:
                for path in (config_path, capture_path):
                    try:
                        path.unlink()
                    except FileNotFoundError:
                        pass
    return result_code


if __name__ == "__main__":
    sys.exit(main())
