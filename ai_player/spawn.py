#!/usr/bin/env python3
import argparse
import json
import os
import socket
import subprocess
import time
import random
import uuid
from pathlib import Path

from datetime import datetime, timezone
DEFAULT_TIMEOUT = 30.0


def send_command(sock, cmd):
    data = (json.dumps(cmd) + "\n").encode("utf-8")
    sock.sendall(data)


def recv_response(sock, timeout=DEFAULT_TIMEOUT):
    sock.settimeout(timeout)
    buf = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("Server closed connection while waiting for response")
        buf += chunk
        if b"\n" in buf:
            line, _rest = buf.split(b"\n", 1)
            line = line.strip()
            if not line:
                continue
            return json.loads(line.decode("utf-8"))


def ensure_account(host, port, username, password, client_version):
    import socket, json, uuid, time
    from datetime import datetime, timezone

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    sock.setblocking(False)

    buf = b""

    def send(cmd):
        sock.sendall((json.dumps(cmd) + "\n").encode("utf-8"))

    def recv_all():
        nonlocal buf
        try:
            data = sock.recv(4096)
            if not data:
                return []
            buf += data
        except BlockingIOError:
            return []
        resps = []
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.strip()
            if line:
                resps.append(json.loads(line))
        return resps

    # 1) Try login first
    login_id = f"login-{uuid.uuid4().hex[:8]}"
    login_cmd = {
        "id": login_id,
        "command": "auth.login",
        "data": {
            "username": username,
            "passwd": password,  # IMPORTANT: use 'passwd' here
        },
        "meta": {
            "client_version": client_version,
            "idempotency_key": login_id,
        },
    }
    send(login_cmd)

    # give server a moment
    #time.sleep(0.2)
    time.sleep(0.15 + random.random() * 0.25)
    for resp in recv_all():
        if resp.get("reply_to") == login_id and resp.get("status") == "ok":
            sock.close()
            return  # account already exists

    # 2) Register
    reg_id = f"reg-{uuid.uuid4().hex[:8]}"
    ts = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    register_cmd = {
        "id": reg_id,
        "ts": ts,
        "command": "auth.register",
        "auth": None,
        "data": {
            "username": username,
            "passwd": password,        # ← FIX: server expects 'passwd'
            "ship_name": f"{username}'s ship",
            "ui_locale": "en_GB",
            "ui_timezone": "Europe/London",
        },
        "meta": {
            "client_version": client_version,
            "idempotency_key": reg_id,
        },
    }

    send(register_cmd)

    deadline = time.time() + DEFAULT_TIMEOUT
    got_reply = False

    while time.time() < deadline:
        for resp in recv_all():
            if resp.get("reply_to") == reg_id:
                got_reply = True
                sock.close()
                if resp.get("status") == "ok":
                    return
                # Check if user already exists (error code 1105)
                elif resp.get("error", {}).get("code") == 1105:
                    # User already exists - this is fine, treat as success
                    return
                else:
                    raise RuntimeError(
                        f"auth.register failed for {username}: {resp}"
                    )
        time.sleep(0.05)  # small poll delay

    sock.close()
    raise RuntimeError(f"No response to auth.register for {username} within timeout")


def make_bot_config(base_cfg, idx, username, password, out_path: Path):
    cfg = dict(base_cfg)
    cfg["player_username"] = username
    cfg["player_password"] = password

    # De-clash files so bots don't all trample the same log/state/bug dirs
    suffix = f"_{idx:03d}"

    log_file = base_cfg.get("log_file", "ai_player.log")
    if log_file.endswith(".log"):
        cfg["log_file"] = log_file.replace(".log", f"{suffix}.log")
    else:
        cfg["log_file"] = f"{log_file}{suffix}"

    state_file = base_cfg.get("state_file", "state.json")
    if state_file.endswith(".json"):
        cfg["state_file"] = state_file.replace(".json", f"{suffix}.json")
    else:
        cfg["state_file"] = f"{state_file}{suffix}"

    bug_report_path = base_cfg.get("bug_report_path", "bug_reports")
    cfg["bug_report_path"] = str(bug_report_path) + suffix

    out_path.write_text(json.dumps(cfg, indent=2))
    return cfg


def main():
    p = argparse.ArgumentParser(description="Spawn many AI QA bot processes for twclone.")
    p.add_argument("--bot-dir", required=True,
                   help="Directory where bot configs, logs, and state files will be stored.")
    p.add_argument("--config-template", default=None,
                   help="Path to base config.json (defaults to ai_player/config.json).")
    p.add_argument("--host", default=None,
                   help="Game host (overrides game_host in template if set).")
    p.add_argument("--port", type=int, default=None,
                   help="Game port (overrides game_port in template if set).")
    p.add_argument("--amount", type=int, required=True,
                   help="Number of bots to create.")
    p.add_argument("--base-name", default="ai_qa_bot",
                   help="Prefix for bot usernames (suffix _001, _002, ... is added).")
    p.add_argument("--password", default="quality",
                   help="Password for all bots (or override as needed).")
    p.add_argument("--python", default="python3",
                   help="Python executable to use when launching bots.")
    p.add_argument("--no-launch", action="store_true",
                   help="Only create accounts & configs; do not start bot processes.")
    args = p.parse_args()

    # Bot dir is where we store configs and data files
    bot_dir = Path(args.bot_dir).resolve()
    bot_dir.mkdir(parents=True, exist_ok=True)
    
    # Code dir is where main.py lives (ai_player directory)
    code_dir = Path(__file__).parent.resolve()
    if not (code_dir / "main.py").exists():
        raise SystemExit(f"main.py not found in {code_dir}")

    # Config template defaults to config.json in code directory
    if args.config_template:
        cfg_path = Path(args.config_template)
        if not cfg_path.is_absolute():
            cfg_path = code_dir / cfg_path
    else:
        cfg_path = code_dir / "config.json"

    if not cfg_path.exists():
        raise SystemExit(f"Base config not found at {cfg_path}")

    base_cfg = json.loads(cfg_path.read_text())

    game_host = args.host or base_cfg.get("game_host", "127.0.0.1")
    game_port = args.port or int(base_cfg.get("game_port", 1234))
    client_version = base_cfg.get("client_version", "AI_QA_Bot/1.0")

    procs = []
    for i in range(1, args.amount + 1):
        username = f"{args.base_name}_{i:03d}"
        password = args.password

        print(f"[{i}/{args.amount}] ensuring account {username!r} exists...")
        ensure_account(game_host, game_port, username, password, client_version)

        cfg_out = bot_dir / f"config_bot_{i:03d}.json"
        make_bot_config(base_cfg, i, username, password, cfg_out)

        if args.no_launch:
            continue

        # Launch a bot process using the per-bot config
        # Config path is absolute, so main.py can find it regardless of cwd
        cmd = [args.python, str(code_dir / "main.py"), str(cfg_out)]
        print(f"  launching: {' '.join(cmd)}")
        procs.append(
            subprocess.Popen(
                cmd,
                cwd=str(bot_dir),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        )
        # Small stagger to avoid thundering herd on login
        time.sleep(0.25)

    if args.no_launch:
        print("Configs created and accounts ensured; no bots launched (--no-launch set).")
    else:
        print(f"Launched {len(procs)} bot processes.")
        # Script exits; children keep running.


if __name__ == "__main__":
    main()
