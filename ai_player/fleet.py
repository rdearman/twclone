#!/usr/bin/env python3
"""Run a bounded cohort of existing AI players as isolated processes.

Each source JSON file uses the ordinary ai_player config format. This runner
does not create accounts or provision game resources; accounts must already
exist and be authorised in the target test universe.
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
import uuid
from pathlib import Path
from typing import Any


MAX_PLAYERS = 50
DEFAULT_MAX_CONCURRENT = 5
DEFAULT_DURATION_SECONDS = 1800


class FleetConfigError(ValueError):
    """Raised when cohort configs cannot be run safely and independently."""


def load_player_configs(config_dir: Path, count: int) -> list[tuple[Path, dict[str, Any]]]:
    if not 1 <= count <= MAX_PLAYERS:
        raise FleetConfigError(f"count must be between 1 and {MAX_PLAYERS}")
    files = sorted(config_dir.glob("*.json"))
    if len(files) < count:
        raise FleetConfigError(f"requested {count} players, found only {len(files)} config files")

    players = []
    usernames: set[str] = set()
    passwords: set[str] = set()
    for path in files[:count]:
        try:
            config = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise FleetConfigError(f"cannot read player config {path.name}: {exc}") from exc
        if not isinstance(config, dict):
            raise FleetConfigError(f"player config {path.name} must contain a JSON object")
        username = config.get("player_username")
        password = config.get("player_password")
        if not isinstance(username, str) or not username.strip():
            raise FleetConfigError(f"player config {path.name} has no player_username")
        if not isinstance(password, str) or not password:
            raise FleetConfigError(f"player config {path.name} has no player_password")
        if username in usernames:
            raise FleetConfigError("player configs must use distinct usernames")
        if password in passwords:
            raise FleetConfigError("player configs must use distinct credentials")
        usernames.add(username)
        passwords.add(password)
        players.append((path, config))
    return players


def prepare_runtime_configs(
    players: list[tuple[Path, dict[str, Any]]], run_root: Path
) -> list[dict[str, Any]]:
    prepared = []
    for index, (source, config) in enumerate(players, start=1):
        username = config["player_username"]
        player_dir = run_root / f"{index:02d}_{username}"
        player_dir.mkdir(parents=True, exist_ok=False)
        os.chmod(player_dir, 0o700)
        runtime = dict(config)
        runtime["log_file"] = str(player_dir / "ai_player.log")
        runtime["state_file"] = str(player_dir / "state.json")
        runtime["bug_report_path"] = str(player_dir / "bug_reports")
        # Keep each bot's planner stagger independent while the cohort manager
        # controls the actual concurrency bound.
        runtime_config_path = player_dir / "config.json"
        runtime_config_path.write_text(json.dumps(runtime, indent=2), encoding="utf-8")
        os.chmod(runtime_config_path, 0o600)
        prepared.append({
            "identity": username,
            "config_path": str(runtime_config_path),
            "player_dir": str(player_dir),
            "source_config": source.name,
        })
    return prepared


def load_resumable_players(run_root: Path) -> list[dict[str, Any]]:
    """Load the private per-player configs from a prior interrupted cohort."""
    players = []
    os.chmod(run_root, 0o700)
    usernames: set[str] = set()
    passwords: set[str] = set()
    for config_path in sorted(run_root.glob("*/config.json")):
        try:
            config = json.loads(config_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise FleetConfigError(f"cannot read saved player config {config_path.parent.name}") from exc
        username, password = config.get("player_username"), config.get("player_password")
        if not isinstance(username, str) or not username or not isinstance(password, str) or not password:
            raise FleetConfigError(f"saved player config {config_path.parent.name} lacks identity")
        if username in usernames or password in passwords:
            raise FleetConfigError("saved cohort contains duplicate player identity or credentials")
        usernames.add(username)
        passwords.add(password)
        player_dir = config_path.parent.resolve()
        if config_path.resolve().parent != player_dir:
            raise FleetConfigError("saved player config must remain inside its player directory")
        os.chmod(player_dir, 0o700)
        os.chmod(config_path, 0o600)
        for key in ("state_file", "log_file", "bug_report_path"):
            target = Path(config.get(key, ""))
            if not target.is_absolute() or target.resolve().parent != player_dir:
                raise FleetConfigError("saved runtime paths must remain isolated inside each player directory")
        players.append({
            "identity": username,
            "config_path": str(config_path.resolve()),
            "player_dir": str(player_dir),
            "source_config": "resumed",
        })
    if not players:
        raise FleetConfigError("resume directory contains no per-player configs")
    if len(players) > MAX_PLAYERS:
        raise FleetConfigError(f"resume directory exceeds the {MAX_PLAYERS}-player limit")
    return players


def _signal_process_group(process: subprocess.Popen, sig: int) -> None:
    try:
        os.killpg(process.pid, sig)
    except ProcessLookupError:
        pass
    except (AttributeError, PermissionError, OSError):
        # Popen instances supplied by tests may not represent OS processes.
        try:
            process.send_signal(sig)
        except (AttributeError, ProcessLookupError, OSError):
            pass


def stop_processes(processes: list[subprocess.Popen], grace_seconds: float = 10.0) -> None:
    """Ask only our own children to exit through main.py's KeyboardInterrupt path."""
    active = [process for process in processes if process.poll() is None]
    for process in active:
        _signal_process_group(process, signal.SIGINT)
    deadline = time.monotonic() + max(0.0, grace_seconds)
    for process in active:
        remaining = max(0.0, deadline - time.monotonic())
        try:
            process.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            _signal_process_group(process, signal.SIGTERM)
            try:
                process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                _signal_process_group(process, signal.SIGKILL)
                process.wait()


