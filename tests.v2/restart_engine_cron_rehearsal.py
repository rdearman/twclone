#!/usr/bin/env python3
"""Disposable server/engine restart and cron integration rehearsal.

Requires a migrated and seeded disposable PostgreSQL database, `bin/server`,
and `bin/bigbang.json`-shaped config. The configured database must be local
and named `twclone_qa_*`; the test refuses other databases.
"""

import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
STARTUP_TIMEOUT = 30


def require_disposable_url():
    url = os.environ.get("QA_DATABASE_URL", "")
    parsed = urlparse(url)
    database = parsed.path.lstrip("/")
    if parsed.scheme not in ("postgres", "postgresql"):
        raise SystemExit("QA_DATABASE_URL must be a PostgreSQL URL")
    if parsed.hostname not in ("localhost", "127.0.0.1", "::1"):
        raise SystemExit("refusing non-local database host")
    if not database.startswith("twclone_qa_"):
        raise SystemExit("refusing database not named twclone_qa_*")
    return url


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_handshake(process, log_path, client_port):
    deadline = time.monotonic() + STARTUP_TIMEOUT
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("server exited during startup; see " + str(log_path))
        text = log_path.read_text(errors="replace") if log_path.exists() else ""
        listener = "Listening on 0.0.0.0:%d" % client_port
        if ("accepted hello" in text and "ack send rc=0" in text
                and listener in text):
            return text
        time.sleep(0.2)
    raise TimeoutError("S2S hello/ack not observed; see " + str(log_path))


def stop_server(process):
    if process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=5)


def main():
    database_url = require_disposable_url()
    server = Path(os.environ.get("QA_SERVER_BIN", ROOT / "bin/server")).resolve()
    config_template = Path(os.environ.get("QA_CONFIG_TEMPLATE", ROOT / "bin/bigbang.json")).resolve()
    if not server.is_file() or not config_template.is_file():
        raise SystemExit("QA_SERVER_BIN and QA_CONFIG_TEMPLATE must name existing files")

    evidence_root = Path(os.environ.get(
        "QA_EVIDENCE_DIR", tempfile.mkdtemp(prefix="twclone-restart-qa-")))
    evidence_root.mkdir(parents=True, exist_ok=True)
    run_dir = evidence_root / "server"
    run_dir.mkdir(exist_ok=True)
    config = json.loads(config_template.read_text())
    config["app"] = database_url
    config["server_port"] = free_port()
    config["s2s_port"] = free_port()
    (run_dir / "bigbang.json").write_text(json.dumps(config, indent=2))
    client_port = int(config["server_port"])
    if not shutil.which("psql"):
        raise SystemExit("psql is required to set disposable listener ports")
    port_sql = (
        "INSERT INTO config (key,value,type) VALUES "
        "('server_port','%d','int'),('s2s_port','%d','int') "
        "ON CONFLICT (key) DO UPDATE SET value=EXCLUDED.value,type=EXCLUDED.type"
        % (client_port, int(config["s2s_port"]))
    )
    subprocess.run(["psql", database_url, "-v", "ON_ERROR_STOP=1", "-c", port_sql],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                   text=True)
    subprocess.run(
        ["psql", database_url, "-v", "ON_ERROR_STOP=1", "-c",
         "INSERT INTO sectors (sector_id,name) VALUES (555,'QA restart hazard sector') "
         "ON CONFLICT (sector_id) DO NOTHING"],
        check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    suite = ROOT / "tests.v2/suite_planet_fighter_production_e2e.py"
    boot_logs = []

    for boot_number in (1, 2):
        console_path = run_dir / ("boot%d.console.log" % boot_number)
        log_path = run_dir / "twclone.log"
        if log_path.exists():
            log_path.unlink()
        with console_path.open("w") as log_file:
            process = subprocess.Popen(
                [str(server)], cwd=run_dir, stdout=log_file,
                stderr=subprocess.STDOUT, start_new_session=True)
            try:
                boot_logs.append(wait_for_handshake(process, log_path, client_port))
                if boot_number == 1:
                    stop_server(process)
                    print("SERVER_RESTART: first server/engine stopped cleanly")
                else:
                    env = os.environ.copy()
                    env.update(HOST="127.0.0.1", PORT=str(client_port))
                    result = subprocess.run(
                        [sys.executable, str(suite)], cwd=ROOT, env=env,
                        text=True, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, timeout=120)
                    (evidence_root / "post_restart_cron_test.log").write_text(result.stdout)
                    if result.returncode != 0:
                        raise RuntimeError("post-restart planet_growth test failed:\n" + result.stdout)
                    print("POST_RESTART_CRON: " + result.stdout.strip().splitlines()[-1])
                    hazard_result = subprocess.run(
                        [sys.executable, str(ROOT / "tests.v2/run_suites.py"),
                         "--host", "127.0.0.1", "--port", str(client_port),
                         "--suite", str(ROOT / "tests.v2/suite_sector_environmental_hazards.json")],
                        cwd=ROOT, text=True, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, timeout=120)
                    (evidence_root / "hazard_contract_test.log").write_text(
                        hazard_result.stdout)
                    if hazard_result.returncode != 0:
                        raise RuntimeError("post-restart hazard response test failed:\n"
                                           + hazard_result.stdout)
                    print("HAZARD_RESPONSE: " + hazard_result.stdout.strip().splitlines()[-1])
            finally:
                stop_server(process)

    for boot_number, text in enumerate(boot_logs, 1):
        (evidence_root / ("boot%d_s2s_evidence.log" % boot_number)).write_text(
            "\n".join(line for line in text.splitlines()
                       if "accepted hello" in line or "ack send rc=" in line) + "\n")
    print("S2S_EVIDENCE:", evidence_root)
    print("PASS: server restarted, engine completed S2S hello/ack on both boots, "
          "and post-restart cron and hazard response integration checks passed")


if __name__ == "__main__":
    main()
