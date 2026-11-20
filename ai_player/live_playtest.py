#!/usr/bin/env python3
"""Provision and supervise an isolated, persistent 10-player V3 playtest.

All mutable game state lives under ~/.local/share/twclone-v3-live-playtest and
in a distinctly named Docker volume. This utility never discovers or mutates
other game databases or containers.
"""

from __future__ import annotations

import argparse
import json
import os
import secrets
import shutil
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
RUNTIME = Path.home() / ".local/share/twclone-v3-live-playtest"
UNIT_DIR = Path.home() / ".config/systemd/user"
PG_CONTAINER = "twclone-v3-live-playtest-pg"
PG_VOLUME = "twclone-v3-live-playtest-pgdata"
DB_PORT = 55639
GAME_HOST = "192.168.25.5"
GAME_PORT = 12484
UNIVERSE_SEED = 2026112001
OLLAMA_A = "http://192.168.25.5:11434"
OLLAMA_B = "http://192.168.25.20:11434"
MODEL = "qwen3:8b"
MODEL_ID = "500a1f067a9f"
BOT_ROLES = [
    ("Trader", "Maximise credits through profitable public port trades.", "trader"),
    ("Merchant", "Discover and repeat profitable trade routes.", "trader"),
    ("Coloniser", "Find eligible unowned worlds and develop them.", "balanced"),
    ("Industrialist", "Develop planetary workforce and production.", "balanced"),
    ("Explorer", "Discover sectors, ports and useful opportunities.", "explorer"),
    ("Pirate", "Seek permitted opportunities to attack vulnerable targets and capture resources.", "opportunist"),
    ("Bounty hunter", "Pursue currently hostile players and NPCs when combat rules allow.", "cautious"),
    ("Corporation founder", "Create and expand a corporation using supported public corporation actions.", "balanced"),
    ("Corporate operative", "Coordinate with BOT08 through supported corporation invitations and transfers.", "balanced"),
    ("Opportunist", "Adapt to current public market, exploration and combat opportunities.", "opportunist"),
]
NON_GAMEPLAY_ACTIONS = {
    "bank.balance", "bank.history", "combat.status", "corp.balance", "corp.status",
    "move.autopilot.status", "player.computer.recommend_routes", "player.info",
    "player.my_info", "player.ping", "planet.info", "sector.info", "ship.info",
    "ship.status", "shipyard.list", "trade.history", "trade.port_info", "trade.quote",
}


def completed_gameplay_actions(bot: str, log_text: str) -> list[tuple[str, str]]:
    """Return successful action records, excluding read-only status requests."""
    marker = f"PLAYTEST_ACTION bot={bot} "
    actions = []
    for line in log_text.splitlines():
        if marker not in line or "command=" not in line:
            continue
        command = line.rsplit("command=", 1)[1].strip().split()[0]
        if command not in NON_GAMEPLAY_ACTIONS:
            actions.append((line[:19], command))
    return actions


def run(args: list[str], *, check: bool = True, capture: bool = False,
        cwd: Path | None = None, env: dict | None = None):
    return subprocess.run(args, check=check, text=True,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.STDOUT if capture else None, cwd=cwd,
                          env=env)


def require_runtime() -> dict:
    path = RUNTIME / "playtest.json"
    if not path.exists():
        raise SystemExit("Playtest is not provisioned. Run live_playtest.py provision first.")
    return json.loads(path.read_text())


def _free_tcp_port(host: str, port: int) -> bool:
    sock = socket.socket()
    try:
        sock.bind((host, port))
        return True
    except OSError:
        return False
    finally:
        sock.close()


def _read_playtest_sql(sql: str) -> str:
    config = json.loads((RUNTIME / "bigbang.json").read_text())
    conn = config["app"].replace("%DB%", config["db"])
    password = conn.split("password=", 1)[1].split()[0]
    safe_conn = conn.replace(" password=" + password, "")
    result = run(["psql", safe_conn, "-X", "-A", "-F", "|", "-t", "-c", sql],
                 capture=True, env=dict(os.environ, PGPASSWORD=password))
    return result.stdout.strip()


def _ollama_tags(endpoint: str) -> dict:
    with urllib.request.urlopen(endpoint + "/api/tags", timeout=8) as response:
        return json.loads(response.read())