def _player_summary(player: dict[str, Any], exit_code: int | None) -> dict[str, Any]:
    summary: dict[str, Any] = {
        "identity": player["identity"],
        "exit_code": exit_code,
        "runtime_dir": player["player_dir"],
        "metrics": {},
        "state_available": False,
    }
    state_file = Path(player["player_dir"]) / "state.json"
    try:
        state = json.loads(state_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return summary
    summary["state_available"] = True
    metrics = state.get("ai_metrics")
    if isinstance(metrics, dict):
        # Restrict the aggregate to the existing numeric gameplay metrics.
        summary["metrics"] = {
            key: value for key, value in metrics.items()
            if isinstance(value, (int, float)) and not isinstance(value, bool)
        }
    summary["current_sector"] = state.get("player_location_sector")
    summary["strategy_plan_source"] = state.get("strategy_plan_source")
    return summary


def run_cohort(
    players: list[dict[str, Any]], *, run_root: Path, duration_seconds: float,
    max_concurrent: int, python: str = sys.executable,
    main_path: Path | None = None,
) -> dict[str, Any]:
    if not 1 <= max_concurrent <= min(MAX_PLAYERS, len(players)):
        raise FleetConfigError("max_concurrent must be between 1 and the cohort size (up to 50)")
    if duration_seconds <= 0:
        raise FleetConfigError("duration_seconds must be positive")
    main_path = main_path or Path(__file__).with_name("main.py")
    records = []
    pending = list(players)
    active: list[tuple[dict[str, Any], subprocess.Popen, Any]] = []
    deadline = time.monotonic() + duration_seconds
    interrupted = False

    def launch(player: dict[str, Any]) -> None:
        log = open(Path(player["player_dir"]) / "process.log", "ab")
        process = subprocess.Popen(
            [python, str(main_path), player["config_path"]],
            cwd=player["player_dir"], stdout=log, stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        active.append((player, process, log))

    try:
        while (pending or active) and time.monotonic() < deadline:
            while pending and len(active) < max_concurrent:
                launch(pending.pop(0))
            for entry in list(active):
                player, process, log = entry
                code = process.poll()
                if code is not None:
                    log.close()
                    active.remove(entry)
                    records.append(_player_summary(player, code))
            if active:
                time.sleep(0.2)
    except KeyboardInterrupt:
        interrupted = True
    finally:
        owned_processes = [entry[1] for entry in active]
        stop_processes(owned_processes)
        for player, process, log in active:
            code = process.poll()
            if code is None:
                try:
                    code = process.wait(timeout=0)
                except subprocess.TimeoutExpired:
                    code = None
            log.close()
            records.append(_player_summary(player, code))

    # A bounded run may leave configs that were never started. Include them so
    # the cohort report records the requested population and its actual status.
    completed_names = {item["identity"] for item in records}
    for player in players:
        if player["identity"] not in completed_names:
            records.append({
                "identity": player["identity"], "exit_code": None,
                "runtime_dir": player["player_dir"], "metrics": {},
                "state_available": False, "status": "not_started_before_duration_limit",
            })
    report = {
        "run_id": run_root.name,
        "requested_players": len(players),
        "max_concurrent": max_concurrent,
        "duration_seconds": duration_seconds,
        "interrupted": interrupted,
        "players": records,
    }
    report_path = run_root / "summary.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    os.chmod(report_path, 0o600)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="Run existing AI players as a bounded QA cohort.")
    parser.add_argument("--config-dir", type=Path,
                        help="Directory of per-player configs using ai_player/config.json fields")
    parser.add_argument("--count", type=int, help="Players to run (1–50); inferred when resuming")
    parser.add_argument("--run-root", type=Path,
                        help="Parent directory for a new private run")
    parser.add_argument("--resume-run", type=Path,
                        help="Resume a prior run directory, preserving each player's saved state")
    parser.add_argument("--max-concurrent", type=int, default=DEFAULT_MAX_CONCURRENT)
    parser.add_argument("--duration-seconds", type=float, default=DEFAULT_DURATION_SECONDS)
    parser.add_argument("--python", default=sys.executable)
    args = parser.parse_args()

    try:
        if args.resume_run:
            if args.config_dir or args.run_root:
                raise FleetConfigError("--resume-run cannot be combined with --config-dir or --run-root")
            run_root = args.resume_run.resolve()
            prepared = load_resumable_players(run_root)
            if args.count is not None and args.count != len(prepared):
                raise FleetConfigError("--count must match the saved cohort size when resuming")
        else:
            if not args.config_dir or not args.run_root or args.count is None:
                raise FleetConfigError("new runs require --config-dir, --count and --run-root")
            source_players = load_player_configs(args.config_dir, args.count)
            run_id = f"fleet-{time.strftime('%Y%m%d-%H%M%S')}-{uuid.uuid4().hex[:8]}"
            run_root = args.run_root.resolve() / run_id
            run_root.mkdir(parents=True, exist_ok=False)
            os.chmod(run_root, 0o700)
            prepared = prepare_runtime_configs(source_players, run_root)
        report = run_cohort(
            prepared, run_root=run_root, duration_seconds=args.duration_seconds,
            max_concurrent=args.max_concurrent, python=args.python,
        )
    except FleetConfigError as exc:
        parser.error(str(exc))
    print(json.dumps({
        "run_id": report["run_id"], "requested_players": report["requested_players"],
        "started_or_pending": len(report["players"]),
        "summary": str(run_root / "summary.json"),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
