#!/usr/bin/env python3
"""Run the complete JSON regression suite against a disposable real server.

This is orchestration only.  BigBang, PostgreSQL, the built server binary and
the existing JSON runner remain the implementation under test.
"""

import argparse
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests.v2"))
from rig_db import (QA_DEFAULT_PGPORT, QA_DEFAULT_PGUSER,
                    validate_qa_target)


def conninfo_value(conninfo, key):
    match = re.search(rf"(?:^|\s){re.escape(key)}=(?:'([^']*)'|([^\s]+))", conninfo)
    return (match.group(1) or match.group(2)) if match else None


def build_qa_conninfos(dbname, host, port, user, password=None, admin_conn=None):
    """Build DB connections that are provably scoped to the disposable QA host."""
    validate_qa_target(dbname, host, port)
    auth = f" user={user} host={host} port={port} connect_timeout=5"
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


def direct_psql_path():
    """Prefer the versioned PostgreSQL client over Debian's cluster wrapper."""
    pg_config = shutil.which("pg_config")
    if pg_config:
        try:
            result = subprocess.run([pg_config, "--bindir"], capture_output=True,
                                    text=True, timeout=3, check=False)
            candidate = Path(result.stdout.strip()) / "psql"
            if result.returncode == 0 and candidate.is_file():
                return str(candidate)
        except (OSError, subprocess.TimeoutExpired):
            pass
    return shutil.which("psql") or "psql"