def check_ollama() -> dict:
    result = {}
    for name, endpoint in (("A", OLLAMA_A), ("B", OLLAMA_B)):
        tags = _ollama_tags(endpoint)
        models = {item.get("name"): item.get("digest", "") for item in tags.get("models", [])}
        installed = next((digest for name_, digest in models.items()
                          if name_ == MODEL or name_.startswith(MODEL + ":")), None)
        if not installed or not installed.startswith(MODEL_ID):
            raise SystemExit(f"Ollama {name} does not have required {MODEL} ID {MODEL_ID}: {models}")
        schema = {"type": "object", "properties": {
            "plan": {"type": "array", "items": {"type": "string",
                "enum": ["scan: density", "goto: 2"]}, "minItems": 1, "maxItems": 1}},
            "required": ["plan"], "additionalProperties": False}
        payload = {"model": MODEL, "stream": False, "think": False,
            "keep_alive": "30m", "format": schema,
            "options": {"temperature": 0.3, "num_ctx": 4096, "num_predict": 256},
            "messages": [
                {"role": "system", "content": "Return exactly one JSON plan with a legal action string from the schema."},
                {"role": "user", "content": "This is a schema validation probe. Return one of the exact allowed actions."},
            ]}
        request = urllib.request.Request(endpoint + "/api/chat",
            data=json.dumps(payload).encode(), headers={"Content-Type": "application/json"})
        started = time.monotonic()
        with urllib.request.urlopen(request, timeout=120) as response:
            body = json.loads(response.read())
        elapsed = time.monotonic() - started
        content = json.loads(body["message"]["content"])
        if content.get("plan") not in (["scan: density"], ["goto: 2"]):
            raise SystemExit(f"Ollama {name} failed structured decision validation: {content}")
        result[name] = {"endpoint": endpoint, "model": MODEL, "id": installed,
                        "probe_latency_seconds": round(elapsed, 2),
                        "structured_decision": content}
    print(json.dumps(result, indent=2))
    return result


def _write_bot_configs(runtime: dict) -> None:
    bot_root = RUNTIME / "bots"
    bot_root.mkdir(mode=0o700, parents=True, exist_ok=True)
    for index, (role, objective, profile) in enumerate(BOT_ROLES, start=1):
        bot = f"BOT{index:02d}"
        directory = bot_root / bot
        directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        old_path = directory / "config.json"
        if old_path.exists():
            continue
        endpoint = OLLAMA_A if index <= 5 else OLLAMA_B
        username = f"play{bot.lower()}"
        config = {
            "game_host": GAME_HOST, "game_port": GAME_PORT,
            "ollama_url": endpoint, "ollama_model": MODEL,
            "strategy_mode": "ollama", "require_llm_decisions": True,
            "ollama_num_ctx": 4096, "ollama_num_predict": 256,
            "ollama_temperature": 0.3, "ollama_keep_alive": "30m",
            "ollama_lock_dir": str(RUNTIME / "ollama-locks"),
            "ollama_max_concurrent": 2, "inference_timeout_seconds": 110,
            "ollama_retry_seconds": 35, "cooldown_seconds": 8,
            "startup_stagger_max_seconds": 0,
            "reconnect_base_seconds": 3, "reconnect_max_seconds": 60,
            "role_name": role, "primary_objective": objective,
            "behavior_profile": profile, "playtest_bot": bot,
            "playtest_index": index, "client_version": "TWCloneLivePlaytest/1.0",
            "register_if_missing": True,
            "player_username": username,
            "player_password": secrets.token_hex(24),
            "solo_empire_mode": False,
            "allow_corporation_play": index in (8, 9),
            "corporation_win_condition": index in (8, 9),
            "corporation_ai_players": 2 if index in (8, 9) else 0,
            "enable_bank_management": True,
            "enable_planet_creation": True,
            "enable_citadel_management": True,
            "enable_planet_workforce": True,
            "enable_planet_defense": True,
            "fleet_management": True,
            "persist_dangerous_sectors": True,
            "log_file": str(directory / "bot.log"),
            "state_file": str(directory / "state.json"),
            "bug_report_path": str(directory / "bug_reports"),
        }
        old_path.write_text(json.dumps(config, indent=2) + "\n")
        old_path.chmod(0o600)
    runtime["bot_count"] = 10
    runtime["ollama"] = check_ollama()


