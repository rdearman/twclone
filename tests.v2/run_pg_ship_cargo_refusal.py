"""Run ship cargo transfer regression against a disposable BigBang PostgreSQL world.

Set CARGO_REFUSAL_QA_DATABASE (a new twclone_cargo_refusal_qa_* name),
CARGO_REFUSAL_QA_ADMIN_DSN, and CARGO_REFUSAL_QA_APP_TEMPLATE (containing %DB%).
BIGBANG_BIN and SERVER_BIN may point to separately built executables.
CARGO_REFUSAL_QA_SQL_DIR may select the matching isolated SQL tree.
"""
import json
import os
from pathlib import Path
import secrets
import signal
import socket
import subprocess
import tempfile
import time

import psycopg2
from psycopg2 import sql
from test_pg_bigbang_economy_profit import free_port
from test_pg_ship_cargo_transfer import check_cargo_transfer

ROOT = Path(__file__).resolve().parents[1]

def main() -> None:
    db_name = os.environ.get("CARGO_REFUSAL_QA_DATABASE", "")
    admin_dsn = os.environ.get("CARGO_REFUSAL_QA_ADMIN_DSN")
    app_template = os.environ.get("CARGO_REFUSAL_QA_APP_TEMPLATE")
    if not db_name.startswith("twclone_cargo_refusal_qa_") or db_name == "twclone":
        raise SystemExit("CARGO_REFUSAL_QA_DATABASE must be a unique twclone_cargo_refusal_qa_* name")
    if not admin_dsn or not app_template or "%DB%" not in app_template:
        raise SystemExit("Set CARGO_REFUSAL_QA_ADMIN_DSN and CARGO_REFUSAL_QA_APP_TEMPLATE (with %DB%).")

    bigbang = Path(os.environ.get("BIGBANG_BIN", ROOT / "bin/bigbang")).resolve()
    server_bin = Path(os.environ.get("SERVER_BIN", ROOT / "bin/server")).resolve()
    if not bigbang.is_file() or not server_bin.is_file():
        raise SystemExit("Built isolated bin/bigbang and bin/server are required")

    with psycopg2.connect(admin_dsn) as admin:
        with admin.cursor() as cur:
            cur.execute("SELECT 1 FROM pg_database WHERE datname=%s", (db_name,))
            if cur.fetchone():
                raise SystemExit("Refusing to use an existing database; choose a fresh disposable name")

    app_dsn = app_template.replace("%DB%", db_name)
    client_port, s2s_port = free_port(), free_port()
    account = "inspect_qa_" + secrets.token_hex(5)
    password = secrets.token_urlsafe(28)
    server = None
    server_log = None
    conn = None
    created = False
    try:
        with tempfile.TemporaryDirectory(prefix="twclone-cargo-refusal-qa-") as tmp:
            temp = Path(tmp)
            config = json.loads((ROOT / "bin/bigbang.json").read_text(encoding="utf-8"))
            config.update({
                "admin": admin_dsn,
                "db": db_name,
                "app": app_dsn,
                "sql_dir": str(Path(os.environ.get("CARGO_REFUSAL_QA_SQL_DIR", ROOT / "sql/pg")).resolve()),
                "sectors": int(os.environ.get("CARGO_REFUSAL_QA_SECTORS", "2500")),
                "server_port": client_port,
                "s2s_port": s2s_port,
            })
            bb_dir = temp / "bigbang"
            bb_dir.mkdir()
            (bb_dir / "bigbang.json").write_text(json.dumps(config), encoding="utf-8")
            os.chmod(bb_dir / "bigbang.json", 0o600)
            created = True
            generated = subprocess.run(
                [str(bigbang)], cwd=bb_dir, input="y\n", text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900,
            )
            if generated.returncode:
                raise RuntimeError("BigBang failed on disposable database:\n" + generated.stdout[-5000:])

            conn = psycopg2.connect(app_dsn)
            server_dir = temp / "server"
            server_dir.mkdir()
            (server_dir / "bigbang.json").write_text(json.dumps(config), encoding="utf-8")
            os.chmod(server_dir / "bigbang.json", 0o600)
            server_log = (server_dir / "server.log").open("w", encoding="utf-8")
            server = subprocess.Popen(
                [str(server_bin)], cwd=server_dir, stdout=server_log,
                stderr=subprocess.STDOUT, start_new_session=True,
            )
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                if server.poll() is not None:
                    raise RuntimeError("isolated server exited during startup")
                try:
                    with socket.create_connection(("127.0.0.1", client_port), timeout=0.2):
                        break
                except OSError:
                    time.sleep(0.2)
            else:
                raise TimeoutError("isolated server did not start")

            from twclient import TWClient
            registration = TWClient("127.0.0.1", client_port, timeout=10)
            registration.connect()
            if not registration.register(account, password, fail_if_exists=True):
                raise AssertionError("disposable inspection player registration failed")
            registration.close()

            check_cargo_transfer(conn, "127.0.0.1", client_port, account, password)

    finally:
        if server is not None and server.poll() is None:
            os.killpg(server.pid, signal.SIGTERM)
            try:
                server.wait(timeout=12)
            except subprocess.TimeoutExpired:
                os.killpg(server.pid, signal.SIGKILL)
                server.wait(timeout=5)
        if server_log is not None:
            server_log.close()
        if conn is not None:
            conn.close()
        if created:
            admin = psycopg2.connect(admin_dsn)
            admin.autocommit = True
            try:
                with admin.cursor() as cur:
                    cur.execute("SELECT 1 FROM pg_database WHERE datname=%s", (db_name,))
                    if cur.fetchone():
                        cur.execute(sql.SQL("DROP DATABASE {}").format(sql.Identifier(db_name)))
            finally:
                admin.close()


if __name__ == "__main__":
    main()