def verify_qa_cluster(host, port, admin_conn, env, psql_path=None):
    """Read-only check against the repository-documented QA cluster identity."""
    try:
        with socket.create_connection((host, int(port)), timeout=3):
            pass
    except PermissionError as error:
        raise RuntimeError(
            f"QA TCP access blocked by execution sandbox (socket errno {error.errno}): {error}"
        ) from error
    except OSError as error:
        if getattr(error, "errno", None) == 111:
            reason = "QA PostgreSQL listener is not accepting connections"
        else:
            reason = f"QA TCP connection failed (errno {getattr(error, 'errno', None)})"
        raise RuntimeError(f"{reason}: {error}") from error

    command = [psql_path or direct_psql_path(), "-X", "-w", "-A", "-t",
               "-q", "-v", "ON_ERROR_STOP=1", admin_conn, "-c",
               "SELECT json_build_array(current_database(), current_setting('port'), "
               "host(inet_server_addr()), current_user, EXISTS (SELECT 1 FROM pg_database "
               "WHERE left(datname, 11) = 'twclone_qa_' "
               "OR left(datname, 13) = 'twclone_v3qa_'))::text;"]
    try:
        result = subprocess.run(command, env=env, capture_output=True,
                                text=True, timeout=8, check=False)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("QA PostgreSQL identity query timed out after 8 seconds") from error
    except OSError as error:
        raise RuntimeError(f"Could not launch direct PostgreSQL client: {error}") from error
    if result.returncode != 0:
        diagnostic = (result.stderr or result.stdout).strip()
        lowered = diagnostic.lower()
        if "password authentication failed" in lowered or "no password supplied" in lowered:
            category = "QA PostgreSQL authentication failure"
        elif "permission denied" in lowered:
            category = "QA PostgreSQL authorization failure"
        elif "operation not permitted" in lowered:
            category = "QA PostgreSQL socket access blocked by execution environment"
        elif "connection refused" in lowered or "no response" in lowered:
            category = "QA PostgreSQL listener unavailable"
        else:
            category = "QA PostgreSQL identity query failed"
        raise RuntimeError(f"{category} (psql exit {result.returncode}): {diagnostic or 'no client diagnostic'}")
    try:
        identity = json.loads(result.stdout.strip())
        database, server_port, server_address, user, marker_present = identity
    except (ValueError, TypeError):
        raise RuntimeError(
            "QA PostgreSQL identity query returned no parseable identity; "
            f"psql stdout={result.stdout!r}, stderr={result.stderr!r}"
        )
    if (database != "postgres" or int(server_port) != int(port)
            or server_address not in ("127.0.0.1", "::1")
            or user != QA_DEFAULT_PGUSER or not marker_present):
        raise RuntimeError(
            "Refusing database operations: endpoint identity mismatch "
            f"(database={database!r}, port={server_port!r}, "
            f"server_address={server_address!r}, role={user!r}, "
            f"documented_QA_marker_present={marker_present!r})"
        )
    return identity


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
    parser.add_argument("--suite", action="append",
                        help="Run only this JSON suite; repeatable")
    parser.add_argument("--skip-rig-ports", action="store_true",
                        help="Keep BigBang-generated ports instead of applying fixture port rows")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=12483)
    parser.add_argument("--s2s-port", type=int, default=43218)
    parser.add_argument("--pg-host", default=os.environ.get("QA_PGHOST", "127.0.0.1"))
    parser.add_argument("--pg-port", type=int, default=int(os.environ.get("QA_PGPORT", str(QA_DEFAULT_PGPORT))))
    parser.add_argument("--pg-user", default=os.environ.get("QA_PGUSER", QA_DEFAULT_PGUSER))
    parser.add_argument("--admin-conn", default=os.environ.get("TWCLONE_TEST_PG_ADMIN"))
    parser.add_argument("--template", default=str(ROOT / "bin" / "bigbang.json"))
    parser.add_argument("--sectors", type=int, default=int(os.environ.get("TWCLONE_REGRESSION_SECTORS", "500")))
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args(argv)

    template = json.load(open(args.template, encoding="utf-8"))
    app_conn = template.get("app")
    if not app_conn:
        parser.error("a PostgreSQL app connection template is required")

    # Never inherit a role from the generic BigBang template: that template
    # may name a non-QA account whose credentials are not in the QA .pgpass.
    pg_user = args.pg_user
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
    qa_identity_verified = False
    psql_path = direct_psql_path()
    try:
        print(f"REGRESSION DATABASE: {dbname}")
        print(f"REGRESSION ARTIFACTS: {artifact_dir}")

        verify_qa_cluster(args.pg_host, args.pg_port, admin_conn, env, psql_path)
        qa_identity_verified = True
        # rig_db invokes `psql` by name. Prefer the same versioned client used
        # for identity checks and cleanup, avoiding Debian's cluster wrapper.
        env["PATH"] = str(Path(psql_path).parent) + os.pathsep + env.get("PATH", "")
        print(f"QA CLUSTER VERIFIED: loopback port {args.pg_port}, dedicated QA role, "
              "QA database namespace present")

        bigbang_log = artifact_dir / "bigbang.log"
        with open(bigbang_log, "w", encoding="utf-8") as output:
            bigbang = run([str(ROOT / "bin" / "bigbang")], cwd=artifact_dir,
                          env=env, log=output)
        if bigbang.returncode != 0:
            tail = bigbang_log.read_text(encoding="utf-8", errors="replace").splitlines()[-40:]
            diagnostics = "\n".join(tail) or "(no output)"
            raise RuntimeError(
                f"BigBang failed with status {bigbang.returncode}; "
                f"diagnostics:\n{diagnostics}; see {artifact_dir}"
            )

        rig = run([
            sys.executable, str(ROOT / "tests.v2" / "rig_db.py"),
            "--config", str(ROOT / "tests.v2" / "test_rig.json"),
            "--reset", "--dbname", dbname, "--host", args.pg_host,
            "--port", str(args.pg_port), "--user", pg_user,
            *( ["--skip-ports"] if args.skip_rig_ports else [] ),
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
        for suite in args.suite or []:
            suite_path = Path(suite)
            if not suite_path.is_absolute():
                suite_path = ROOT / "tests.v2" / suite_path
            runner.extend(("--suite", str(suite_path.resolve())))
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
            if qa_identity_verified:
                cleanup = subprocess.run([psql_path, "-X", "-w", admin_conn,
                                "-v", "ON_ERROR_STOP=1", "-c",
                                f'DROP DATABASE IF EXISTS "{dbname}"'],
                               env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                               text=True, timeout=10, check=False)
                if cleanup.returncode != 0:
                    print("WARNING: disposable QA database cleanup failed "
                          f"(psql exit {cleanup.returncode}): "
                          f"{cleanup.stderr.strip() or 'no client diagnostic'}", file=sys.stderr)
            else:
                print("Skipping database cleanup because QA cluster identity was not verified",
                      file=sys.stderr)
        except (OSError, subprocess.TimeoutExpired) as error:
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