def provision(args) -> None:
    if not _free_tcp_port("127.0.0.1", DB_PORT):
        raise SystemExit(f"Dedicated database port {DB_PORT} is already occupied; refusing to reuse it.")
    if not _free_tcp_port(GAME_HOST, GAME_PORT):
        raise SystemExit(f"Game port {GAME_HOST}:{GAME_PORT} is already occupied.")
    if not _free_tcp_port("0.0.0.0", GAME_PORT):
        raise SystemExit(f"Game port {GAME_PORT} is already occupied on a local interface.")
    if not (REPO / "bin/server").is_file() or not (REPO / "bin/bigbang").is_file():
        raise SystemExit("Build the actual V3 server first: make -C bin server bigbang")
    RUNTIME.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(RUNTIME, 0o700)
    if run(["docker", "inspect", PG_CONTAINER], check=False, capture=True).returncode == 0:
        raise SystemExit(f"Refusing to reuse existing container {PG_CONTAINER}.")
    if run(["docker", "volume", "inspect", PG_VOLUME], check=False, capture=True).returncode == 0:
        raise SystemExit(f"Refusing to reuse existing volume {PG_VOLUME}.")

    password = secrets.token_hex(24)
    dbname = "twclone_live_playtest"
    app_conn = f"host=127.0.0.1 port={DB_PORT} dbname={dbname} user=postgres password={password} connect_timeout=5"
    admin_conn = f"host=127.0.0.1 port={DB_PORT} dbname=postgres user=postgres password={password} connect_timeout=5"
    runtime = {
        "repo": str(REPO), "db_container": PG_CONTAINER, "db_volume": PG_VOLUME,
        "db_port": DB_PORT, "db_name": dbname, "game_host": GAME_HOST,
        "game_port": GAME_PORT, "universe_sectors": 500,
        "universe_seed": UNIVERSE_SEED, "ollama_model": MODEL,
    }
    (RUNTIME / "playtest.json").write_text(json.dumps(runtime, indent=2) + "\n")
    (RUNTIME / "playtest.json").chmod(0o600)
    config = {
        "backend": "postgres", "db": dbname,
        "admin": admin_conn,
        "app": f"host=127.0.0.1 port={DB_PORT} dbname=%DB% user=postgres password={password} connect_timeout=5",
        "sql_dir": str(REPO / "sql/pg"), "sectors": 500,
        "density": 4, "port_ratio": 40, "planet_ratio": 30.0,
        "turnsperday": 120, "min_tunnels": 15, "min_tunnel_len": 4,
        "include_v3_migrations": True, "universe_seed": UNIVERSE_SEED,
        "exact_sector_count": True,
    }
    (RUNTIME / "bigbang.json").write_text(json.dumps(config, indent=2) + "\n")
    (RUNTIME / "bigbang.json").chmod(0o600)
    env = (f'TWCLONE_DB_APP_CONNINFO="{app_conn}"\n'
           f'TWCLONE_LISTEN_ADDRESS="{GAME_HOST}"\n'
           f'TWCLONE_LISTEN_PORT="{GAME_PORT}"\n'
           'TWCLONE_MAX_CONNECTIONS="64"\n'
           f'TWCLONE_LOG_PATH="{RUNTIME}/server.log"\n')
    (RUNTIME / "server.env").write_text(env)
    (RUNTIME / "server.env").chmod(0o600)

    run(["docker", "volume", "create", "--label", "twclone.playtest=true", PG_VOLUME])
    run(["docker", "run", "-d", "--name", PG_CONTAINER,
         "--label", "twclone.playtest=true", "--restart", "unless-stopped",
         "-e", "POSTGRES_PASSWORD=" + password,
         "-e", "POSTGRES_DB=" + dbname,
         "-p", f"127.0.0.1:{DB_PORT}:5432",
         "-v", f"{PG_VOLUME}:/var/lib/postgresql/data", "postgres:16-alpine"])
    wait_db()
    run([str(REPO / "bin/bigbang")], cwd=RUNTIME)
    runtime["universe_initialized"] = True
    (RUNTIME / "playtest.json").write_text(json.dumps(runtime, indent=2) + "\n")
    _write_bot_configs(runtime)
    (RUNTIME / "playtest.json").write_text(json.dumps(runtime, indent=2) + "\n")
    install_units()
    print(f"Provisioned isolated 500-sector universe. Seed={UNIVERSE_SEED}, DB port={DB_PORT}, game={GAME_HOST}:{GAME_PORT}")


def wait_db() -> None:
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        result = run(["docker", "exec", PG_CONTAINER, "pg_isready", "-U", "postgres"], check=False, capture=True)
        if result.returncode == 0:
            return
        time.sleep(1)
    raise SystemExit("Dedicated PostgreSQL did not become ready within 90 seconds.")


def install_units() -> None:
    UNIT_DIR.mkdir(parents=True, exist_ok=True)
    py = sys.executable
    cli = REPO / "ai_player/live_playtest.py"
    unit = {
        "twclone-live-server.service": f"""[Unit]
Description=TWClone isolated V3 live playtest server
After=default.target

[Service]
Type=simple
WorkingDirectory={RUNTIME}
EnvironmentFile={RUNTIME}/server.env
ExecStartPre={py} {cli} wait-db
ExecStart={REPO}/bin/server
Restart=on-failure
RestartSec=5
KillSignal=SIGTERM
TimeoutStopSec=30

[Install]
WantedBy=default.target
""",
        "twclone-live-bot@.service": f"""[Unit]
Description=TWClone live playtest bot %i
After=default.target

[Service]
Type=simple
WorkingDirectory={RUNTIME}/bots/%i
ExecStart={py} {REPO}/ai_player/main.py {RUNTIME}/bots/%i/config.json
Restart=always
RestartSec=15
KillSignal=SIGINT
TimeoutStopSec=25
StandardOutput=append:{RUNTIME}/bots/%i/process.log
StandardError=append:{RUNTIME}/bots/%i/process.log

[Install]
WantedBy=default.target
""",
        "twclone-live-controller.service": f"""[Unit]
Description=Start TWClone playtest bots sequentially with launch staggering
After=twclone-live-server.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart={py} {cli} start-bots
ExecStop=/bin/true

[Install]
WantedBy=default.target
""",
    }
    for name, contents in unit.items():
        target = UNIT_DIR / name
        target.write_text(contents)
        target.chmod(0o600)
    run(["systemctl", "--user", "daemon-reload"])
    run(["systemctl", "--user", "enable", "twclone-live-server.service",
         "twclone-live-controller.service"])


def start_bots() -> None:
    runtime = require_runtime()
    for index in range(1, 11):
        bot = f"BOT{index:02d}"
        service = f"twclone-live-bot@{bot}.service"
        active = run(["systemctl", "--user", "is-active", service], check=False, capture=True).returncode == 0
        action = "restart" if active else "start"
        print(f"{time.strftime('%Y-%m-%dT%H:%M:%S%z')} {action} {bot}", flush=True)
        run(["systemctl", "--user", action, service])
        if index < 10:
            time.sleep(float(runtime.get("bot_launch_interval_seconds", 5)))


def service(action: str, name: str) -> None:
    run(["systemctl", "--user", action, name])


def docker_container(action: str) -> None:
    runtime = require_runtime()
    inspected = run(["docker", "inspect", "--format", "{{ index .Config.Labels \"twclone.playtest\" }}", runtime["db_container"]], capture=True)
    if inspected.stdout.strip() != "true":
        raise SystemExit("Dedicated database ownership label mismatch; refusing Docker action.")
    run(["docker", action, runtime["db_container"]])


def status() -> None:
    runtime = require_runtime()
    print(f"server={GAME_HOST}:{GAME_PORT} universe={runtime.get('universe_sectors')} seed={runtime.get('universe_seed')}")
    for name, unit in (("game", "twclone-live-server.service"), ("controller", "twclone-live-controller.service")):
        result = run(["systemctl", "--user", "is-active", unit], check=False, capture=True)
        print(f"{name}: {result.stdout.strip() or 'inactive'}")
    db_state = run(["docker", "inspect", "--format", "{{.State.Status}}", PG_CONTAINER], check=False, capture=True).stdout.strip()
    print(f"postgres: {db_state or 'missing'}")
    try:
        authoritative = {}
        for row in _read_playtest_sql(
            "SELECT p.name, p.sector_id, p.ship_id, p.credits, t.turns_remaining, "
            "EXISTS(SELECT 1 FROM sessions s WHERE s.player_id=p.player_id AND s.expires>now()) "
            "FROM players p LEFT JOIN turns t USING (player_id) "
            "WHERE p.name LIKE 'playbot%' ORDER BY p.name"
        ).splitlines():
            values = row.split("|")
            if len(values) == 6:
                authoritative[values[0]] = {
                    "sector": values[1], "ship": values[2], "credits": values[3],
                    "turns": values[4], "session_valid": values[5] == "t",
                }
        humans = _read_playtest_sql(
            "SELECT count(DISTINCT p.player_id) FROM players p JOIN sessions s USING (player_id) "
            "WHERE s.expires > now() AND p.name NOT LIKE 'playbot%' AND NOT p.is_npc"
        )
        print(f"human players with valid authentication sessions: {humans or '0'}")
    except (OSError, subprocess.CalledProcessError, KeyError, IndexError):
        authoritative = {}
        print("player state: database readback unavailable")
    for index, (role, objective, _) in enumerate(BOT_ROLES, start=1):
        bot = f"BOT{index:02d}"
        unit = f"twclone-live-bot@{bot}.service"
        active = run(["systemctl", "--user", "is-active", unit], check=False, capture=True).stdout.strip() or "inactive"
        path = RUNTIME / "bots" / bot / "state.json"
        state = {}
        try:
            state = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            pass
        cfg = json.loads((RUNTIME / "bots" / bot / "config.json").read_text())
        info = (state.get("player_info") or {}).get("player") or {}
        metrics = state.get("ai_metrics") or {}
        endpoint = cfg["ollama_url"]
        ship = state.get("ship_info") or {}
        sector = state.get("player_location_sector") or info.get("current_sector", "?")
        db_player = authoritative.get(cfg["player_username"], {})
        if db_player:
            sector = db_player["sector"]
            ship_id = db_player["ship"]
            credits = db_player["credits"]
            turns = db_player.get("turns", "?")
        else:
            ship_id = ship.get("id", info.get("ship_id", "?"))
            credits = info.get("credits", "?")
            turns = (info.get("turns_remaining", "?"))
        # systemd's append-only capture survives an individual bot restart;
        # main.py's state log is intentionally a fresh per-process snapshot.
        bot_log = RUNTIME / "bots" / bot / "process.log"
        try:
            action_records = completed_gameplay_actions(bot, bot_log.read_text(errors="replace"))
            completed_actions = len(action_records)
            last_action = action_records[-1][1] if action_records else "?"
        except OSError:
            completed_actions = 0
            last_action = "?"
        print(f"{bot}: service={active} endpoint={endpoint} model={cfg['ollama_model']} "
              f"sector={sector} ship={ship_id} turns={turns} "
              f"credits={credits} valid_session={db_player.get('session_valid', False)} objective={objective} "
              f"actions={completed_actions} "
              f"inferences={metrics.get('ollama_successes', 0)}/{metrics.get('ollama_requests', 0)} "
              f"inference_last={state.get('ollama_last_latency_seconds', '?')}s "
              f"inference_avg={state.get('ollama_average_latency_seconds', '?')}s "
              f"inference_errors={metrics.get('ollama_errors', 0)} last={last_action} "
              f"reconnects={metrics.get('reconnects', 0)}")
    print("Human count reflects valid auth sessions, not live TCP sockets.")


def health() -> None:
    runtime = require_runtime()
    status()
    for name, endpoint in (("Ollama A", OLLAMA_A), ("Ollama B", OLLAMA_B)):
        try:
            tags = json.loads(urllib.request.urlopen(endpoint + "/api/tags", timeout=8).read())
            print(f"{name}: reachable models={','.join(m.get('name', '?') for m in tags.get('models', []))}")
        except Exception as exc:
            print(f"{name}: ERROR {type(exc).__name__}: {exc}")
    try:
        with socket.create_connection((GAME_HOST, GAME_PORT), timeout=3):
            print(f"game socket {GAME_HOST}:{GAME_PORT}: reachable from this host")
    except OSError as exc:
        print(f"game socket: ERROR {exc}")
    print(f"Database volume retained: {runtime['db_volume']}")


def reset_universe(args) -> None:
    if not args.confirm_reset:
        raise SystemExit("Reset destroys only this playtest database. Repeat with --confirm-reset to proceed.")
    runtime = require_runtime()
    service("stop", "twclone-live-controller.service")
    for index in range(1, 11):
        service("stop", f"twclone-live-bot@BOT{index:02d}.service")
    service("stop", "twclone-live-server.service")
    docker_container("stop")
    # The container and volume carry a dedicated label and unique playtest names.
    run(["docker", "start", PG_CONTAINER])
    wait_db()
    conn = json.loads((RUNTIME / "bigbang.json").read_text())["admin"]
    # Pass the password through the child environment, never the psql argv.
    password = conn.split("password=", 1)[1].split()[0]
    env = dict(os.environ, PGPASSWORD=password)
    safe_conn = conn.replace(" password=" + password, "")
    run(["psql", safe_conn, "-v", "ON_ERROR_STOP=1",
         "-c", f"DROP DATABASE IF EXISTS {runtime['db_name']} WITH (FORCE)"], env=env)
    run(["psql", safe_conn, "-v", "ON_ERROR_STOP=1",
         "-c", f"CREATE DATABASE {runtime['db_name']}"], env=env)
    run([str(REPO / "bin/bigbang")], cwd=RUNTIME)
    for index in range(1, 11):
        state = RUNTIME / "bots" / f"BOT{index:02d}" / "state.json"
        state.unlink(missing_ok=True)
    runtime["universe_initialized"] = True
    (RUNTIME / "playtest.json").write_text(json.dumps(runtime, indent=2) + "\n")
    print("Playtest universe reset and regenerated with the same deterministic seed.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("provision", help="Create a dedicated DB, deterministic universe, private bot identities and user services")
    sub.add_parser("check-ollama")
    sub.add_parser("wait-db")
    sub.add_parser("install-units")
    sub.add_parser("start-bots")
    sub.add_parser("start", help="Start DB, server and sequentially staggered bots")
    sub.add_parser("stop", help="Stop only this playtest; preserve its database and identities")
    sub.add_parser("restart-server")
    restart_bot = sub.add_parser("restart-bot")
    restart_bot.add_argument("bot", choices=[f"BOT{i:02d}" for i in range(1, 11)])
    sub.add_parser("status")
    sub.add_parser("health")
    logs = sub.add_parser("logs")
    logs.add_argument("target", choices=["server", "bots", "database"], default="server", nargs="?")
    reset = sub.add_parser("reset")
    reset.add_argument("--confirm-reset", action="store_true")
    args = parser.parse_args()
    if args.command == "provision":
        provision(args)
    elif args.command == "check-ollama":
        check_ollama()
    elif args.command == "wait-db":
        wait_db()
    elif args.command == "install-units":
        install_units()
    elif args.command == "start-bots":
        start_bots()
    elif args.command == "start":
        runtime = require_runtime()
        docker_container("start")
        wait_db()
        run(["systemctl", "--user", "enable", "twclone-live-server.service",
             "twclone-live-controller.service"])
        service("start", "twclone-live-server.service")
        time.sleep(3)
        service("restart" if run(["systemctl", "--user", "is-active", "twclone-live-controller.service"], check=False, capture=True).returncode == 0 else "start",
                "twclone-live-controller.service")
    elif args.command == "stop":
        service("stop", "twclone-live-controller.service")
        for index in range(1, 11):
            service("stop", f"twclone-live-bot@BOT{index:02d}.service")
        service("stop", "twclone-live-server.service")
        docker_container("stop")
    elif args.command == "restart-server":
        service("restart", "twclone-live-server.service")
    elif args.command == "restart-bot":
        service("restart", f"twclone-live-bot@{args.bot}.service")
    elif args.command == "status":
        status()
    elif args.command == "health":
        health()
    elif args.command == "logs":
        if args.target == "server":
            print((RUNTIME / "server.log").read_text(errors="replace")[-16000:])
        elif args.target == "database":
            run(["docker", "logs", "--tail", "100", PG_CONTAINER])
        else:
            for index in range(1, 11):
                path = RUNTIME / "bots" / f"BOT{index:02d}" / "bot.log"
                if path.exists():
                    print(f"--- BOT{index:02d} ---\n" + path.read_text(errors="replace")[-5000:])
    elif args.command == "reset":
        reset_universe(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
