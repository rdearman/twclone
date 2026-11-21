import sys
import time
import socket
import errno
import json
import copy
import logging
import os
import uuid
import re
import random # Add this import
import select
import threading
from datetime import datetime, timezone, timedelta

from bug_reporter import BugReporter, redact_secrets
from planner import Planner
from state_manager import StateManager
from llm_client import get_ollama_response, parse_llm_json
from turn_budget import (affordable_plan, command_fits_turn_budget,
                         command_turn_cost, turn_refresh_due)
from turn_budget import session_heartbeat_interval
from bandit_policy import BanditPolicy, make_context_key 
from typing import Optional
from helpers import canon_commodity
from v3_capabilities import (
    build_system_hello,
    capability_snapshot,
    build_player_command,
    normalize_command_catalogue,
    eligible_combat_targets,
)

# --- Standard Logging Setup ---
logger = logging.getLogger()
logger.setLevel(logging.DEBUG)
#logger.setLevel(logging.DEBUG)

# Console Handler
c_handler = logging.StreamHandler(sys.stdout)
c_handler.setLevel(logging.DEBUG)  # Set to DEBUG to see all logs
c_formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
c_handler.setFormatter(c_formatter)
logger.addHandler(c_handler)

# --- FIX: File Handler setup ---
def setup_logging(config):
    """Configures file logging based on config."""
    console_level = config.get("console_log_level", "INFO")
    if isinstance(console_level, str):
        console_level = logging._nameToLevel.get(console_level.upper(), logging.INFO)
    c_handler.setLevel(console_level if isinstance(console_level, int) else logging.INFO)
    log_file = config.get("log_file", "ai_player.log")
    f_handler = logging.FileHandler(log_file, mode='w')
    f_handler.setLevel(logging.INFO)  # Keep file log cleaner at INFO level
    try:
        os.chmod(log_file, 0o600)
    except OSError as exc:
        logger.warning("Could not restrict permissions on AI log file: %s", exc)
    f_formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    f_handler.setFormatter(f_formatter)
    logger.addHandler(f_handler)

# Suppress noisy libraries
logging.getLogger("urllib3").setLevel(logging.WARNING)
logging.getLogger("httpcore").setLevel(logging.WARNING)
logging.getLogger("httpx").setLevel(logging.WARNING)

# --- Global Shutdown Flag ---
shutdown_flag = False


def _utc_clock_minutes(value):
    try:
        hour_text, minute_text = str(value).split(":", 1)
        hour, minute = int(hour_text), int(minute_text)
        if 0 <= hour < 24 and 0 <= minute < 60:
            return hour * 60 + minute
    except (TypeError, ValueError):
        pass
    return None


def population_pause_seconds(config, state_manager, now=None, turns_remaining=None):
    """Return seconds until the next active period, or zero while population is active.

    Population pacing is opt-in. Turn use is counted from observed server
    `turns_remaining` decreases; this function never estimates or grants turns.
    """
    if not config.get("population_mode", False):
        return 0.0
    now = time.time() if now is None else float(now)
    state = state_manager.state
    updates = {}

    start_minute = _utc_clock_minutes(config.get("population_daily_start_utc", "00:00"))
    end_minute = _utc_clock_minutes(config.get("population_daily_end_utc", "00:00"))
    if start_minute is not None and end_minute is not None and start_minute != end_minute:
        current = datetime.fromtimestamp(now, timezone.utc)
        minute = current.hour * 60 + current.minute
        if start_minute < end_minute:
            in_window = start_minute <= minute < end_minute
        else:
            in_window = minute >= start_minute or minute < end_minute
        if not in_window:
            if state.get("population_active_since") is not None:
                updates.update({
                    "population_active_since": None,
                    "population_turns_used": 0,
                    "population_last_turns": None,
                })
            if updates:
                state.update(updates)
                state_manager.save_state()
            next_start = current.replace(hour=start_minute // 60,
                                         minute=start_minute % 60,
                                         second=0, microsecond=0)
            if next_start.timestamp() <= now:
                next_start += timedelta(days=1)
            return max(1.0, next_start.timestamp() - now)

    if state.get("population_turns_unverified"):
        try:
            return max(1.0, float(config.get("population_turn_readback_retry_seconds", 60)))
        except (TypeError, ValueError):
            return 60.0

    inactive_until = state.get("population_inactive_until")
    try:
        remaining_inactive = float(inactive_until) - now if inactive_until is not None else 0.0
    except (TypeError, ValueError):
        remaining_inactive = 0.0
    if remaining_inactive > 0:
        return remaining_inactive
    if inactive_until is not None:
        updates["population_inactive_until"] = None
        updates["population_active_since"] = now
        updates["population_turns_used"] = 0
        updates["population_last_turns"] = turns_remaining
    elif state.get("population_active_since") is None:
        updates.update({
            "population_active_since": now,
            "population_turns_used": 0,
            "population_last_turns": turns_remaining,
        })

    active_since = updates.get("population_active_since", state.get("population_active_since"))
    turns_used = int(updates.get("population_turns_used", state.get("population_turns_used", 0)) or 0)
    last_turns = updates.get("population_last_turns", state.get("population_last_turns"))
    try:
        current_turns = int(turns_remaining) if turns_remaining is not None else None
        previous_turns = int(last_turns) if last_turns is not None else None
    except (TypeError, ValueError):
        current_turns = previous_turns = None
    if current_turns is not None:
        if previous_turns is not None and current_turns < previous_turns:
            turns_used += previous_turns - current_turns
            updates["population_turns_used"] = turns_used
        if current_turns != previous_turns:
            updates["population_last_turns"] = current_turns

    try:
        active_seconds = max(60.0, float(config.get("population_active_minutes", 30)) * 60.0)
    except (TypeError, ValueError):
        active_seconds = 1800.0
    try:
        inactive_seconds = max(0.0, float(config.get("population_inactive_minutes", 120)) * 60.0)
    except (TypeError, ValueError):
        inactive_seconds = 7200.0
    try:
        turn_budget = max(1, int(config.get("population_max_turns_per_period", 12)))
    except (TypeError, ValueError):
        turn_budget = 12

    elapsed = now - float(active_since) if active_since is not None else 0.0
    if elapsed >= active_seconds or turns_used >= turn_budget:
        inactive_until = now + inactive_seconds
        updates.update({
            "population_active_since": None,
            "population_inactive_until": inactive_until if inactive_seconds else None,
            "population_turns_used": turns_used,
        })
        wait = inactive_seconds
    else:
        wait = 0.0
    if updates:
        state.update(updates)
        state_manager.save_state()
    return wait


def population_turn_readback_required(command_name, config):
    """Whether a successful command may have consumed a server turn."""
    read_only = {
        "auth.login", "auth.register", "player.my_info", "player.info", "player.ping",
        "ship.info", "ship.status", "sector.info", "trade.port_info", "trade.quote",
        "bank.balance", "bank.history", "bank.statement", "corp.status", "corp.balance",
        "corp.roster", "corp.list", "ship.inspect", "shipyard.list", "combat.status",
        "planet.info", "trade.history", "trade.offer.list", "notes.list",
        "system.schema", "system.cmd_list", "system.schema_list",
        "player.computer.recommend_routes", "move.autopilot.status",
    }
    return bool(command_name) and command_name not in read_only and not command_name.startswith("system.")


def reconcile_planet_transition_receipt(state_manager, response_type, response_data):
    """Apply only authoritative landing/departure receipts to the active ship cache."""
    if response_type == "planet.land.success":
        ship = dict(state_manager.get("ship_info") or {})
        ship["onplanet"] = 1
        planet_id = response_data.get("planet_id") if isinstance(response_data, dict) else None
        if planet_id is not None:
            ship["planet_id"] = int(planet_id)
            state_manager.set("planet_info", None)
            state_manager.set("planet_landing_pending_refresh", {
                "planet_id": int(planet_id), "player_confirmed": False,
                "ship_confirmed": False,
            })
        state_manager.update_player_info({"ship": ship})
        return "landed", planet_id
    if response_type == "planet.launch.success":
        ship = dict(state_manager.get("ship_info") or {})
        ship["onplanet"] = 0
        ship.pop("planet_id", None)
        state_manager.update_player_info({"ship": ship})
        state_manager.set("planet_info", None)
        state_manager.set("planet_landing_pending_refresh", None)
        return "launched", None
    return None, None


def _confirm_planet_landing_readback(state_manager, response_type):
    """Unlock planet actions only after both public identity snapshots match."""
    pending = state_manager.get("planet_landing_pending_refresh")
    if not isinstance(pending, dict):
        return False
    pending = dict(pending)
    if response_type in ("player.my_info", "player.info"):
        pending["player_confirmed"] = True
    elif response_type in ("ship.info", "ship.status"):
        pending["ship_confirmed"] = True
    state_manager.set("planet_landing_pending_refresh", pending)
    if not (pending.get("player_confirmed") and pending.get("ship_confirmed")):
        return False
    player = ((state_manager.get("player_info") or {}).get("player") or {})
    ship = state_manager.get("ship_info") or {}
    active_ship_id = player.get("ship_id")
    ship_id = ship.get("id", ship.get("ship_id"))
    landed_planet = ship.get("planet_id", ship.get("last_planet_id"))
    if landed_planet is None:
        landed_planet = player.get("last_planet_id")
    try:
        valid = (int(active_ship_id) == int(ship_id)
                 and bool(ship.get("onplanet"))
                 and int(landed_planet) == int(pending.get("planet_id")))
    except (TypeError, ValueError):
        valid = False
    if valid:
        state_manager.set("planet_landing_pending_refresh", None)
        state_manager.set("planet_info", None)
        return True
    return False


def mutation_reconciliation_contract(command_name):
    """Describe authoritative evidence required before clearing ambiguity."""
    contracts = {
        "trade.buy": ["trade.buy_receipt_v1", "player.my_info", "ship.info"],
        "trade.sell": ["trade.sell_receipt_v1", "player.my_info", "ship.info"],
        "move.warp": ["move.result", "player.my_info", "ship.info"],
        "bank.deposit": ["bank.balance", "player.my_info"],
        "bank.withdraw": ["bank.balance", "player.my_info"],
        "ship.upgrade": ["ship.info", "player.my_info"],
        "planet.land": ["planet.land.success", "player.my_info", "ship.info"],
        "planet.launch": ["planet.launch.success", "player.my_info", "ship.info"],
        "port.dock": ["player.dock.v1", "player.my_info", "ship.info"],
        "move.transwarp": ["move.result", "player.my_info", "ship.info"],
        "move.autopilot.start": ["move.autopilot.route_v1", "player.my_info"],
        "move.autopilot.control": ["move.autopilot.controlled_v1", "player.my_info"],
        "move.autopilot.stop": ["move.autopilot.stopped_v1", "player.my_info"],
        "planet.deposit": ["player.planet_transfer.v1", "planet.info", "ship.info"],
        "planet.withdraw": ["player.planet_transfer.v1", "planet.info", "ship.info"],
        "planet.fighters.transfer": ["planet.fighters.transferred_v1", "planet.info"],
        "planet.colonists.set": ["planet.colonists.set", "planet.info", "player.my_info", "ship.info"],
        "planet.colonists.get": ["planet.colonists.get"],
        "planet.colonists.allocate": ["planet.info", "player.my_info"],
        "planet.genesis_create": ["planet.genesis_created_v1", "planet.info", "player.my_info"],
        "planet.create": ["planet.genesis_created_v1", "planet.info", "player.my_info"],
        "citadel.build": ["citadel.upgrade_started", "planet.info"],
        "citadel.upgrade": ["citadel.upgrade_started", "planet.info"],
        "planet.market.sell": ["planet.info", "player.my_info"],
        "planet.market.buy_order": ["planet.info", "player.my_info"],
        "bank.transfer": ["bank.balance", "bank.statement"],
        "corp.create": ["corp.create.success", "corp.status"],
        "corp.invite": ["corp.invite.success", "corp.status"],
        "corp.join": ["corp.join.success", "corp.status"],
        "corp.kick": ["corp.kick.success", "corp.status"],
        "corp.leave": ["corp.leave.success", "corp.status"],
        "corp.dissolve": ["corp.leave.dissolved", "corp.status"],
        "corp.transfer_ceo": ["corp.transfer_ceo.success", "corp.status"],
        "corp.deposit": ["corp.deposit.success", "corp.balance"],
        "corp.withdraw": ["corp.withdraw.success", "corp.balance"],
        "corp.set_tax": ["corp.status"],
        "corp.issue_dividend": ["corp.status", "corp.balance"],
        "hardware.buy": ["ship.info", "player.my_info"],
        "shipyard.upgrade": ["ship.info", "player.my_info"],
        "ship.repair": ["ship.info", "player.my_info"],
        "ship.transfer_cargo": ["ship.info", "player.my_info"],
        "ship.jettison": ["ship.info"],
        "ship.rename": ["ship.info"],
        "notes.set": ["player.note.set_v1", "notes.list"],
        "notes.delete": ["player.note.deleted_v1", "notes.list"],
        "fine.pay": ["fine.paid", "player.my_info"],
        "bounty.post": ["bounty.list", "player.my_info"],
        "trade.offer": ["trade.offer.created_v1", "player.my_info"],
        "trade.accept": ["trade.offer.accepted_v1", "player.my_info"],
        "trade.cancel": ["trade.offer.cancelled_v1", "player.my_info"],
        "combat.attack.ship": ["combat.ship_damage.v1", "ship.info", "player.my_info"],
        "combat.attack.port": ["player.port_strike.v1", "player.my_info", "ship.info"],
        "combat.attack_planet": ["player.attack_planet.v1", "planet.info", "ship.info"],
        "move.intercept": ["move.result", "player.my_info", "ship.info"],
        "ship.repair": ["ship.repair", "player.my_info", "ship.info"],
        "fighters.recall": ["combat.fighters.deployed", "sector.info", "ship.info"],
        "mines.recall": ["combat.mines.recalled", "sector.info", "ship.info"],
        "notice.ack": ["notice.acknowledged", "notice.list"],
        "nav.avoid.add": ["nav.avoid.add", "nav.avoid.list"],
        "nav.avoid.remove": ["nav.avoid.remove", "nav.avoid.list"],
    }
    return {"response_types": contracts.get(command_name, []), "readback_required": True}


def uncertain_mutation_readback_requests(command_name, data=None, current_state=None):
    """Return authoritative readbacks for costly writes whose reply can be lost.

    These are deliberately reads, never automatic mutation retries. The bot
    remains paused until fresh public state has been received for every
    affected balance/inventory view.
    """
    data = data or {}
    current_state = current_state or {}
    base = [("player.my_info", "player.my_info", {}),
            ("ship.info", "ship.info", {})]
    if command_name in {"hardware.buy", "shipyard.sell", "shipyard.upgrade",
                        "ship.repair", "trade.buy", "trade.sell"}:
        return [("player.my_info", "player.my_info", {}),
                ("ship.info", "ship.info", {})]
    if command_name in {"bank.deposit", "bank.withdraw", "bank.transfer"}:
        return [("player.my_info", "player.my_info", {}),
                ("bank.balance", "bank.balance", {})]
    if command_name.startswith("planet.") or command_name.startswith("citadel."):
        planet_id = data.get("planet_id")
        ship = current_state.get("ship_info") or {}
        planet_id = planet_id or ship.get("planet_id")
        requests = list(base)
        if planet_id is None:
            sector_id = data.get("sector_id", current_state.get("player_location_sector"))
            if sector_id is None:
                return requests
            return requests + [("sector.info", "sector.info", {"sector_id": sector_id})]
        requests.append(("planet.info", "planet.info", {"planet_id": planet_id}))
        return requests
    if command_name.startswith("corp."):
        requests = list(base) + [("corp.status.success", "corp.status", {}),
                                 ("corp.balance.success", "corp.balance", {})]
        if command_name in {"corp.member.transfer", "corp.kick", "corp.leave"}:
            player = ((current_state.get("player_info") or {}).get("player") or {})
            corp_id = data.get("corp_id", player.get("corp_id"))
            if corp_id is not None:
                requests.append(("corp.roster.success", "corp.roster", {"corp_id": corp_id}))
        return requests
    if command_name in {"combat.attack", "combat.attack.ship", "combat.attack.port",
                        "combat.attack_planet", "combat.fire_photon",
                        "combat.deploy_fighters", "combat.deploy_mines"}:
        sector_id = data.get("sector_id")
        requests = list(base)
        sector_id = sector_id or current_state.get("player_location_sector")
        if sector_id is not None:
            requests.append(("sector.info", "sector.info", {"sector_id": sector_id}))
        return requests
    if command_name.startswith("move."):
        sector_id = data.get("to_sector_id", current_state.get("player_location_sector"))
        requests = list(base)
        if sector_id is not None:
            requests.append(("sector.info", "sector.info", {"sector_id": sector_id}))
        return requests
    # Conservative fallback for any other state-changing command: refresh
    # account and active-ship state before the planner can submit it again.
    return list(base)


def classify_protocol_outcome(response, config):
    """Classify ordinary refusals separately from server/infrastructure errors."""
    if isinstance(response, dict) and response.get("status") == "refused":
        error = response.get("error") or {}
        code = error.get("code")
        unexpected = set(config.get("qa_unexpected_refusal_codes", []))
        return "unexpected_refusal" if code in unexpected else "expected_refusal"
    if isinstance(response, dict) and response.get("status") == "error":
        return "infrastructure_or_protocol_error"
    return "success" if isinstance(response, dict) and response.get("status") == "ok" else "unknown"


def _remember_owned_planet(state_manager, planet):
    """Persist only server-confirmed player/corporation planet ownership."""
    if not isinstance(planet, dict):
        return
    player = ((state_manager.get("player_info") or {}).get("player") or {})
    try:
        player_id = int(player.get("player_id", player.get("id", 0)) or 0)
        corp_id = int(player.get("corp_id", 0) or 0)
        planet_id = int(planet.get("planet_id", planet.get("id", 0)) or 0)
        sector_id = int(planet.get("sector_id", 0) or 0)
        owner_id = int(planet.get("owner_id", 0) or 0)
    except (TypeError, ValueError):
        return
    owner_type = str(planet.get("owner_type", "")).lower()
    owned = ((owner_type == "player" and player_id > 0 and owner_id == player_id)
             or (owner_type in ("corp", "corporation") and corp_id > 0 and owner_id == corp_id))
    planets = []
    for item in state_manager.get("owned_planets") or []:
        if not isinstance(item, dict):
            continue
        try:
            existing_id = int(item.get("planet_id", 0) or 0)
        except (TypeError, ValueError):
            continue
        if existing_id != planet_id:
            planets.append(item)
    if owned and planet_id > 0 and sector_id > 0:
        planets.insert(0, {
            "planet_id": planet_id, "sector_id": sector_id,
            "owner_type": owner_type, "owner_id": owner_id,
            "name": str(planet.get("name") or "")[:64],
            "confirmed_at": time.time(),
        })
    state_manager.set("owned_planets", planets[:20])

# --- Game Connection Class ---
class GameConnection:
    """Handles the low-level socket connection to the game server."""
    
    def __init__(self, host, port, state_manager=None):
        self.host = host
        self.port = port
        self.state_manager = state_manager
        self.sock = None
        self.buffer = b""
        self._send_buffer = bytearray()
        self.max_frame_bytes = 1024 * 1024
        self.disconnected_since_poll = False

    def _record_connection_event(self, metric, reason=None):
        if self.state_manager is not None:
            self.state_manager.record_ai_metric(metric)
        logger.info("BOT_CONNECTION event=%s reason=%s", metric, reason or "unspecified")

    def connect(self):
        """Establishes connection to the server."""
        try:
            self.disconnect(reason="client_replaced")
            sock = socket.create_connection((self.host, self.port), timeout=5.0)
            sock.setblocking(False)
            self.sock = sock
            self._record_connection_event("tcp_connections_created")
            logger.info("Successfully connected to game server at %s:%s", self.host, self.port)
            return True
        except socket.error as e:
            self._record_connection_event("tcp_connect_failures", type(e).__name__)
            self._record_connection_event("network_exceptions", type(e).__name__)
            logger.critical("Failed to connect to game server: %s", e)
            self.sock = None
            return False

    def disconnect(self, reason="client_close"):
        """Closes the socket connection."""
        if self.sock:
            self.disconnected_since_poll = True
            self._record_connection_event("tcp_connections_closed", reason)
            if reason == "server_close":
                self._record_connection_event("server_initiated_disconnects", reason)
            else:
                self._record_connection_event("client_initiated_disconnects", reason)
            try:
                self.sock.close()
                logger.info("Disconnected from game server; reason=%s.", reason)
            except socket.error as e:
                self._record_connection_event("network_exceptions", type(e).__name__)
                logger.error("Error while disconnecting: %s", e)
        self.sock = None
        self.buffer = b""
        self._send_buffer.clear()

    def consume_disconnect(self):
        disconnected = self.disconnected_since_poll
        self.disconnected_since_poll = False
        return disconnected

    def flush_writes(self):
        """Flush queued frames without blocking the decision loop."""
        if not self.sock or not self._send_buffer:
            return bool(self.sock)
        try:
            _readable, writable, _exceptional = select.select([], [self.sock], [self.sock], 0)
            if self.sock in _exceptional:
                raise OSError("socket became unwritable")
            if not writable:
                return True
            sent = self.sock.send(self._send_buffer)
            if sent <= 0:
                raise ConnectionError("socket closed while sending")
            del self._send_buffer[:sent]
            return True
        except (OSError, ValueError) as e:
            logger.error("Socket error while sending queued data: %s", e)
            self._record_connection_event("network_exceptions", type(e).__name__)
            self.disconnect(reason="network_exception")
            return False

    def send_command(self, command_dict):
        """Sends a JSON command to the server, suffixed with a newline."""
        if not self.sock:
            logger.warning("Not connected. Cannot send command.")
            return False
        
        try:
            message = json.dumps(command_dict, separators=(",", ":")) + "\n"
            self._send_buffer.extend(message.encode("utf-8"))
            return self.flush_writes()
        except socket.error as e:
            logger.error("Error sending command: %s. Reconnecting.", e)
            self._record_connection_event("network_exceptions", type(e).__name__)
            self.disconnect(reason="network_exception")
            return False
        except Exception as e:
            logger.error("Unexpected error in send_command: %s", e)
            return False

    def receive_responses(self):
        """Receives data, handles buffering, and yields complete JSON responses."""
        if not self.sock:
            return []

        try:
            self.flush_writes()
            closed = False
            while self.sock:
                try:
                    data = self.sock.recv(4096 * 8)
                except (BlockingIOError, InterruptedError):
                    break
                except socket.error as e:
                    if e.errno in (errno.EWOULDBLOCK, errno.EAGAIN):
                        break
                    raise
                if not data:
                    logger.warning("Server closed the connection.")
                    closed = True
                    break
                self.buffer += data
                if len(self.buffer) > self.max_frame_bytes and b"\n" not in self.buffer:
                    raise ValueError("server frame exceeded configured limit")
        except socket.error as e:
            if e.errno in (errno.EWOULDBLOCK, errno.EAGAIN):
                # No data available right now, this is normal for non-blocking
                return []
            logger.error("Socket error on receive: %s. Disconnecting.", e)
            self._record_connection_event("network_exceptions", type(e).__name__)
            self.disconnect(reason="network_exception")
            return []
        except ValueError as e:
            logger.error("Invalid server frame: %s. Disconnecting.", e)
            self._record_connection_event("invalid_server_frames", type(e).__name__)
            self.disconnect(reason="invalid_server_frame")
            return []

        # Process the buffer and extract complete JSON messages
        responses = []
        while b'\n' in self.buffer:
            response_str, self.buffer = self.buffer.split(b'\n', 1)
            response_str = response_str.strip()
            if response_str:
                if len(response_str) > self.max_frame_bytes:
                    logger.error("Server frame exceeded configured limit; disconnecting.")
                    self.disconnect(reason="invalid_server_frame")
                    return responses
                try:
                    response_json = json.loads(response_str)
                    responses.append(response_json)
                except (json.JSONDecodeError, UnicodeDecodeError) as e:
                    logger.error("Failed to decode JSON response: %s", e)
                    logger.debug("Discarded malformed server frame (%d bytes).", len(response_str))

        if closed:
            self.disconnect(reason="server_close")
        elif len(self.buffer) > self.max_frame_bytes:
            logger.error("Server frame exceeded configured limit; disconnecting.")
            self._record_connection_event("invalid_server_frames", "oversized_frame")
            self.disconnect(reason="invalid_server_frame")
        
        return responses


def _start_llm_keepalive(game_conn, state_manager, config):
    """Keep this authenticated socket alive during a slow synchronous inference."""
    stop = threading.Event()
    interval = max(1.0, float(config.get("llm_keepalive_interval_seconds", 4)))

    def heartbeat():
        sent = 0
        while not stop.wait(interval):
            if not game_conn.sock:
                continue
            token = state_manager.get("session_id")
            if not token:
                continue
            request_id = f"c-llm-ping-{uuid.uuid4().hex[:12]}"
            command = {
                "id": request_id,
                "command": "session.ping",
                "data": {},
                "meta": {
                    "client_version": config.get("client_version"),
                    "session_token": token,
                },
            }
            if game_conn.send_command(command):
                sent += 1
                state_manager.record_ai_metric("llm_keepalives_sent")
                if sent == 1:
                    logger.debug("Started session keepalives during Ollama inference.")

    thread = threading.Thread(target=heartbeat, name="ollama-session-keepalive", daemon=True)
    thread.start()

    def stop_and_join():
        stop.set()
        thread.join(timeout=2)

    return stop_and_join

# --- Strategy & Goal Functions ---

STRATEGIC_ACTION_ORDER = (
    "move.warp", "move.transwarp", "move.pathfind", "sector.info", "sector.scan",
    "sector.scan.density", "trade.port_info", "trade.quote", "trade.buy", "trade.sell",
    "hardware.list", "hardware.buy", "dock.status", "shipyard.list", "shipyard.upgrade", "ship.repair",
    "planet.genesis_create", "planet.land", "planet.info", "planet.harvest",
    "planet.colonists.allocate", "planet.fighters.transfer", "citadel.build",
    "citadel.upgrade", "corp.create", "corp.list", "corp.invite", "corp.join",
    "corp.member.transfer", "combat.attack", "combat.fire_photon",
    "combat.attack.ship", "combat.attack.port", "combat.deploy_fighters",
    "combat.deploy_mines", "ship.info", "ship.status", "ship.inspect",
    "ship.repair", "ship.upgrade", "shipyard.list", "shipyard.upgrade",
    "ferengi.traders",
    "ferengi.deal.accept", "ferengi.deal.reject", "bank.deposit", "bank.withdraw",
)


def _role_goal_options(role_name, game_state, filtered_state, config):
    """Expose only goals the current role can actually attempt from observed state."""
    role = str(role_name or config.get("role_name") or config.get("behavior_profile") or "").strip().lower()
    player = (game_state.get("player_info") or {}).get("player") or {}
    sector_id = game_state.get("player_location_sector")
    sector = (game_state.get("sector_data") or {}).get(str(sector_id), {}) or {}
    commands = set(game_state.get("server_commands") or [])

    # Ship recovery is role-independent. A lost ship must not leave a bot's
    # saved pirate, trade, or planet plan issuing actions from an escape pod.
    recovery_check = getattr(Planner, "_needs_ship_recovery", None)
    if callable(recovery_check) and recovery_check(game_state):
        return ["shipyard: recover"]

    def supports(*names):
        return not commands or any(name in commands for name in names)

    if role in ("coloniser", "industrialist"):
        ship = game_state.get("ship_info") or {}
        genesis = int(ship.get("genesis_torpedoes", ship.get("genesis", 0)) or 0)
        max_genesis = ship.get("max_genesis")
        if max_genesis is not None:
            try:
                if int(max_genesis) < 1:
                    return ["shipyard: recover"]
            except (TypeError, ValueError):
                return []
        if (game_state.get("shipyard_recovery_pending_refresh")
                or (int(ship.get("holds", 0) or 0) <= 0 and supports("shipyard.list", "shipyard.upgrade"))):
            return ["shipyard: recover"]
        owned_planets = game_state.get("owned_planets") or []
        owned_sectors = {str(item.get("sector_id")) for item in owned_planets
                         if isinstance(item, dict) and item.get("sector_id") is not None}
        if (ship.get("onplanet") or str(sector_id) in owned_sectors
                or (int(sector_id or 0) == 1 and bool(owned_planets))):
            return ["planet: develop"] if supports(
                "planet.land", "planet.colonists.get", "planet.colonists.set",
                "planet.harvest", "planet.colonists.allocate") else []
        if owned_planets:
            ship_colonists = Planner._ship_colonist_count(ship)
            if ship_colonists > 0:
                target = min(owned_planets, key=lambda item: abs(
                    int(item.get("sector_id", 1)) - int(sector_id or 1)))
                return [f"goto: {int(target['sector_id'])}"]
            return ["goto: 1"]
        options = []
        if (genesis and sector.get("is_msl") is False and not sector.get("has_planet")
                and int(sector_id or 0) > 10
                and supports("planet.genesis_create", "planet.create")):
            options.append("planet: create")
        elif (not genesis and supports("hardware.list", "hardware.buy")
              and (filtered_state.get("current_port") or filtered_state.get("known_port_sector_ids"))):
            options.append("hardware: genesis")
        elif genesis and sector.get("has_planet"):
            empty_targets = [sid for sid, details in (game_state.get("sector_data") or {}).items()
                             if isinstance(details, dict) and not details.get("has_planet")]
            if empty_targets:
                try:
                    options.append(f"goto: {int(empty_targets[0])}")
                except (TypeError, ValueError):
                    pass
        for known_id, details in (game_state.get("sector_data") or {}).items():
            if isinstance(details, dict) and not details.get("has_planet") and int(known_id or 0) > 10:
                try:
                    if genesis:
                        options.append(f"goto: {int(known_id)}")
                except (TypeError, ValueError):
                    continue
        return list(dict.fromkeys(options[:8])) or (["scan: density"] if supports("sector.scan.density") else [])
    if role == "corporation founder":
        if bool(game_state.get("corporation_transfer_completed")):
            return []
        if not int(player.get("corp_id", 0) or 0):
            return ["corp: found"]
        if bool(game_state.get("corporation_invitation_sent")):
            return ["corp: operate"]
        # The executor's single multi-step goal creates first, then invites the
        # configured partner once the server reports this bot's corp_id.
        return ["corp: found"]
    if role == "corporate operative":
        if bool(game_state.get("corporation_transfer_completed")):
            return []
        return ["corp: join" if not int(player.get("corp_id", 0) or 0) else "corp: operate"]
    if role in ("pirate", "bounty hunter"):
        ship = game_state.get("ship_info") or {}
        try:
            required_fighters = max(1, int(config.get("combat_min_fighters", 10)))
            required_shields = max(0, int(config.get("combat_min_shields", 5)))
            if ((ship.get("max_fighters") is not None
                 and int(ship["max_fighters"]) < required_fighters)
                    or (ship.get("max_shields") is not None
                        and int(ship["max_shields"]) < required_shields)):
                return ["shipyard: recover"]
        except (TypeError, ValueError):
            return []
        # Do not offer combat to an unarmed hull. The planner's survival path
        # seeks legal hardware and exposes combat again only after an
        # authoritative ship snapshot confirms that equipment was installed.
        try:
            if (int(ship.get("fighters", 0) or 0) < required_fighters
                    or int(ship.get("shields", 0) or 0) < required_shields):
                return []
        except (TypeError, ValueError):
            return []
        try:
            protected_here = 1 <= int(sector_id) <= 10
        except (TypeError, ValueError):
            protected_here = False
        if protected_here:
            exit_goal = filtered_state.get("nearest_known_unprotected_sector")
            if exit_goal is not None:
                return [f"goto: {int(exit_goal)}"]
            # No known route out yet: discover only the current frontier until
            # the public map exposes a legal sector outside the safe zone.
            return [f"goto: {sid}" for sid in
                    filtered_state.get("unexplored_adjacent_sector_ids", [])[:4]] or (
                        ["scan: density"] if supports("sector.scan.density") else [])
        threat_threshold = float(config.get("combat_target_threat_threshold", 25.0))
        photons = int((filtered_state.get("ship") or {}).get("photons", 0) or 0)
        adjacent_targets = eligible_combat_targets(
            filtered_state.get("adjacent_ship_targets"), game_state.get("opponent_model"),
            threat_threshold,
        )
        if photons > 0 and adjacent_targets and supports("combat.fire_photon"):
            return ["combat: photon"]
        targets = filtered_state.get("visible_ship_targets") or []
        if eligible_combat_targets(targets, game_state.get("opponent_model"), threat_threshold) and supports("combat.attack", "combat.attack.ship"):
            return ["combat: attack"]
        search = [f"goto: {sid}" for sid in filtered_state.get("unexplored_adjacent_sector_ids", [])[:4]]
        if supports("sector.scan.density"):
            search.append("scan: density")
        return search
    if role == "explorer":
        options = [f"goto: {sid}" for sid in
                   filtered_state.get("unexplored_adjacent_sector_ids", [])[:4]]
        if supports("sector.scan.density"):
            options.append("scan: density")
        return options
    if role in ("trader", "merchant"):
        options = []
        cargo_codes = [item.get("comm") for item in filtered_state.get("ship", {}).get("cargo", [])]
        options.extend(f"sell: {code}" for code in cargo_codes if code)
        current_port = filtered_state.get("current_port") or {}
        options.extend(f"buy: {item.get('code')}" for item in current_port.get("comms", [])
                       if item.get("code") and item.get("buy") is not None)
        options.extend(f"goto: {sid}" for sid in filtered_state.get("known_port_sector_ids", [])[:4])
        if not options:
            options.extend(f"goto: {sid}" for sid in filtered_state.get("valid_goto_sectors", [])[:2])
        return options[:8] or ["scan: density"]
    # Opportunists can choose from actual current opportunities; do not tell
    # other roles to default to trading and exploration.
    options = []
    player = ((game_state.get("player_info") or {}).get("player") or {})
    planet = game_state.get("planet_info") or {}
    try:
        player_id = int(player.get("player_id", player.get("id", 0)) or 0)
        corp_id = int(player.get("corp_id", 0) or 0)
        owner_id = int(planet.get("owner_id", 0) or 0)
        planet_sector = int(planet.get("sector_id", sector_id) or 0)
    except (TypeError, ValueError):
        player_id = corp_id = owner_id = planet_sector = 0
    owner_type = str(planet.get("owner_type", "")).lower()
    try:
        current_sector_number = int(sector_id or 0)
    except (TypeError, ValueError):
        current_sector_number = 0
    has_owned_world_here = False
    for item in game_state.get("owned_planets") or []:
        if not isinstance(item, dict):
            continue
        try:
            item_sector = int(item.get("sector_id", 0) or 0)
            item_owner = int(item.get("owner_id", 0) or 0)
        except (TypeError, ValueError):
            continue
        item_type = str(item.get("owner_type", "")).lower()
        if (item_sector == current_sector_number
                and ((item_type == "player" and player_id > 0 and item_owner == player_id)
                     or (item_type in ("corp", "corporation") and corp_id > 0
                         and item_owner == corp_id))):
            has_owned_world_here = True
            break
    has_owned_world_here = has_owned_world_here or (
        bool((game_state.get("ship_info") or {}).get("onplanet"))
        and planet_sector == current_sector_number
        and ((owner_type == "player" and player_id > 0 and owner_id == player_id)
             or (owner_type in ("corp", "corporation") and corp_id > 0 and owner_id == corp_id))
    )
    if has_owned_world_here:
        options.append("planet: develop")
    if eligible_combat_targets(filtered_state.get("visible_ship_targets"),
                               game_state.get("opponent_model"),
                               float(config.get("combat_target_threat_threshold", 25.0))):
        options.append("combat: attack")
    if filtered_state.get("current_port"):
        options.extend(("buy: ORE", "sell: ORE"))
    return options[:8] or ["scan: density"]


def _role_action_inventory(role_name, commands):
    """Keep the LLM's protocol vocabulary small and executable for its role."""
    role = str(role_name or "").strip().lower()
    if role in ("coloniser", "industrialist"):
        prefixes = ("planet.", "citadel.", "hardware.", "shipyard.", "move.", "sector.")
        extra = {"dock.status", "ship.claim", "ship.info", "ship.status", "ship.repair", "player.my_info"}
    elif role in ("corporation founder", "corporate operative"):
        prefixes = ("corp.", "move.", "sector.")
        extra = {"player.list_online", "player.my_info", "bank.deposit", "bank.balance"}
    elif role in ("pirate", "bounty hunter"):
        prefixes = ("combat.", "photon.", "move.", "sector.", "ship.", "hardware.")
        extra = {"ship.info", "ship.status", "player.my_info"}
    elif role in ("trader", "merchant"):
        prefixes = ("trade.", "port.", "move.", "bank.", "ship.")
        extra = {"sector.info", "player.my_info", "ship.info", "ship.status"}
    elif role == "explorer":
        prefixes = ("move.", "sector.")
        extra = {"player.my_info", "ship.info", "player.computer.recommend_routes"}
    else:
        return list(commands)
    return [command for command in commands
            if command in extra or command.startswith(prefixes)]

def _get_filtered_game_state_for_llm(game_state, state_manager):
    """
    Creates a MINIMAL version of the game state for the LLM to prevent CPU stall and timeouts.
    Includes only critical decision-making data.
    """
    filtered_state = {}

    current_sector_id = game_state.get("player_location_sector")
    filtered_state["current_sector"] = current_sector_id
    
    # Player & Ship (Essential only)
    p_obj = (game_state.get("player_info") or {}).get("player", {})
    s_obj = game_state.get("ship_info", {})
    
    filtered_state["player"] = {
        "credits": p_obj.get("credits"),
        "alignment": p_obj.get("alignment"),
        "turns": p_obj.get("turns_remaining")
    }
    
    cargo_summary = []
    if s_obj and "cargo" in s_obj:
        for item in s_obj["cargo"]:
            if item.get("quantity", 0) > 0:
                cargo_summary.append({
                    "comm": item["commodity"],
                    "qty": item["quantity"],
                    "buy_price": item.get("purchase_price"),
                    "origin": item.get("origin_port_id")
                })

    filtered_state["ship"] = {
        "holds_free": max(0, int(s_obj.get("holds", 0)) - sum(i.get("quantity", 0) for i in s_obj.get("cargo", []))),
        "cargo": cargo_summary,
        "fighters": s_obj.get("fighters", 0),
        "shields": s_obj.get("shields", 0),
        "photons": s_obj.get("photons", 0),
        "genesis_torpedoes": s_obj.get("genesis_torpedoes", s_obj.get("genesis", 0)),
        "on_planet": bool(s_obj.get("onplanet")),
    }
    filtered_state["player"]["corporation_id"] = p_obj.get("corp_id", 0)

    # Current Location Details
    all_port_info = game_state.get("port_info_by_sector", {})
    curr_port = all_port_info.get(str(current_sector_id))
    
    if curr_port:
        port_id_str = str(curr_port.get("port_id"))
        price_cache = game_state.get("price_cache", {}).get(port_id_str, {})
        
        commodities = []
        for c in curr_port.get("commodities", []):
            code = canon_commodity(c.get("commodity"))
            commodities.append({
                "code": code,
                "buy": price_cache.get("buy", {}).get(code),
                "sell": price_cache.get("sell", {}).get(code)
            })
        filtered_state["current_port"] = {"id": curr_port.get("port_id"), "comms": commodities}
    
    # Navigation hints (just IDs, no details)
    sector_data = game_state.get("sector_data", {}).get(str(current_sector_id), {})
    adj = sector_data.get("adjacent") or sector_data.get("adjacent_sectors") or []
    adjacent_ids = []
    for item in adj:
        value = item.get("to_sector", item.get("to_sector_id")) if isinstance(item, dict) else item
        try:
            adjacent_ids.append(int(value))
        except (TypeError, ValueError):
            continue
    filtered_state["adjacent_sectors"] = adjacent_ids

    # Only expose map knowledge already held by this player. Without these
    # distinctions the model can repeatedly route between familiar sectors
    # while believing each destination is unexplored.
    known_sector_ids = set()
    for sector_id in game_state.get("sector_data", {}):
        try:
            known_sector_ids.add(int(sector_id))
        except (TypeError, ValueError):
            continue
    try:
        if current_sector_id is not None:
            known_sector_ids.add(int(current_sector_id))
    except (TypeError, ValueError):
        pass
    known_port_sectors = set()
    for sector_id, port in all_port_info.items():
        if isinstance(port, dict) and port:
            try:
                known_port_sectors.add(int(sector_id))
            except (TypeError, ValueError):
                continue
    for sector_id, details in (game_state.get("sector_data") or {}).items():
        if isinstance(details, dict) and details.get("has_port"):
            try:
                known_port_sectors.add(int(sector_id))
            except (TypeError, ValueError):
                continue
    filtered_state["known_sector_ids"] = sorted(known_sector_ids)
    filtered_state["known_port_sector_ids"] = sorted(known_port_sectors)
    filtered_state["all_known_sectors"] = sorted(known_sector_ids)
    warp_blacklist = {
        str(sector) for sector in state_manager.get("warp_blacklist", [])
    }
    filtered_state["valid_goto_sectors"] = [
        sector_id for sector_id in adjacent_ids if str(sector_id) not in warp_blacklist
    ]
    filtered_state["current_sector_opportunities"] = {
        "has_planet": bool(sector_data.get("has_planet")),
        "has_port": bool(sector_data.get("has_port")),
        "protected": bool(sector_data.get("protected") or sector_data.get("fedspace")),
    }
    own_ship_id = s_obj.get("ship_id", s_obj.get("id", p_obj.get("ship_id")))
    visible_ships = sector_data.get("ships_present") or sector_data.get("ships") or []
    filtered_state["visible_ship_targets"] = [
        {key: ship[key] for key in ("ship_id", "id", "player_id", "name", "is_npc",
                                    "is_derelict", "owner_type", "hostile", "is_hostile",
                                    "alignment") if key in ship}
        for ship in visible_ships
        if isinstance(ship, dict)
        and str(ship.get("ship_id", ship.get("id", ""))) != str(own_ship_id)
    ][:8]
    adjacent_targets = []
    for adjacent_sector in adjacent_ids:
        details = (game_state.get("sector_data") or {}).get(str(adjacent_sector), {}) or {}
        for ship in details.get("ships_present") or details.get("ships") or []:
            if not isinstance(ship, dict):
                continue
            ship_id = ship.get("ship_id", ship.get("id"))
            if ship_id is None or str(ship_id) == str(own_ship_id) or ship.get("is_derelict"):
                continue
            adjacent_targets.append({
                "ship_id": ship_id, "sector_id": int(adjacent_sector),
                "player_id": ship.get("player_id"), "is_npc": ship.get("is_npc"),
                "hostile": ship.get("hostile"), "is_hostile": ship.get("is_hostile"),
                "shields": ship.get("shields"), "owner_type": ship.get("owner_type"),
            })
            if len(adjacent_targets) >= 8:
                break
        if len(adjacent_targets) >= 8:
            break
    filtered_state["adjacent_ship_targets"] = adjacent_targets
    filtered_state["unexplored_adjacent_sector_ids"] = [
        sector_id for sector_id in adjacent_ids if sector_id not in known_sector_ids
    ]
    if current_sector_id is not None and hasattr(state_manager, "find_path"):
        external = [sid for sid in known_sector_ids if sid > 10]
        ranked = []
        for target in external[:60]:
            try:
                path = state_manager.find_path(int(current_sector_id), int(target))
                if isinstance(path, list) and path:
                    ranked.append((len(path), int(target)))
            except (TypeError, ValueError):
                continue
        if ranked:
            filtered_state["nearest_known_unprotected_sector"] = min(ranked)[1]
    
    # Global Market Intelligence (Top 3 profitable routes only)
    market_intel = game_state.get("market_intelligence", {})
    # (Simplified pass-through of pre-calculated intel)
    filtered_state["market_top_prices"] = market_intel

    filtered_state["last_action_result"] = game_state.get("last_action_result")
    filtered_state["valid_comms"] = state_manager.get("valid_commodity_names")

    return filtered_state


def _audit_state_snapshot(game_state):
    """Small before-state record for live gameplay outcome verification."""
    return {
        "player_location_sector": game_state.get("player_location_sector"),
        # StateManager merges player/ship responses in place. Deep-copy them
        # so a later authoritative readback cannot rewrite the before snapshot.
        "player_info": copy.deepcopy(game_state.get("player_info")),
        "ship_info": copy.deepcopy(game_state.get("ship_info")),
    }


def build_registration_command(config, idempotency_key):
    """Build the existing public auth.register request for explicit opt-in."""
    username = config.get("player_username")
    data = {
        "username": username,
        "passwd": config.get("player_password"),
        "ship_name": config.get("registration_ship_name", f"{username}'s ship"),
        "ui_locale": config.get("ui_locale", "en_GB"),
        "ui_timezone": config.get("ui_timezone", "Europe/London"),
    }
    return {
        "id": f"c-{idempotency_key[:8]}",
        "command": "auth.register",
        "data": data,
        "meta": {
            "client_version": config.get("client_version"),
            "idempotency_key": idempotency_key,
        },
    }


def align_command_idempotency(command_name, data_payload, generated_key):
    """Return the envelope key, keeping trade copies identical."""
    if command_name in ("trade.buy", "trade.sell"):
        key = str(data_payload.get("idempotency_key") or generated_key)
        data_payload["idempotency_key"] = key
        return key
    return generated_key

PROMPT_CONTRACT_BLOCK = """
State: {game_state}

Choose actions that advance the stated role objective and current progress.
Use only available_actions and legal goals supported by the observation.
Preserve the turn reserve for role-priority goals; remaining turns are shown.
Avoid repeating a failed goal unless the failure reason or relevant state changed.
Avoid movement without a concrete destination or gameplay purpose.
Trade only listed commodities and only when the current port supports that side
of the trade. Sell carried cargo when the port buys it at a viable price.

Output JSON with one to three purposeful goals, for example
{{"plan": ["planet: create"]}} or {{"plan": ["goto: 5", "buy: ORE"]}}.
"""

PROMPT_EXPLORE = """Explorer goal: Find new sectors and ports. """ + PROMPT_CONTRACT_BLOCK

PROMPT_STRATEGY = """Trader goal: Maximize profit. If you have cargo, sell it first. Then identify new buy/sell opportunities. """ + PROMPT_CONTRACT_BLOCK

PROMPT_QA_OBJECTIVE = """You are a QA testing bot for a space game. Your current high-level objective is to "{qa_objective}".
Based on this objective, provide a detailed strategic plan.
"""

# Deterministic workflow-based QA testing (replaces vague LLM objectives)
# Each workflow is a sequence of commands to execute in order
QA_WORKFLOWS = {
    "planet": [
        "player:info",
        "ship:status",
        "sector:info",
        "planet:list",
        "planet:create",
        "move:pathfind",
        "move:warp",
        "planet:land",
        "planet:info",
        "planet:harvest",
        "planet:launch",
        "move:result"
    ],
    "combat": [
        "player:info",
        "ship:status",
        "sector:info",
        "ship:scan",
        "combat:attack",
        "combat:deploy_fighters",
        "combat:target_lock",
        "combat:fire_weapon",
        "move:result",
        "ship:status"
    ],
    "corp": [
        "player:info",
        "corp:list",
        "corp:create",
        "corp:info",
        "corp:invite",
        "corp:accept_invite",
        "corp:member_list",
        "corp:storage",
        "corp:disband",
        "player:info"
    ],
    "stock": [
        "player:info",
        "bank:balance",
        "stock:list",
        "stock:quote",
        "stock:buy",
        "stock:portfolio",
        "stock:sell",
        "bank:balance",
        "player:info"
    ],
    "mail": [
        "player:info",
        "mail:list",
        "mail:send",
        "mail:read",
        "mail:delete",
        "chat:broadcast",
        "chat:list",
        "player:info"
    ],
    "banking": [
        "player:info",
        "bank:balance",
        "bank:deposit",
        "bank:withdraw",
        "bank:loan_request",
        "bank:loan_list",
        "bank:loan_pay",
        "bank:balance",
        "player:info"
    ],
    "bounty": [
        "player:info",
        "ship:status",
        "bounty:list",
        "bounty:info",
        "bounty:claim",
        "bounty:hunt_log",
        "move:result",
        "player:info"
    ],
    "trade": [
        "player:my_info",
        "ship:status",
        "sector:info",
        "trade:port_info",
        # Repeat quote/buy/sell cycle 3 times to maximize profit opportunities
        "trade:quote", "trade:buy", "trade:sell",
        "trade:quote", "trade:buy", "trade:sell",
        "trade:quote", "trade:buy", "trade:sell"
    ]
}

# Normative V3 ordinary-player request inventory used by the exhaustive QA
# rotation.  Response/event types, engine commands, and SysOp/S2S operations
# are intentionally excluded.  Availability is still checked against the
# server's runtime command catalogue before a request is sent.
V3_ORDINARY_PLAYER_COMMANDS = [
    "player.my_info", "player.list_online", "player.rankings",
    "player.computer.recommend_routes", "player.get_settings", "player.get_prefs",
    "player.set_prefs", "player.get_subscriptions", "player.get_topics",
    "player.set_subscriptions", "player.set_topics", "notes.set", "notes.delete",
    "notes.list", "fine.list", "fine.pay", "session.ping",
    "sector.info", "sector.scan", "sector.scan.density", "sector.search",
    "sector.set_beacon", "move.describe_sector", "move.scan", "move.warp", "move.transwarp",
    "move.pathfind", "move.intercept", "move.autopilot.start", "move.autopilot.status",
    "move.autopilot.control", "move.autopilot.stop", "nav.bookmark.add",
    "nav.bookmark.remove", "nav.bookmark.list", "nav.avoid.add", "nav.avoid.remove",
    "nav.avoid.list", "port.dock", "dock.status", "trade.buy", "trade.sell",
    "trade.offer", "trade.accept", "trade.cancel", "hardware.list", "hardware.buy",
    "ship.status", "ship.info", "ship.inspect", "ship.rename", "ship.repair",
    "ship.upgrade", "ship.transfer_cargo", "ship.jettison", "combat.status",
    "deploy.fighters.list", "deploy.mines.list", "fighters.recall", "mines.recall",
    "ship.tow.status", "ship.tow", "shipyard.list", "shipyard.upgrade",
    "combat.attack.ship", "combat.attack_planet", "combat.attack.port",
    "planet.land", "planet.launch", "planet.info", "planet.fighters.transfer",
    "planet.rename", "planet.transfer_ownership", "planet.harvest", "planet.genesis",
    "planet.deposit", "planet.withdraw", "planet.colonists.get",
    "planet.colonists.set", "planet.colonists.allocate", "planet.genesis_create",
    "planet.view", "planet.market.sell", "planet.market.buy_order",
    "citadel.build", "citadel.upgrade", "corp.create", "corp.list", "corp.status",
    "corp.invite", "corp.join", "corp.roster", "corp.kick", "corp.leave",
    "corp.transfer_ceo", "corp.balance", "corp.statement",
    "corp.deposit", "corp.withdraw", "corp.set_tax", "corp.issue_dividend",
    "bank.balance", "bank.statement", "bank.deposit", "bank.withdraw", "bank.transfer",
    "bank.history", "bank.leaderboard", "bank.orders.list", "bounty.list", "bounty.post",
    "trade.history", "trade.jettison", "port.rob", "combat.sweep_mines",
    "player.set_trade_account_preference", "player.get_bookmarks", "player.set_bookmarks",
    "player.get_avoids", "player.set_avoids", "nav.bookmark.set", "nav.avoid.set",
    "news.get_feed", "news.read", "news.mark_feed_read", "subscribe.add",
    "subscribe.remove", "subscribe.list", "subscribe.catalog", "ship.claim",
    "ship.reregister", "shipyard.sell", "insurance.policies_list", "insurance.policies_buy",
    "insurance.claim_file", "fine.list", "trade.offer", "trade.accept", "trade.cancel",
    "notice.list", "notice.ack", "chat.send", "mail.send", "mail.inbox", "mail.read", "mail.delete",
    "insurance.policies.list", "ferengi.traders",
]
QA_WORKFLOWS["v3_inventory"] = list(V3_ORDINARY_PLAYER_COMMANDS)

# Workflow order for round-robin rotation
QA_WORKFLOW_ORDER = ["planet", "combat", "corp", "stock", "mail", "banking", "bounty", "trade", "v3_inventory"]

QA_COMMAND_ALIASES = {
    "player:info": "player.my_info", "player:my_info": "player.my_info",
    "ship:status": "ship.status", "sector:info": "sector.info",
    "planet:list": "planet.list", "move:pathfind": "move.pathfind",
    "move:warp": "move.warp", "planet:land": "planet.land",
    "planet:info": "planet.info", "planet:launch": "planet.launch",
    "trade:port_info": "trade.port_info", "trade:quote": "trade.quote",
    "trade:buy": "trade.buy", "trade:sell": "trade.sell",
    "bank:balance": "bank.balance", "bank:deposit": "bank.deposit",
    "bank:withdraw": "bank.withdraw",
}
QA_RESPONSE_ONLY = {"move:result"}


def resolve_qa_command(label: str, server_commands=None):
    """Resolve a workflow label to an advertised request command."""
    if label in QA_RESPONSE_ONLY:
        return None
    command = QA_COMMAND_ALIASES.get(label, label.replace(":", "."))
    if server_commands is not None and command not in set(server_commands):
        return None
    return command


def validate_config(config: dict) -> list[str]:
    """Return actionable startup configuration errors without side effects."""
    if not isinstance(config, dict):
        return ["configuration must be a JSON object"]
    errors = []
    try:
        if not 1 <= int(config.get("game_port", 1234)) <= 65535:
            errors.append("game_port must be between 1 and 65535")
    except (TypeError, ValueError):
        errors.append("game_port must be an integer")
    if config.get("strategy_mode", "deterministic") not in {"deterministic", "ollama"}:
        errors.append("strategy_mode must be 'deterministic' or 'ollama'")
    if config.get("population_mode") and config.get("qa_mode"):
        errors.append("population_mode and qa_mode cannot both be enabled")
    for key in ("state_file", "log_file", "bug_report_path"):
        value = config.get(key)
        if value is not None and (not isinstance(value, str) or not value.strip()):
            errors.append(f"{key} must be a non-empty path")
    try:
        if int(config.get("max_retries_per_command", 5)) < 0:
            errors.append("max_retries_per_command must be non-negative")
    except (TypeError, ValueError):
        errors.append("max_retries_per_command must be an integer")
    return errors


def qa_coverage_snapshot(tested_commands, server_commands, scenario_ids=None):
    """Calculate coverage from the current advertised inventory."""
    inventory = sorted(set(server_commands or []))
    tested_input = set(tested_commands or [])
    tested = sorted(tested_input & set(inventory))
    return {
        "inventory_size": len(inventory),
        "tested_count": len(tested),
        "tested_commands": tested,
        "unsupported_tested_commands": sorted(tested_input - set(inventory)),
        "scenario_ids": sorted(set(scenario_ids or [])),
        "coverage_percent": len(tested) / len(inventory) * 100 if inventory else 0.0,
    }

def _pause_for_turn_refresh(state_manager, seconds=3600, heartbeat_interval=4):
    """Yield without invalidating state; the main loop polls reset every 30s."""
    # Keep enough loop opportunities to send a liveness request before the
    # server's idle request deadline, even while the player has no turns.
    pause = max(0.0, min(2.0, float(seconds), max(0.25, float(heartbeat_interval) * 0.5)))
    if pause and not shutdown_flag:
        time.sleep(pause)


def _get_fallback_plan(game_state, state_manager, planner):
    """
    Provides a simple fallback plan when the LLM fails to respond.
    Prioritizes surveying, then selling cargo, then buying if empty, then exploration.
    """
    logger.info("Generating local strategy plan")

    current_sector = game_state.get("player_location_sector")
    sector_data = game_state.get("sector_data", {}).get(str(current_sector), {})

    # Solo players need a persistent win path, not just an endless trade loop.
    # The planner only emits these milestones when the required state is
    # observed, so a refused/unsupported action cannot strand the strategy.
    mission_goal_fn = getattr(planner, "_mission_goal", None)
    mission_goal = mission_goal_fn(game_state) if callable(mission_goal_fn) else None
    if mission_goal:
        return [mission_goal]

    diplomacy_goal_fn = getattr(planner, "_diplomacy_goal", None)
    diplomacy_goal = diplomacy_goal_fn(game_state) if callable(diplomacy_goal_fn) else None
    if diplomacy_goal:
        return [diplomacy_goal]

    fleet_goal_fn = getattr(planner, "_fleet_goal", None)
    fleet_goal = fleet_goal_fn(game_state) if callable(fleet_goal_fn) else None
    if fleet_goal:
        return [fleet_goal]

    endgame_goal_fn = getattr(planner, "_endgame_goal", None)
    endgame_goal = endgame_goal_fn(game_state) if callable(endgame_goal_fn) else None
    if endgame_goal:
        return [endgame_goal]

    readiness_fn = getattr(planner, "_combat_readiness_command", None)
    readiness_action = readiness_fn(game_state) if callable(readiness_fn) else None
    if readiness_action:
        return ["combat: readiness"]

    progression_fn = getattr(planner, "_solo_progression_goal", None)
    progression_goal = progression_fn(game_state) if callable(progression_fn) else None
    if progression_goal:
        return [progression_goal]
    
    # 0. Survey Logic: If we are at a port and haven't surveyed it, DO IT.
    if sector_data.get("has_port"):
        port_info = game_state.get("port_info_by_sector", {}).get(str(current_sector))
        if port_info is None:
            logger.info("Fallback: At an unsurveyed port. Setting goal to SURVEY.")
            return ["survey: port"]

    ship_info = game_state.get("ship_info", {})
    cargo = ship_info.get("cargo", [])
    
    # Filter for sellable items (quantity > 0)
    sellable_items = [item for item in cargo if item.get("quantity", 0) > 0]
    
    # 1. Sell Logic: If we have cargo, find where to sell it
    if sellable_items:
        if sector_data.get("has_port"):
            commodity = planner._get_best_commodity_to_sell(game_state)
            if commodity:
                return [f"sell: {commodity}"]

        cargo_route = planner._find_port_for_cargo(game_state)
        if cargo_route:
            target_sector, commodity = cargo_route
            return [f"goto: {target_sector}", f"sell: {commodity}"]

    # 2. Buy Logic: If we have space and credits, and are at a port, buy something PROFITABLE
    if sector_data.get("has_port"):
        credits = float(game_state.get("player_info", {}).get("player", {}).get("credits", 0))
        holds_free = ship_info.get("holds", 0) - sum(i.get("quantity", 0) for i in cargo)
        
        if credits > 100 and holds_free > 0:
             # Use planner's profit-aware buy logic
             target_comm = planner._get_cheapest_commodity_to_buy(game_state)
             if target_comm:
                 logger.info(f"Fallback: Found profitable buy opportunity: {target_comm}.")
                 return [f"buy: {target_comm}"]
             else:
                 logger.info("Fallback: No profitable buy opportunities at this port.")

    # 3. Intelligence Logic: If empty, go to the nearest unsurveyed port
    unsurveyed_ports = []
    for s_id, p_info in game_state.get("port_info_by_sector", {}).items():
        # Check if we have prices for all commodities at this port
        p_id_str = str(p_info.get("port_id"))
        prices = game_state.get("price_cache", {}).get(p_id_str, {}).get("buy", {})
        if p_info.get("commodities") and not prices: # Empty markets have nothing to survey
            unsurveyed_ports.append(int(s_id))
    
    if unsurveyed_ports:
        target = planner._nearest_known_sector(game_state, unsurveyed_ports)
        if target is not None:
            logger.info(f"Fallback: Moving to nearest unsurveyed port in sector {target}")
            return [f"goto: {target}"]

    # 4. Keep fallback exploration on the planner's known-adjacency route.
    next_warp = getattr(planner, "_get_next_warp_target", None)
    target = next_warp(game_state) if callable(next_warp) else None
    if target is not None:
        logger.info("Fallback: Exploring via adjacent sector %s", target)
        return [f"goto: {target}"]

    valid_gotos = state_manager.get_valid_goto_sectors()
    nearest = getattr(planner, "_nearest_known_sector", None)
    target = nearest(game_state, valid_gotos) if callable(nearest) else None
    if target is not None:
        logger.info("Fallback: Moving to nearest known sector %s", target)
        return [f"goto: {target}"]

    logger.warning("Fallback: No known navigation targets are available.")
    return ["scan: density"]

def _behavior_profile_guidance(profile):
    guidance = {
        "trader": "Prioritize viable market trades and profit from public quotes; sell carried cargo before considering another purchase.",
        "explorer": "Prioritize unexplored reachable sectors and discovering ports; trade when public information shows a worthwhile opportunity.",
        "cautious": "Prefer known, surveyed reachable locations and avoid unnecessary uncertainty; follow normal resource and safety constraints.",
        "opportunist": "Prioritize fresh public route recommendations with positive estimated returns; otherwise explore a reachable frontier.",
        "balanced": "Balance exploration and trading using only the information currently available to this player.",
    }
    normalized = str(profile or "balanced").strip().lower()
    return guidance.get(normalized, guidance["balanced"])


def get_strategy_from_llm(game_state, model, stage, state_manager,
                          qa_objective: Optional[str] = None, bot_number: int = 0,
                          planner: Optional[Planner] = None, ollama_url: Optional[str] = None,
                          inference_diagnostics: Optional[dict] = None,
                          behavior_profile: Optional[str] = None,
                          inference_timeout: Optional[float] = None,
                          role_name: Optional[str] = None,
                          primary_objective: Optional[str] = None,
                          ollama_options: Optional[dict] = None):
    """
    Asks the LLM for a high-level strategic plan.
    """
    # Stagger LLM requests from concurrent bots to avoid thundering herd on Ollama
    # Each bot waits 0.5-2.5 seconds, spread by bot number
    jitter = random.uniform(0.5, 2.5)
    stagger = (bot_number % 10) * 0.3  # Add 0-2.7s based on bot number
    wait_time = jitter + stagger
    if wait_time > 0.1:
        logger.debug(f"Staggering LLM request by {wait_time:.1f}s (bot #{bot_number})")
        time.sleep(wait_time)
    
    # Filter the game state to reduce token count for the LLM
    filtered_game_state = _get_filtered_game_state_for_llm(game_state, state_manager)
    config = getattr(state_manager, "config", {}) or {}
    metrics = state_manager.get("ai_metrics", {}) or {}
    failures = []
    for command, detail in (state_manager.get("command_retry_info", {}) or {}).items():
        if isinstance(detail, dict) and int(detail.get("failures", 0) or 0) > 0:
            failures.append({"command": command, "count": int(detail.get("failures", 0))})
    filtered_game_state["objective"] = {
        "role": role_name or config.get("role_name") or behavior_profile or "pilot",
        "primary": primary_objective or config.get("primary_objective", "make steady V3 gameplay progress"),
        "current_goal": (state_manager.get("strategy_plan") or [None])[0],
        "successful_actions": int(metrics.get("playtest_gameplay_actions", 0) or 0),
        "goals_completed": int(metrics.get("goals_completed", 0) or 0),
        "recent_failures": failures[-3:],
        "last_failure_reason": (state_manager.get("last_action_result") or {}).get("error_msg"),
    }
    try:
        turns = int((filtered_game_state.get("player") or {}).get("turns", 0) or 0)
        reserve = max(0, int(config.get("turn_reserve", 12)))
    except (TypeError, ValueError):
        turns, reserve = 0, 0
    filtered_game_state["turn_budget"] = {
        "remaining": turns, "reserved": min(turns, reserve),
        "general_actions_available": max(0, turns - reserve),
    }
    filtered_game_state["capabilities"] = {
        "on_planet": bool((game_state.get("ship_info") or {}).get("onplanet")),
        "fighters": (game_state.get("ship_info") or {}).get("fighters", 0),
        "shields": (game_state.get("ship_info") or {}).get("shields", 0),
        "photons": (game_state.get("ship_info") or {}).get("photons", 0),
        "genesis_torpedoes": (game_state.get("ship_info") or {}).get(
            "genesis_torpedoes", (game_state.get("ship_info") or {}).get("genesis", 0)),
    }
    command_inventory = game_state.get("server_commands") or []
    command_set = set(command_inventory)
    role_name_value = role_name or config.get("role_name") or behavior_profile
    filtered_game_state["available_actions"] = [
        command for command in STRATEGIC_ACTION_ORDER if command in command_set
    ]
    filtered_game_state["available_actions"] = _role_action_inventory(
        role_name_value, filtered_game_state["available_actions"]
    )
    filtered_game_state["available_goals"] = _role_goal_options(
        role_name_value,
        game_state, filtered_game_state, config
    )
    filtered_game_state["objective_policy"] = {
        "role": role_name or config.get("role_name") or behavior_profile or "pilot",
        "primary": primary_objective or config.get("primary_objective", "make steady V3 gameplay progress"),
        "goal_options": filtered_game_state["available_goals"],
        "progress_metric": "successful server-acknowledged role actions; navigation is tracked separately",
    }
    prompt_observation = dict(filtered_game_state)
    # Exact sector IDs are used locally to reject repeat exploration. Sending
    # the entire map on every decision adds tokens without helping the model.
    for key in ("all_known_sectors", "known_sector_ids"):
        values = prompt_observation.pop(key, [])
        prompt_observation[key.replace("_ids", "_count").replace("all_known_sectors", "known_sector_count")] = len(values)
    prompt_observation["known_port_sector_ids"] = (prompt_observation.get("known_port_sector_ids") or [])[:20]
    prompt_observation["market_top_prices"] = (prompt_observation.get("market_top_prices") or {})
    try:
        if len(prompt_observation["market_top_prices"]) > 3:
            prompt_observation["market_top_prices"] = dict(list(prompt_observation["market_top_prices"].items())[:3])
    except (TypeError, AttributeError):
        prompt_observation["market_top_prices"] = {}
    logger.debug(f"DEBUG: Known sectors passed to LLM: {filtered_game_state.get('all_known_sectors')}")

    # --- FIX 3.1 & 3.2 ---
    # If we are not at a port, we must be exploring.
    if not filtered_game_state.get("can_trade_at_current_location", False) and not qa_objective:
        stage = "explore"
    
    role_intro = (
        f"Role: {role_name or config.get('role_name') or behavior_profile or 'pilot'}. Primary objective: "
        f"{primary_objective or 'make steady progress through V3 gameplay'}.\n"
        "Choose a goal from State.objective_policy.goal_options and use only its listed "
        "available_actions. Do not substitute unrelated navigation or trading for the role "
        "objective. Navigation counts only when it targets a listed prerequisite or opportunity. "
        "If a role goal is blocked, select the listed action that clears its prerequisite, or report no safe action.\n"
    )
    if qa_objective:
        # Pre-format the QA objective into the specific prompt section
        qa_intro = PROMPT_QA_OBJECTIVE.format(qa_objective=qa_objective)
        prompt = role_intro + qa_intro + PROMPT_CONTRACT_BLOCK.format(game_state=json.dumps(prompt_observation, separators=(",", ":")))
        logger.info(f"Using QA Objective prompt for LLM: {qa_objective}")
    elif stage == "explore":
        prompt = (
            role_intro + "Select a concrete role goal; exploration is only a prerequisite when listed.\n"
            f"Behaviour profile: {_behavior_profile_guidance(behavior_profile)}\n"
            + PROMPT_CONTRACT_BLOCK.format(game_state=json.dumps(prompt_observation, separators=(",", ":")))
        )
    else:
        prompt = (
            role_intro + "Select a concrete role goal; do not default to trading or exploration.\n"
            f"Behaviour profile: {_behavior_profile_guidance(behavior_profile)}\n"
            + PROMPT_CONTRACT_BLOCK.format(game_state=json.dumps(prompt_observation, separators=(",", ":")))
        )

    try:
        client_error = {}
        ollama_kwargs = {
            "override_prompt": prompt,
            "endpoint": ollama_url,
            "error_report": client_error,
            "goal_choices": filtered_game_state["available_goals"],
        }
        if ollama_options:
            ollama_kwargs.update(ollama_options)
        if inference_timeout is not None:
            ollama_kwargs["request_timeout"] = inference_timeout
        response_text = get_ollama_response(
            filtered_game_state, model, "PROMPT_STRATEGY", **ollama_kwargs
        )
        
        if not response_text:
            if inference_diagnostics is not None:
                inference_diagnostics.update({
                    "status": client_error.get("status", "failed"),
                    "reason": client_error.get("reason", "Ollama returned no response"),
                })
            return None

        if inference_diagnostics is not None:
            if "latency_seconds" in client_error:
                inference_diagnostics["latency_seconds"] = client_error["latency_seconds"]
            if "model" in client_error:
                inference_diagnostics["model"] = client_error["model"]
            if "profile" in client_error:
                inference_diagnostics["profile"] = client_error["profile"]

        raw_plan = []

        # 1) Robust JSON parsing
        json_response = parse_llm_json(response_text)
        
        if json_response:
            if isinstance(json_response, list):
                for item in json_response:
                    if isinstance(item, str):
                        raw_plan.append(item.strip())
                    elif isinstance(item, list):
                        # Flatten nested list
                        for sub_item in item:
                            if isinstance(sub_item, str):
                                raw_plan.append(sub_item.strip())
                    else:
                        logger.warning(f"Invalid item type in plan list: {type(item)}. Expected string or list.")
            elif isinstance(json_response, dict):
                 # Primary Contract: {"plan": ["goal1", "goal2"]}
                 if "plan" in json_response and isinstance(json_response["plan"], list):
                     for item in json_response["plan"]:
                         if isinstance(item, str):
                             raw_plan.append(item.strip())
                         elif isinstance(item, list):
                             # Flatten nested list
                             for sub_item in item:
                                 if isinstance(sub_item, str):
                                     raw_plan.append(sub_item.strip())
                         elif isinstance(item, dict) and "goal" in item: # Support rich objects {goal: "...", reason: "..."}
                             raw_plan.append(str(item["goal"]).strip())
                 
                 # Fallback: Flat dict {"goto": 123} (Legacy)
                 else:
                    for k, v in json_response.items():
                        key = k.strip()
                        if key in ["goto", "buy", "sell", "scan"] or "." in key:
                            if isinstance(v, (str, int, float)):
                                raw_plan.append(f"{key}: {v}")
                            elif v is None or v == "":
                                raw_plan.append(key)
                            elif isinstance(v, list):
                                for item in v:
                                    raw_plan.append(f"{key}: {item}")
                        elif ":" in key:
                            raw_plan.append(key)
                        elif isinstance(v, list):
                            for item in v:
                                if isinstance(item, str):
                                    raw_plan.append(item.strip())
                        elif isinstance(v, str):
                                raw_plan.append(v.strip())
            else:
                logger.warning(f"LLM returned JSON of type {type(json_response)}, expected list or dict. Raw: {response_text}")
        else:
            logger.warning("LLM returned invalid JSON. Attempting to recover text lines.")
            # Fallback: Try to extract lines that look like goals
            lines = response_text.strip().split('\n')
            for line in lines:
                line = line.strip()
                # Basic cleanup
                line = re.sub(r'^[\d\-\*\.]+\s*', '', line).strip('"').strip("'").strip(',')
                if ":" in line:
                    raw_plan.append(line)

        # 2) Validation & Normalization
        validated_plan = []
        
        # Context for validation
        valid_gotos = set(filtered_game_state.get("valid_goto_sectors", []))
        all_known = set(filtered_game_state.get("all_known_sectors", []))
        known_sector_ids = set(filtered_game_state.get("known_sector_ids", []))
        known_port_sector_ids = set(filtered_game_state.get("known_port_sector_ids", []))
        valid_comms = set(c.upper() for c in filtered_game_state.get("valid_commodity_names", []))
        can_trade = filtered_game_state.get("can_trade_at_current_location", False)
        has_cargo = bool((filtered_game_state.get("ship") or {}).get("cargo"))
        
        goal_pattern = re.compile(r'^(goto|buy|sell|scan|planet|combat|bank|citadel|fleet|corp|diplomacy|hardware|shipyard)\s*:\s*(.+)$', re.IGNORECASE)

        for raw_goal in raw_plan:
            goal = _canonicalize_strategy_goal(
                raw_goal, filtered_game_state.get("available_goals"), game_state
            )
            if not goal:
                logger.debug("Ignoring unsupported model goal/action: %r", raw_goal)
                continue
            
            match = goal_pattern.match(goal)
            if not match:
                # Allow "scan: density" which might match, but double check strictness
                # Actually the regex covers it.
                logger.debug(f"Skipping malformed goal string: {goal}")
                continue

            verb = match.group(1).lower()
            arg = match.group(2).strip()
            
            # Reconstruct goal string
            clean_goal = f"{verb}: {arg}"

            if verb == "goto":
                try:
                    sid = int(arg)
                    # During exploration, do not let an Ollama plan loop back
                    # through already-known non-port sectors. Known ports and
                    # cargo destinations remain valid strategic targets.
                    if (stage == "explore" and not has_cargo
                            and sid in known_sector_ids
                            and sid not in known_port_sector_ids):
                        logger.warning(
                            "Skipping exploration destination %s already known to this player.", sid
                        )
                        continue
                    validated_plan.append(f"goto: {sid}")
                except ValueError:
                    logger.warning(f"Skipping invalid goto format: {goal}")

            elif verb in ("buy", "sell"):
                if not can_trade:
                    logger.warning(f"Skipping trade goal '{clean_goal}': Not at a port/can't trade.")
                    continue
                
                # Handle comma-separated commodities
                commodities = [c.strip().upper() for c in arg.split(",")]
                
                for comm in commodities:
                    if comm in valid_comms:
                        validated_plan.append(f"{verb}: {comm}")
                    else:
                        logger.warning(f"Skipping trade goal '{verb}: {comm}': Invalid commodity '{comm}'")

            elif verb == "scan":
                if "density" in arg.lower():
                    validated_plan.append("scan: density")

            elif verb == "planet":
                if arg.lower() in ("land", "info", "create", "develop", "acquire", "workforce", "defend"):
                    validated_plan.append(f"planet: {arg.lower()}")

            elif verb == "bank":
                if arg.lower() in ("secure", "deposit"):
                    validated_plan.append(f"bank: {arg.lower()}")

            elif verb == "citadel":
                if arg.lower() in ("maintain", "build", "upgrade"):
                    validated_plan.append(f"citadel: {arg.lower()}")

            elif verb == "fleet":
                if arg.lower() in ("assign", "acquire"):
                    validated_plan.append(f"fleet: {arg.lower()}")

            elif verb == "corp":
                if arg.lower() in ("found", "join", "invite", "operate"):
                    validated_plan.append(f"corp: {arg.lower()}")

            elif verb == "diplomacy":
                if arg.lower() == "offer":
                    validated_plan.append("diplomacy: offer")
            
            elif verb == "combat":
                if arg.lower() in ("attack", "photon"):
                     validated_plan.append(f"combat: {arg.lower()}")

            elif verb == "hardware":
                if arg.lower() in ("genesis", "fighters", "photon"):
                    validated_plan.append(f"hardware: {arg.lower()}")

            elif verb == "shipyard":
                if arg.lower() == "recover":
                    validated_plan.append("shipyard: recover")
            
            else:
                logger.debug(f"Skipping unknown verb: {verb}")

        # Enforce only the role-specific goals that are currently actionable.
        # This blocks repeated generic sector travel when a concrete role
        # opportunity or prerequisite is available.
        role_options = set(filtered_game_state.get("available_goals") or [])
        if role_options:
            rejected = [goal for goal in validated_plan if goal not in role_options]
            if rejected:
                logger.info("Role goal filter rejected unsupported choices: %s", rejected)
            validated_plan = [goal for goal in validated_plan if goal in role_options]

        # Deduplicate adjacent identical goals
        final_plan = []
        for g in validated_plan:
            if not final_plan or g != final_plan[-1]:
                final_plan.append(g)

        if final_plan:
            # --- BOREDOM FILTER: Override LLM if it hallucinates a loop ---
            # 1. Check if we just scanned successfully
            last_result = game_state.get("last_action_result", {})
            just_scanned = (last_result and last_result.get("status") == "ok" and 
                            last_result.get("response_type") == "sector.density.scan")

            # 2. Check if the new plan is just "scan again"
            if final_plan[0] == "scan: density" and just_scanned:
                logger.warning("BOREDOM FILTER: LLM tried to scan twice. Forcing a move!")
                target = planner._get_next_warp_target(game_state) if planner else None
                if target is not None and f"goto: {target}" in role_options:
                    if inference_diagnostics is not None:
                        inference_diagnostics.update({"status": "safely_rejected", "reason": "Ollama plan suppressed by repeated-scan safeguard"})
                    return [f"goto: {target}"]
                logger.info("Boredom filter found no known adjacent destination; defer to fallback planning.")
                if inference_diagnostics is not None:
                    inference_diagnostics.update({"status": "safely_rejected", "reason": "Ollama plan suppressed by repeated-scan safeguard; no known adjacent destination"})
                return None
            # ---------------------------------------------------------------

            logger.info(f"LLM provided valid plan: {final_plan}")
            if inference_diagnostics is not None:
                inference_diagnostics.update({
                    "status": "succeeded", "reason": None,
                    "llm_raw_plan": raw_plan[:5], "accepted_plan": final_plan[:5],
                    "decision_observation": {
                        "role": filtered_game_state.get("objective_policy", {}).get("role"),
                        "goal_options": filtered_game_state.get("available_goals", [])[:8],
                        "available_actions": filtered_game_state.get("available_actions", []),
                        "turns": (filtered_game_state.get("player") or {}).get("turns"),
                        "capabilities": filtered_game_state.get("capabilities", {}),
                        "visible_ship_targets": filtered_game_state.get("visible_ship_targets", []),
                    },
                })
            return final_plan
        
        logger.warning(f"LLM response yielded no valid goals after validation. Raw goals: {raw_plan}, response_text: {response_text}")
        if inference_diagnostics is not None:
            inference_diagnostics.update({
                "status": "invalid_response",
                "reason": "Ollama responded, but no strategy goal passed parsing and validation",
            })
        return None

    except Exception as e:
        logger.error(f"Error parsing strategy from LLM response: {e}", exc_info=True)
        if inference_diagnostics is not None:
            inference_diagnostics.update({"status": "failed", "reason": f"Strategy processing failed: {type(e).__name__}"})
        return None


def select_strategy_plan(game_state, config, state_manager, planner):
    """Select a local plan or optionally ask Ollama for high-level strategy."""
    # Strategy planning is also used with small protocol adapters and test
    # planners.  Recovery requirements are optional metadata for those
    # implementations; don't make selecting a strategy depend on a concrete
    # Planner class.
    requirements_reader = getattr(planner, "_recovery_capability_requirements", None)
    requirements = requirements_reader() if callable(requirements_reader) else {}
    game_state["ship_recovery_requirements"] = requirements
    state_manager.set("ship_recovery_requirements", requirements)
    mode = config.get("strategy_mode", "ollama")
    if mode not in ("deterministic", "ollama"):
        raise ValueError(f"Unknown strategy_mode: {mode!r}")
    recovery_check = getattr(planner, "_needs_ship_recovery", None)
    if callable(recovery_check) and recovery_check(game_state):
        state_manager.set("strategy_plan_source", "controller_priority")
        state_manager.set("strategy_inference_status", {
            "status": "preempted", "reason": "shipyard: recover",
        })
        state_manager.record_decision({
            "goal": "shipyard: recover", "reason": "active ship requires recovery",
            "source": "controller",
        })
        return ["shipyard: recover"]
    strict_llm = bool(config.get("require_llm_decisions", False))
    now = time.time()
    retry_after = float(state_manager.get("ollama_retry_after", 0) or 0)
    next_decision_at = float(state_manager.get("ollama_next_decision_at", 0) or 0)
    if mode == "ollama" and max(retry_after, next_decision_at) > now:
        state_manager.set("strategy_plan_source", "ollama_wait")
        return []
    # Safety and victory transitions are controller-owned and must preempt a
    # stale or imaginative LLM plan in every strategy mode.
    endgame_reader = getattr(planner, "_endgame_goal", None)
    readiness_reader = getattr(planner, "_combat_readiness_command", None)
    mission_reader = getattr(planner, "_mission_goal", None)
    diplomacy_reader = getattr(planner, "_diplomacy_goal", None)
    fleet_reader = getattr(planner, "_fleet_goal", None)
    priority_goal = mission_reader(game_state) if callable(mission_reader) and not strict_llm else None
    if priority_goal is None and callable(diplomacy_reader) and not strict_llm:
        priority_goal = diplomacy_reader(game_state)
    if priority_goal is None and callable(fleet_reader) and not strict_llm:
        priority_goal = fleet_reader(game_state)
    if priority_goal is None and callable(endgame_reader) and not strict_llm:
        priority_goal = endgame_reader(game_state)
    if priority_goal is None and callable(readiness_reader) and readiness_reader(game_state) and not strict_llm:
        priority_goal = "combat: readiness"
    if priority_goal:
        state_manager.set("strategy_plan_source", "controller_priority")
        state_manager.set("strategy_inference_status", {"status": "preempted", "reason": priority_goal})
        state_manager.record_decision({"goal": priority_goal, "reason": "controller priority", "source": "controller"})
        return [priority_goal]
    if mode == "deterministic":
        plan = _get_fallback_plan(game_state, state_manager, planner)
        state_manager.set("strategy_plan_source", "deterministic")
        state_manager.set("strategy_inference_status", {"status": "disabled", "reason": None})
        state_manager.record_decision({"goal": plan[0] if plan else None, "reason": "deterministic fallback", "source": "deterministic"})
        return plan
    diagnostics = {}
    try:
        profile = planner._behavior_profile(game_state)
    except (AttributeError, TypeError):
        profile = config.get("behavior_profile", "balanced")
    plan = get_strategy_from_llm(
        game_state,
        config.get("ollama_model"),
        planner.current_stage,
        state_manager,
        planner=planner,
        ollama_url=config.get("ollama_url"),
        inference_diagnostics=diagnostics,
        behavior_profile=profile,
        inference_timeout=config.get("inference_timeout_seconds"),
        role_name=config.get("role_name"),
        primary_objective=config.get("primary_objective"),
        ollama_options={
            "num_ctx": config.get("ollama_num_ctx", 4096),
            "num_predict": config.get("ollama_num_predict", 256),
            "temperature": config.get("ollama_temperature", 0.3),
            "keep_alive": config.get("ollama_keep_alive", "30m"),
            "lock_dir": config.get("ollama_lock_dir", "/tmp/twclone-ollama-slots"),
            "max_concurrent": config.get("ollama_max_concurrent", 2),
        },
    )
    try:
        interval = max(5.0, float(config.get("decision_interval_seconds", 8)))
    except (TypeError, ValueError):
        interval = 8.0
    state_manager.set("ollama_next_decision_at",
                      time.time() + interval * random.uniform(0.8, 1.2))
    state_manager.record_ai_metric("ollama_requests")
    if diagnostics.get("status") not in ("succeeded", "safely_rejected"):
        state_manager.record_ai_metric("ollama_errors")
    if diagnostics.get("profile"):
        state_manager.set("ollama_last_profile", diagnostics["profile"])
    latency = diagnostics.get("latency_seconds")
    if isinstance(latency, (int, float)):
        metrics = state_manager.get("ai_metrics", {})
        previous_count = int(metrics.get("ollama_successes", 0) or 0)
        previous_total = float(metrics.get("ollama_latency_seconds_total", 0) or 0)
        state_manager.set("ollama_last_latency_seconds", latency)
        state_manager.set("ollama_average_latency_seconds",
                          (previous_total + latency) / (previous_count + 1))
        state_manager.record_ai_metric("ollama_successes")
        state_manager.record_ai_metric("ollama_latency_seconds_total", latency)
    if plan and isinstance(plan, list) and all(
        isinstance(goal, str) and ":" in goal for goal in plan
    ):
        if diagnostics.get("status") == "safely_rejected":
            source = "controller_safety_override"
            logger.info("Strategy decision source=%s plan=%s", source, plan)
        else:
            source = "ollama"
            logger.info("Strategy decision source=ollama model=%s plan=%s", config.get("ollama_model"), plan)
    else:
        source = "fallback"
        diagnostics.setdefault("status", "invalid_response")
        diagnostics.setdefault("reason", "Ollama strategy was empty or invalid")
        logger.warning(
            "Strategy decision source=fallback inference_status=%s reason=%s",
            diagnostics["status"], diagnostics["reason"],
        )
        if strict_llm:
            try:
                retry_delay = max(5.0, float(config.get("ollama_retry_seconds", 30)))
            except (TypeError, ValueError):
                retry_delay = 30.0
            state_manager.set("strategy_plan_source", "ollama_wait")
            state_manager.set("strategy_inference_status", diagnostics)
            state_manager.set("ollama_retry_after", time.time() + retry_delay)
            return []
        plan = _get_fallback_plan(game_state, state_manager, planner)
    if plan and diagnostics.get("status") == "succeeded":
        state_manager.set("ollama_retry_after", 0)
        if config.get("playtest_bot") and not state_manager.get("playtest_first_inference_at"):
            stamp = time.time()
            state_manager.set("playtest_first_inference_at", stamp)
            logger.info("PLAYTEST_FIRST_INFERENCE bot=%s endpoint=%s model=%s latency_seconds=%.2f",
                        config.get("playtest_bot"), config.get("ollama_url"), config.get("ollama_model"),
                        float(diagnostics.get("latency_seconds", 0)))
    state_manager.set("strategy_plan_source", source)
    state_manager.set("strategy_inference_status", {
        "status": diagnostics.get("status"), "reason": diagnostics.get("reason")
    })
    state_manager.record_decision({
        "goal": plan[0] if plan else None,
        "reason": diagnostics.get("reason"), "source": source,
        "role": config.get("role_name"),
        "objective": config.get("primary_objective"),
        "llm_raw_plan": diagnostics.get("llm_raw_plan", []),
        "accepted_plan": diagnostics.get("accepted_plan", plan or []),
        "observation": diagnostics.get("decision_observation", {}),
    })
    return plan


def quarantine_timed_out_trades(state_manager, expired_commands):
    """Persist ambiguous mutation outcomes until they can be reconciled."""
    for request_id, command in expired_commands:
        if command.get("command") in ("trade.buy", "trade.sell"):
            if (state_manager.get("uncertain_trade") or {}).get("request_id") == request_id:
                return
            state_manager.set("uncertain_trade", {
                "request_id": request_id, "command": command["command"],
                # Retain the exact original payload and key so reconnect
                # recovery can safely replay through the server's idempotency
                # contract instead of either guessing or abandoning the bot.
                "data": dict(command.get("data") or {}),
                "idempotency_key": ((command.get("meta") or {}).get("idempotency_key")
                                    or (command.get("data") or {}).get("idempotency_key")),
            })
            return
        if command.get("command") == "move.warp":
            if (state_manager.get("uncertain_warp") or {}).get("request_id") == request_id:
                return
            data = command.get("data") or {}
            state_manager.set("uncertain_warp", {
                "request_id": request_id,
                "command": "move.warp",
                "to_sector_id": data.get("to_sector_id"),
                "idempotency_key": (command.get("meta") or {}).get("idempotency_key"),
            })
            return

def is_goal_complete(goal_str, game_state, response_data, response_type):
    """
    Checks if the last server response or current state fulfills the goal.
    """
    try:
        goal_type, _, goal_target = goal_str.partition(":")
        goal_type = goal_type.strip().lower()
        goal_target = goal_target.strip().lower()

        if goal_type == "goto":
            target_sector_id = int(goal_target)
            
            # This handles the case where the goal is "goto" a sector we are already in.
            current_sector_id = game_state.get("player_location_sector")
            try:
                if current_sector_id is not None and int(current_sector_id) == target_sector_id:
                    return True
            except (TypeError, ValueError):
                pass

            # Check if we just arrived from a warp (move.result)
            if response_type == "move.result":
                new_sector = response_data.get("to_sector_id")
                try:
                    if new_sector is not None and int(new_sector) == target_sector_id:
                        return True
                except (TypeError, ValueError):
                    pass

        elif goal_type == "sell":
            if response_type != "trade.sell_receipt_v1": return False
            # Check if what we just sold matches the goal
            return any(canon_commodity(l.get("commodity", "")).lower() == goal_target for l in response_data.get("lines", []))
        
        elif goal_type == "buy":
            if response_type != "trade.buy_receipt_v1": return False
            # Check if what we just bought matches the goal
            return any(canon_commodity(l.get("commodity", "")).lower() == goal_target for l in response_data.get("lines", []))

        elif goal_type == "survey" and goal_target == "port":
            # Check whether each advertised commodity has been quoted
            # We mirror the planner's strict logic here to avoid premature popping.
            current_sector_id = str(game_state.get("player_location_sector"))
            port_info = game_state.get("port_info_by_sector", {}).get(current_sector_id)
            
            if port_info is None:
                return False # Don't even have the list yet

            if not port_info.get("commodities"):
                return True # Successfully inspected empty market; no quotes are needed
            
            port_id_str = str(port_info.get("port_id"))
            price_cache = game_state.get("price_cache", {}).get(port_id_str, {})
            
            for c in port_info.get("commodities", []):
                c_code = canon_commodity(c.get("commodity"))
                if not c_code: continue
                
                if not price_cache.get("quoted_at", {}).get(c_code):
                    return False # Found a commodity that has not been quoted
            
            return True # All commodities have at least one price

        elif goal_type == "scan" and goal_target == "density":
            return response_type == "sector.density.scan"
        
        elif goal_type == "planet" and goal_target == "land":
            # The V3 server returns planet.land.success. Do not keep replaying
            # the turn-consuming landing mutation while the authoritative ship
            # and planet identities refresh.
            ship = game_state.get("ship_info") or {}
            if game_state.get("planet_landing_pending_refresh"):
                return False
            player = (game_state.get("player_info") or {}).get("player") or {}
            active_ship = player.get("ship_id")
            ship_id = ship.get("id", ship.get("ship_id"))
            landed_planet = ship.get("planet_id", ship.get("last_planet_id"))
            if not landed_planet:
                landed_planet = player.get("last_planet_id")
            try:
                authoritative_landed = (
                    bool(ship.get("onplanet"))
                    and int(active_ship) == int(ship_id)
                    and int(landed_planet) > 0
                )
            except (TypeError, ValueError):
                authoritative_landed = False
            if response_type == "planet.land.success":
                requested = response_data.get("planet_id")
                try:
                    return authoritative_landed and int(requested) == int(landed_planet)
                except (TypeError, ValueError):
                    return False
            return response_type in ("state.snapshot", "player.my_info", "ship.info", "ship.status") and authoritative_landed

        elif goal_type == "planet" and goal_target == "info":
            return response_type == "planet.info"

        elif goal_type == "bank" and goal_target in ("secure", "deposit"):
            return response_type in ("bank.deposit.confirmed", "bank.balance")

        elif goal_type == "planet" and goal_target == "create":
            if response_type not in ("planet.genesis_created_v1", "planet.create.success"):
                return False
            player = (game_state.get("player_info") or {}).get("player") or {}
            try:
                player_id = int(player.get("player_id", player.get("id", 0)) or 0)
                return (str(response_data.get("owner_type", "")).lower() == "player"
                        and int(response_data.get("owner_id", 0) or 0) == player_id
                        and player_id > 0)
            except (TypeError, ValueError):
                return False

        elif goal_type == "planet" and goal_target == "develop":
            planet = game_state.get("planet_info") or {}
            player = (game_state.get("player_info") or {}).get("player") or {}
            try:
                player_id = int(player.get("player_id", player.get("id", 0)) or 0)
                corp_id = int(player.get("corp_id", 0) or 0)
                owner_id = int(planet.get("owner_id", 0) or 0)
            except (TypeError, ValueError):
                return False
            owner_type = str(planet.get("owner_type", "")).lower()
            owned = ((owner_type == "player" and player_id > 0 and owner_id == player_id)
                     or (owner_type in ("corp", "corporation") and corp_id > 0 and owner_id == corp_id))
            if response_type in ("planet.genesis_created_v1", "planet.create.success"):
                try:
                    return (str(response_data.get("owner_type", "")).lower() == "player"
                            and player_id > 0
                            and int(response_data.get("owner_id", 0) or 0) == player_id)
                except (TypeError, ValueError):
                    return False
            if not owned:
                return False
            try:
                population = int(planet.get("population", 0) or 0)
                workers = sum(int(planet.get(key, 0) or 0) for key in (
                    "colonists_ore", "colonists_org", "colonists_eq", "colonists_weapons"))
            except (TypeError, ValueError):
                population = workers = 0
            return (response_type == "state.snapshot" and (population > 0 or workers > 0)) or (
                response_type in ("planet.colonists.allocate", "planet.harvest", "planet.harvest_v1")
                and (population > 0 or workers > 0)
            )

        elif goal_type == "hardware" and goal_target == "genesis":
            return (response_type in ("hardware.buy", "hardware.purchase_v1")
                    or int((game_state.get("ship_info") or {}).get(
                        "genesis_torpedoes", (game_state.get("ship_info") or {}).get("genesis", 0)) or 0) > 0)

        elif goal_type == "shipyard" and goal_target == "recover":
            ship = game_state.get("ship_info") or {}
            try:
                has_holds = int(ship.get("holds", 0) or 0) > 0
            except (TypeError, ValueError):
                has_holds = False
            requirements_met = True
            for capability, minimum in (game_state.get("ship_recovery_requirements") or {}).items():
                try:
                    if int(ship.get(capability, -1)) < int(minimum):
                        requirements_met = False
                        break
                except (TypeError, ValueError):
                    requirements_met = False
                    break
            # Shipyard acknowledgement only confirms the purchase. Recovery
            # completes after an authoritative ship status satisfies the
            # role's capacity requirements.
            return (has_holds and requirements_met
                    and response_type in ("ship.status", "state.snapshot"))

        elif goal_type == "combat" and goal_target == "photon":
            return response_type == "combat.photon_fired_v1"

        elif goal_type == "corp" and goal_target == "found":
            return response_type in ("corp.create.success", "corp.invite.success")

        elif goal_type == "corp" and goal_target == "join":
            return response_type in ("corp.join.success", "corp.member_joined")

        elif goal_type == "corp" and goal_target == "operate":
            return (response_type == "corp.member.asset_transferred_v1"
                    or bool(game_state.get("corporation_transfer_completed")))

        elif goal_type == "citadel" and goal_target in ("maintain", "build", "upgrade"):
            return response_type in ("citadel.upgrade_started", "planet.info")

        elif goal_type == "planet" and goal_target == "workforce":
            return response_type == "planet.colonists.allocate"

        elif goal_type == "planet" and goal_target == "defend":
            return response_type == "planet.fighters.transferred_v1"

        elif goal_type == "victory" and goal_target == "claim":
            return response_type in ("game.victory_claimed", "victory.claimed")

        elif goal_type == "victory" and goal_target == "consolidate":
            return response_type in ("bank.deposit.confirmed", "ship.repair", "planet.info")

        elif goal_type == "fleet" and goal_target in ("assign", "acquire"):
            return response_type in ("ship.info", "ship.claimed", "ship.claim.success")

        elif goal_type == "diplomacy" and goal_target == "offer":
            return response_type in ("trade.offer.created_v1", "trade.offer.accepted_v1")

        elif goal_type == "corp" and goal_target in ("found", "join", "invite", "operate"):
            return response_type in ("corp.create.success", "corp.invite.success",
                                     "corp.join.success", "corp.status.success")

    except Exception as e:
        logger.warning(f"Error checking goal completion for '{goal_str}': {e}")
    
    return False


def build_uncertain_trade_retry(uncertain, session_token, client_version, allowed_commands=None):
    """Rebuild an ambiguous trade only when its exact idempotent request is saved."""
    if not isinstance(uncertain, dict):
        return None
    command = uncertain.get("command")
    data = uncertain.get("data")
    key = uncertain.get("idempotency_key")
    if command not in ("trade.buy", "trade.sell") or not isinstance(data, dict) or not key:
        return None
    try:
        retry = build_player_command(
            command, data, session_token, client_version, allowed_commands, str(key),
        )
    except (TypeError, ValueError):
        return None
    original_request_id = uncertain.get("request_id")
    if original_request_id:
        retry["id"] = str(original_request_id)
    return retry


def advance_completed_goals(strategy_plan, game_state):
    """Remove only the completed plan prefix using freshly observed state."""
    remaining = list(strategy_plan or [])
    while remaining and is_goal_complete(remaining[0], game_state, {}, "state.snapshot"):
        remaining.pop(0)
    return remaining


def discard_stale_exploration_goals(strategy_plan, game_state):
    """Drop persisted non-port revisits when no cargo needs delivery.

    Strategy plans survive controller restarts, so fresh-plan validation alone
    cannot prevent an old exploration loop from resuming after reconnect.
    """
    if not isinstance(strategy_plan, list):
        return []
    cargo = (game_state.get("ship_info") or {}).get("cargo", [])
    has_cargo = False
    if isinstance(cargo, list):
        for item in cargo:
            if not isinstance(item, dict):
                continue
            try:
                has_cargo = float(item.get("quantity", 0) or 0) > 0
            except (TypeError, ValueError):
                has_cargo = False
            if has_cargo:
                break
    if has_cargo:
        return strategy_plan

    known_sectors = set()
    for sector_id in (game_state.get("sector_data") or {}):
        try:
            known_sectors.add(int(sector_id))
        except (TypeError, ValueError):
            continue
    known_ports = set()
    for sector_id, port in (game_state.get("port_info_by_sector") or {}).items():
        if isinstance(port, dict) and port:
            try:
                known_ports.add(int(sector_id))
            except (TypeError, ValueError):
                continue
    for sector_id, details in (game_state.get("sector_data") or {}).items():
        if isinstance(details, dict) and details.get("has_port"):
            try:
                known_ports.add(int(sector_id))
            except (TypeError, ValueError):
                continue

    kept = []
    for goal in strategy_plan:
        match = re.match(r"^goto\s*:\s*(\d+)\s*$", str(goal), re.IGNORECASE)
        if match and int(match.group(1)) in known_sectors - known_ports:
            continue
        kept.append(goal)
    return kept


def discard_protected_combat_goals(strategy_plan, game_state):
    """Remove persisted combat intents when authoritative location is protected.

    Old plans can outlive the observations that produced them. A protected
    sector makes combat intents unexecutable, so remove those intents and let
    role selection choose a legal exploration/escape objective.
    """
    try:
        sector_id = int(game_state.get("player_location_sector"))
    except (TypeError, ValueError):
        return strategy_plan
    if not 1 <= sector_id <= 10 or not isinstance(strategy_plan, list):
        return strategy_plan
    retained = [goal for goal in strategy_plan
                if not str(goal).strip().lower().startswith("combat:")]
    if retained != strategy_plan:
        logger.warning("Dropping persisted combat goals in protected FedSpace sector %s.", sector_id)
    return retained


ROLE_GATED_GOAL_PREFIXES = (
    "planet:", "shipyard:", "hardware:", "combat:", "corp:",
    "citadel:", "fleet:", "diplomacy:",
)


def discard_unavailable_role_goal_plan(strategy_plan, available_goals):
    """Drop a persisted role goal when current authoritative state forbids it."""
    if not isinstance(strategy_plan, list) or not strategy_plan:
        return []
    first = str(strategy_plan[0]).strip().lower()
    allowed = {str(goal).strip().lower() for goal in (available_goals or [])}
    if first.startswith(ROLE_GATED_GOAL_PREFIXES) and first not in allowed:
        return []
    return list(strategy_plan)


AI_SCHEMA_BOOTSTRAP_COMMANDS = frozenset({
    "player.my_info", "ship.info", "ship.status", "sector.info",
    "sector.search", "sector.scan.density", "move.warp", "move.pathfind",
    "move.autopilot.start", "move.autopilot.status", "move.autopilot.stop",
    "trade.port_info", "trade.quote", "trade.buy", "trade.sell",
    "bank.balance", "bank.deposit", "bank.withdraw", "planet.info", "planet.land",
    "planet.launch", "planet.genesis_create", "planet.harvest",
    "planet.colonists.get", "planet.colonists.set", "planet.colonists.allocate",
    "hardware.list", "hardware.buy", "dock.status", "shipyard.list", "shipyard.upgrade",
    "ship.claim", "player.list_online", "corp.create", "corp.list",
    "corp.invite", "corp.join", "corp.member.transfer", "corp.status",
    "combat.attack", "combat.fire_photon", "combat.deploy_fighters",
    "combat.deploy_mines",
})


def cache_server_command_catalogue(response_data, state_manager):
    """Store the normalized ordinary-player command inventory."""
    commands = normalize_command_catalogue(response_data)
    state_manager.set("server_commands", commands)
    state_manager.set("ordinary_player_commands", commands)
    cached_schemas = (
        state_manager.get("command_schemas", {})
        if hasattr(state_manager, "get")
        else state_manager.state.get("command_schemas", {})
    )
    state_manager.set(
        "capability_snapshot",
        capability_snapshot(commands, cached_schemas),
    )
    # Schema fetches are not part of gameplay planning. Discard pending IDs
    # from older saved state so they cannot hold the controller in bootstrap.
    state_manager.set("pending_schema_requests", {})
    logger.info("Received public command catalogue (%d commands).", len(commands))
    return commands


def schema_commands_to_fetch(commands, cached_schemas=None, include_all=False):
    """Return missing schemas for gameplay, or the complete public inventory."""
    cached_schemas = cached_schemas or {}
    return [
        command for command in commands
        if (include_all or command in AI_SCHEMA_BOOTSTRAP_COMMANDS)
        and not cached_schemas.get(command)
    ]


def reconcile_warp_readback(uncertain_warp, observed_sector):
    """Classify an uncertain movement from authoritative player state."""
    if not isinstance(uncertain_warp, dict) or observed_sector is None:
        return None
    try:
        observed = int(observed_sector)
        target = int(uncertain_warp.get("to_sector_id"))
        origin_value = uncertain_warp.get("from_sector_id")
        origin = int(origin_value) if origin_value is not None else None
    except (TypeError, ValueError):
        return None
    if observed == target:
        return "arrived"
    if origin is not None and observed == origin:
        return "not_moved"
    return "moved_elsewhere"


def _canonicalize_strategy_goal(goal, goal_options, game_state):
    """Translate a model's command-shaped suggestion into a bounded role goal.

    The model may return public command names despite being asked for goals.
    This adapter never executes those commands; it maps a small allowlist to a
    role-approved goal which is then validated against ``goal_options``.
    """
    if not isinstance(goal, str):
        return None
    value = goal.strip().lower().replace(":", ".")
    value = re.sub(r"\s+", "", value)
    choices = list(goal_options or [])
    exact = next((item for item in choices if str(item).strip().lower() == goal.strip().lower()), None)
    if exact:
        return exact
    aliases = {
        "sector.scan": "scan: density",
        "sector.scan.density": "scan: density",
        "planet.land": "planet: develop",
        "planet.info": "planet: develop",
        "planet.harvest": "planet: develop",
        "planet.colonists.allocate": "planet: develop",
        "planet.genesis_create": (
            "planet: create" if int((game_state.get("ship_info") or {}).get(
                "genesis_torpedoes", (game_state.get("ship_info") or {}).get("genesis", 0)) or 0) > 0
            else "hardware: genesis"
        ),
        "hardware.list": "hardware: genesis",
        "hardware.buy": "hardware: genesis",
        "corp.create": "corp: found",
        "corp.invite": "corp: found",
        "corp.list": "corp: join",
        "corp.join": "corp: join",
        "corp.status": "corp: operate",
        "corp.member.transfer": "corp: operate",
        "combat.attack": "combat: attack",
        "combat.attack.ship": "combat: attack",
        "combat.fire_photon": "combat: photon",
    }
    canonical = aliases.get(value)
    if canonical and canonical in choices:
        return canonical
    # Existing goal syntax is normalized only when its semantic goal is in
    # this role's advertised choice set.
    match = re.match(r"^(goto|buy|sell|scan|planet|combat|bank|citadel|fleet|corp|diplomacy|hardware|shipyard)[.:](.+)$", value)
    if match:
        verb, arg = match.groups()
        goal_aliases = {
            ("shipyard", "list"): "shipyard: recover",
            ("shipyard", "upgrade"): "shipyard: recover",
            ("planet", "land"): "planet: develop",
            ("planet", "info"): "planet: develop",
            ("planet", "harvest"): "planet: develop",
            ("planet", "colonists.allocate"): "planet: develop",
            ("planet", "genesis_create"): aliases["planet.genesis_create"],
            ("corp", "create"): "corp: found",
            ("corp", "list"): "corp: join",
            ("corp", "invite"): "corp: found",
            ("corp", "join"): "corp: join",
            ("combat", "fire_photon"): "combat: photon",
            ("sector", "scan"): "scan: density",
        }
        canonical = goal_aliases.get((verb, arg), f"{verb}: {arg}")
        return canonical if canonical in choices else None
    return None


def normalize_shipyard_catalog(response_type, response_data, refreshed_at=None):
    """Normalize the public V3 shipyard response without treating hulls as fleet."""
    if response_type not in ("shipyard.list_v1", "shipyard.list") or not isinstance(response_data, dict):
        return None
    catalog = dict(response_data)
    catalog["available"] = catalog.get("available", catalog.get("ships", []))
    if not isinstance(catalog["available"], list):
        catalog["available"] = []
    if not isinstance(catalog.get("used_available"), list):
        catalog["used_available"] = []
    catalog["_refreshed_at"] = time.time() if refreshed_at is None else float(refreshed_at)
    return catalog


def normalize_public_shipyard_search(response_type, response_data):
    """Keep only Stardock port matches returned by the public search command."""
    if response_type != "sector.search_results_v1" or not isinstance(response_data, dict):
        return []
    items = response_data.get("items", [])
    if not isinstance(items, list):
        return []
    return [
        item for item in items
        if isinstance(item, dict)
        and item.get("kind") == "port"
        and str(item.get("name") or "").strip().lower() == "stardock"
        and item.get("sector_id") is not None
    ]


def should_attempt_registration(config, state_manager):
    """Registration is opt-in and attempted at most once across restarts."""
    return bool(
        config.get("register_if_missing")
        and not state_manager.get("registration_attempted", False)
    )


# --- Main Game Loop ---


def bootstrap_schemas(game_conn, state_manager, config):
    """
    Fetch the server's public command catalogue.

    The planner uses the advertised command names to check availability and
    consumes schemas for its bounded public gameplay command set. Fetching a
    schema for every server-advertised command floods the authenticated rate
    limit and delays gameplay.
    """
    logger.info("Bootstrapping command schemas...")

    idempotency_key = str(uuid.uuid4())
    request_id = f"c-{idempotency_key[:8]}"

    schema_request = {
        "id": request_id,
        "command": "system.cmd_list",
        "data": {},
        "meta": {
            "client_version": config.get("client_version"),
            "idempotency_key": idempotency_key,
            "session_token": state_manager.get("session_id")
        }
    }

    logger.info("Requesting command list from server...")
    if game_conn.send_command(schema_request):
        state_manager.record_ai_metric("commands_sent")

    # Ensure dict exists
    state_manager.state.setdefault("pending_schema_requests", {})
    state_manager.state["pending_schema_requests"]["system.cmd_list"] = request_id

    time.sleep(0.1)  # Avoid overwhelming the server



def main(config_path="config.json"):
    global shutdown_flag

    # Load configuration
    try:
        with open(config_path, 'r') as f:
            config = json.load(f)

    except FileNotFoundError:
        logger.critical("%s not found. Exiting.", config_path)
        return
    except json.JSONDecodeError:
        logger.critical("%s is not valid JSON. Exiting.", config_path)
        return

    config_errors = validate_config(config)
    if config_errors:
        logger.critical("Invalid configuration: %s", "; ".join(config_errors))
        return
    
    # --- Setup Logging from Config ---
    setup_logging(config)
    if config.get("playtest_bot"):
        logger.info("PLAYTEST_CONFIG bot=%s role=%s endpoint=%s model=%s decision_interval=%ss heartbeat_interval=%ss",
                    config.get("playtest_bot"), config.get("role_name"),
                    config.get("ollama_url"), config.get("ollama_model"),
                    config.get("decision_interval_seconds", 8),
                    session_heartbeat_interval(config))

    # Staggering is useful for a deliberately concurrent cohort, but a long
    # random delay made a single local QA run look hung. Keep it opt-in.
    try:
        max_start_delay = max(0.0, float(config.get("startup_stagger_max_seconds", 0)))
    except (TypeError, ValueError):
        max_start_delay = 0.0
    if max_start_delay:
        delay = random.uniform(0, max_start_delay)
        logger.info("Applying configured startup stagger of %.2f seconds.", delay)
        time.sleep(delay)
    
    # --- Initialize Core Components ---
    # --- FIX: Use YOUR config keys ---
    game_host = config.get("game_host", "localhost")
    game_port = config.get("game_port", 9000)
    state_file = config.get("state_file", "state.json")
    bug_path = config.get("bug_report_path", "bug_reports")
    
    state_manager = StateManager(state_file, config)
    if config.get("playtest_bot"):
        metrics = state_manager.get("ai_metrics", {}) or {}
        state_manager.set("playtest_process_started_at", time.time())
        state_manager.set("playtest_process_metric_baseline", {
            key: metrics.get(key, 0) for key in (
                "tcp_connections_created", "tcp_connections_closed", "tcp_connect_failures",
                "authentication_attempts", "authentication_successes", "authentication_failures",
                "server_initiated_disconnects", "client_initiated_disconnects", "network_exceptions",
                "session_pings_sent", "session_pings_acknowledged", "server_request_deadline_errors",
            )
        })
    game_conn = GameConnection(game_host, game_port, state_manager)
    bug_reporter = BugReporter(bug_path)
    
    # --- FIX: This is the fix for the TypeError ---
    # Initialize BanditPolicy
    bandit_policy = BanditPolicy(
        epsilon=config.get("epsilon_exploit", 0.1), # Use a config-driven epsilon
        q_table=state_manager.get("q_table", {}), 
        n_table=state_manager.get("n_table", {})
    )
    
    # --- FIX: Pass bandit_policy and config to Planner ---
    planner = Planner(state_manager, bug_reporter, bandit_policy, config)
    state_manager.set("ship_recovery_requirements", planner._recovery_capability_requirements())
    
    state_manager.set_default("strategy_plan", [])
    state_manager.set_default("session_id", None)
    state_manager.set_default("current_qa_objective", None) # NEW: For QA testing mode
    state_manager.set_default("qa_objective_start_time", None)  # Track when objective started
    state_manager.set_default("qa_commands_for_objective", 0)  # Count commands for this objective
    state_manager.set_default("qa_workflow_queue", [])  # Queue of commands from current workflow
    state_manager.set_default("qa_workflow_index", 0)  # Index in round-robin workflow rotation
    state_manager.set_default("qa_tested_commands", [])  # Track which commands have been tested
    state_manager.set_default("valid_commodity_names", ["ORE", "ORG", "EQU", "DRG", "SLV", "WPN"])  # All tradable commodities
    # We must reset session_id on start, as it's not persistent
    state_manager.set("session_id", None)
    state_manager.set("handshake_complete", False)
    state_manager.set("server_commands", [])
    state_manager.set_default("registration_attempted", False)
    state_manager.set_default("handshake_complete", False)
    state_manager.set_default("server_capabilities", {})
    state_manager.set_default("ordinary_player_commands", [])
    state_manager.set_default("server_commands", [])
    # A process may have died after a non-idempotent write reached the server.
    # Never resume its old plan until public authoritative readbacks reconcile it.
    state_manager.recover_uncertain_mutations_after_restart()

    last_heartbeat = time.time()
    reconnect_attempt = 0
    process_connected_logged = False
    schemas_bootstrapped = False
    
    # QA Mode Constants
    QA_OBJECTIVE_TIME_LIMIT = 600  # 10 minutes - rotate objective after this time
    QA_OBJECTIVE_COMMAND_LIMIT = 50  # commands - rotate objective after this many commands
    
    # --- Main Loop ---
    while not shutdown_flag:
        try:
            # 0. Always process server responses first
            responses = game_conn.receive_responses()
            if responses:
                process_responses(responses, game_conn, state_manager, bug_reporter, bandit_policy, config, planner)
                last_heartbeat = time.time()

            if game_conn.consume_disconnect():
                logger.warning("Connection lost; discarding session-scoped state and requiring a fresh login.")
                state_manager.clear_connection_state()
                schemas_bootstrapped = False

            try:
                response_timeout = max(1.0, float(config.get("command_response_timeout", 30)))
            except (TypeError, ValueError):
                response_timeout = 30.0
            expired_commands = state_manager.expire_pending_commands(response_timeout)
            if expired_commands:
                quarantine_timed_out_trades(state_manager, expired_commands)
                expired_names = [
                    str(command.get("command") or "unknown")
                    for _, command in expired_commands
                ]
                logger.warning(
                    "Timed out waiting for %d command response(s) (%s); "
                    "refreshing authoritative player and ship state.",
                    len(expired_commands), ", ".join(expired_names),
                )
                for _request_id, expired_command in expired_commands:
                    command_name = expired_command.get("command", "unknown")
                    state_manager.mark_mutation_timed_out(_request_id)
                    state_manager.record_command_timeout(command_name)
                    state_manager.record_command_failure(command_name)
                    if command_name in ("move.autopilot.start", "move.autopilot.status"):
                        state_manager.set("autopilot_status_checked", False)
                    if expired_command.get("command") == "player.computer.recommend_routes":
                        state_manager.set("market_recommendations_requested_at", 0)
                # A timed-out action may have reached the server. Do not repeat
                # the old plan until fresh player and ship state is available.
                state_manager.set("player_info", None)
                state_manager.set("ship_info", None)
                state_manager.set("player_location_sector", None)
                state_manager.set("current_path", [])
                state_manager.set("autopilot_status", None)
                state_manager.set("autopilot_status_checked", False)
                state_manager.set("strategy_plan", [])

            game_state = state_manager.get_all() # Always get the freshest state here

            # --- NEW: Check if a deterministic rule already sent a command ---
            if state_manager.get("command_sent_in_response_loop"):
                state_manager.set("command_sent_in_response_loop", False)
                logger.info("Skipping planner turn because a deterministic rule already sent a command.")
                time.sleep(0.5)
                continue
            # -----------------------------------------------------------------

            if (config.get("population_mode") and not config.get("qa_mode")
                    and not game_state.get("pending_commands")):
                player = (game_state.get("player_info") or {}).get("player") or {}
                pause_seconds = population_pause_seconds(
                    config, state_manager, turns_remaining=player.get("turns_remaining")
                )
                if pause_seconds > 0:
                    logger.info("Population schedule inactive; pausing for %.0f seconds.", pause_seconds)
                    if game_conn.sock or game_state.get("session_id"):
                        game_conn.disconnect()
                        state_manager.clear_connection_state(record_reconnect=False)
                        schemas_bootstrapped = False
                    time.sleep(min(60.0, max(1.0, pause_seconds)))
                    continue

            # 1. Handle Connection & Authentication
            if not game_conn.sock:
                if not game_conn.connect():
                    reconnect_attempt += 1
                    base = max(1.0, float(config.get("reconnect_base_seconds", 3)))
                    cap = max(base, float(config.get("reconnect_max_seconds", 60)))
                    bot_index = max(0, int(config.get("playtest_index", 1)) - 1)
                    delay = min(cap, base * (2 ** min(reconnect_attempt - 1, 8)))
                    delay *= random.uniform(0.8, 1.2)
                    delay += min(20.0, bot_index * 2.0)
                    logger.info("Reconnect attempt=%d bot_index=%d retry_in_seconds=%.1f",
                                reconnect_attempt, bot_index + 1, delay)
                    time.sleep(delay)
                    continue
                if reconnect_attempt:
                    logger.info("Reconnected after %d attempts", reconnect_attempt)
                reconnect_attempt = 0
                if config.get("playtest_bot") and not process_connected_logged:
                    process_connected_logged = True
                    if not state_manager.get("playtest_connected_at"):
                        state_manager.set("playtest_connected_at", time.time())
                    logger.info("PLAYTEST_CONNECTED bot=%s host=%s port=%s",
                                config.get("playtest_bot"), game_host, game_port)

            # V3 requires a system handshake before authentication. Older
            # servers may refuse it; in that case retain the legacy flow so
            # compatibility is explicit rather than silently assumed.
            if not game_state.get("handshake_complete") and not state_manager.get("pending_commands"):
                hello = build_system_hello(config)
                if game_conn.send_command(hello):
                    state_manager.add_pending_command(hello["id"], hello)
                    state_manager.record_ai_metric("commands_sent")
                    state_manager.set("handshake_attempted", True)
                continue

            # Serialize actions: choosing against state from before an
            # unacknowledged mutation can duplicate purchases, warps, or builds.
            if state_manager.get("pending_commands"):
                time.sleep(0.1)
                continue
            
            # --- Check if we are authenticated BEFORE planning ---
            game_state = state_manager.get_all()
            if not game_state.get("session_id"):
                if not config.get("player_username") or not config.get("player_password"):
                    logger.critical("player_username or player_password not found in config.json. Exiting.")
                    shutdown_flag = True
                    continue
                if not planner._is_command_ready("auth.login", game_state.get("command_retry_info", {})):
                    time.sleep(1)
                    continue

                # Account creation is disabled by default. When explicitly
                # enabled, use the public auth.register command once, then
                # fall back to login if the username already exists. A lost
                # registration reply is reconciled by login, never blind retry.
                if should_attempt_registration(config, state_manager):
                    retry_info = game_state.get("command_retry_info", {})
                    if not planner._is_command_ready("auth.register", retry_info):
                        time.sleep(1)
                        continue
                    registration_key = str(uuid.uuid4())
                    registration = build_registration_command(config, registration_key)
                    state_manager.record_ai_metric("authentication_attempts")
                    if game_conn.send_command(registration):
                        state_manager.set("registration_attempted", True)
                        state_manager.add_pending_command(registration["id"], registration)
                        state_manager.record_command_sent("auth.register")
                        state_manager.record_ai_metric("commands_sent")
                        logger.info("Attempting configured public auth.register for %s.", config.get("player_username"))
                    time.sleep(1)
                    continue

                # Always send login if we have no session_id
                idempotency_key = str(uuid.uuid4())
                login_cmd = {
                    "id": f"c-{idempotency_key[:8]}",
                    "command": "auth.login",
                    "data": {
                        "passwd": config.get("player_password"),
                        "username": config.get("player_username") 
                    },
                    "meta": {
                        "client_version": config.get("client_version"),
                        "idempotency_key": idempotency_key
                    }
                }
                
                logger.info("No session detected. Sending auth.login for configured player %s.", config.get("player_username"))
                state_manager.record_ai_metric("authentication_attempts")
                if game_conn.send_command(login_cmd):
                    state_manager.add_pending_command(login_cmd["id"], login_cmd)
                    state_manager.record_ai_metric("commands_sent")
                
                # Wait for login response to avoid spamming login requests
                # We'll re-loop and process responses in the next iteration
                time.sleep(1) 
                continue

            # Send the heartbeat through the same correlated, serialized
            # command path as gameplay requests.
            heartbeat_interval = session_heartbeat_interval(config)
            if time.time() - last_heartbeat >= heartbeat_interval:
                idemp = str(uuid.uuid4())
                ping_cmd = {
                    "id": f"c-ping-{idemp[:8]}",
                    "command": "player.ping",
                    "data": {},
                    "meta": {
                        "client_version": config.get("client_version"),
                        "idempotency_key": idemp,
                        "session_token": game_state.get("session_id")
                    }
                }
                if game_conn.send_command(ping_cmd):
                    state_manager.add_pending_command(ping_cmd["id"], ping_cmd)
                    state_manager.record_ai_metric("commands_sent")
                    state_manager.record_ai_metric("session_pings_sent")
                    last_heartbeat = time.time()
                continue
            
            # --- If we are here, we are authenticated ---

            # --- Bootstrap Schemas after login ---
            if not schemas_bootstrapped:
                bootstrap_schemas(game_conn, state_manager, config)
                schemas_bootstrapped = True
                # Give server time to respond to schema requests
                logger.info("Waiting for schema responses...")
                time.sleep(2) 
                continue

            # Do not let a timed-out purchase, transfer or other costly write
            # be planned again from stale local state. Read each affected
            # account/inventory view through the public protocol first.
            uncertain_reads = state_manager.uncertain_mutation_readbacks()
            if uncertain_reads:
                readback = uncertain_reads[0]
                if readback["command"] not in (game_state.get("server_commands") or []):
                    logger.error("Mutation %s is ambiguous; required public readback %s is not advertised. ",
                                 readback["request_id"], readback["command"])
                    time.sleep(5)
                    continue
                readback_id = str(uuid.uuid4())
                try:
                    readback_command = build_player_command(
                        readback["command"], readback["data"], game_state.get("session_id"),
                        config.get("client_version"), game_state.get("server_commands"), readback_id,
                    )
                except (TypeError, ValueError) as exc:
                    logger.error("Cannot safely reconcile mutation %s: %s", readback["request_id"], exc)
                    time.sleep(5)
                    continue
                if game_conn.send_command(readback_command):
                    state_manager.add_pending_command(readback_command["id"], readback_command)
                    state_manager.record_ai_metric("commands_sent")
                    state_manager.record_ai_metric("mutation_readbacks_sent")
                time.sleep(0.2)
                continue

            unresolved_manual = state_manager.unresolved_manual_mutations()
            if unresolved_manual:
                logger.error(
                    "Gameplay paused: %d ambiguous transfer(s) have fresh source state but their "
                    "recipient-side outcome is not publicly verifiable. Original request IDs: %s",
                    len(unresolved_manual),
                    ",".join(item.get("request_id", "?") for item in unresolved_manual),
                )
                time.sleep(10)
                continue

            if game_state.get("uncertain_trade"):
                uncertain = game_state.get("uncertain_trade") or {}
                if state_manager.get("pending_commands"):
                    time.sleep(0.5)
                    continue
                retry = build_uncertain_trade_retry(
                    uncertain, game_state.get("session_id"), config.get("client_version"),
                    game_state.get("server_commands"),
                )
                if retry is None:
                    logger.error(
                        "Trade outcome is uncertain and exact idempotent request data is unavailable; "
                        "gameplay remains paused for safe reconciliation."
                    )
                    time.sleep(10)
                    continue
                if game_conn.send_command(retry):
                    state_manager.set("uncertain_trade", {
                        **uncertain, "request_id": retry["id"],
                        "reconcile_attempts": int(uncertain.get("reconcile_attempts", 0)) + 1,
                    })
                    state_manager.add_pending_command(retry["id"], retry)
                    state_manager.record_ai_metric("commands_sent")
                    state_manager.record_ai_metric("trade_retries")
                    logger.warning("Replaying uncertain trade with its exact saved payload and original idempotency key.")
                else:
                    logger.warning("Could not send idempotent trade retry; will reconcile after reconnect.")
                time.sleep(0.5)
                continue

            if game_state.get("uncertain_warp"):
                uncertain = game_state.get("uncertain_warp") or {}
                if state_manager.get("pending_commands"):
                    time.sleep(0.5)
                    continue
                request_id = f"c-warp-reconcile-{uuid.uuid4().hex[:8]}"
                # A lost movement reply is not safe to replay: the server may
                # have committed the move even when the client missed its ack.
                # Read authoritative location first; only then can planning
                # issue a fresh move from the confirmed sector.
                readback = {
                    "id": request_id,
                    "command": "player.my_info",
                    "data": {},
                    "meta": {"client_version": config.get("client_version"),
                             "session_token": game_state.get("session_id")},
                }
                if game_conn.send_command(readback):
                    state_manager.set("uncertain_warp", {
                        **uncertain, "readback_request_id": request_id,
                        "readback_attempts": int(uncertain.get("readback_attempts", 0)) + 1,
                    })
                    state_manager.add_pending_command(request_id, readback)
                    state_manager.record_ai_metric("commands_sent")
                    logger.info("Reconciling uncertain warp from authoritative player-state readback.")
                else:
                    logger.warning("Could not read back uncertain warp; will retry the state read after reconnect.")
                continue

            # Reconcile a server-persisted autopilot route after each login.
            if ("move.autopilot.status" in game_state.get("server_commands", [])
                    and not game_state.get("autopilot_status_checked", False)):
                idemp = str(uuid.uuid4())
                status_cmd = {
                    "id": f"c-ap-status-{idemp[:8]}",
                    "command": "move.autopilot.status",
                    "data": {},
                    "meta": {"client_version": config.get("client_version"),
                             "idempotency_key": idemp,
                             "session_token": game_state.get("session_id")},
                }
                if game_conn.send_command(status_cmd):
                    state_manager.set("autopilot_status_checked", True)
                    state_manager.add_pending_command(status_cmd["id"], status_cmd)
                    state_manager.record_ai_metric("commands_sent")
                continue

            # Poll trade-loop recommendations occasionally, and only when the
            # connected server advertises the existing command.
            known_port_count = len(game_state.get("port_info_by_sector", {}))
            refresh_seconds = max(60, int(config.get("route_recommendation_refresh_seconds", 900)))
            last_route_request = float(game_state.get("market_recommendations_requested_at", 0) or 0)
            if (not config.get("qa_mode")
                    and "player.computer.recommend_routes" in game_state.get("server_commands", [])
                    and known_port_count >= 2
                    and time.time() - last_route_request >= refresh_seconds):
                idemp = str(uuid.uuid4())
                route_cmd = {
                    "id": f"c-route-rec-{idemp[:8]}",
                    "command": "player.computer.recommend_routes",
                    "data": {"max_hops_between": 10, "max_hops_from_player": 20,
                             "require_two_way": False, "limit": 20},
                    "meta": {"client_version": config.get("client_version"),
                             "idempotency_key": idemp,
                             "session_token": game_state.get("session_id")},
                }
                if game_conn.send_command(route_cmd):
                    state_manager.set("market_recommendations_requested_at", time.time())
                    state_manager.add_pending_command(route_cmd["id"], route_cmd)
                    state_manager.record_ai_metric("commands_sent")
                continue

            # Events are hints, not authoritative state. Fetch fresh state
            # before allowing the planner to act on the affected subsystem.
            refresh_command = state_manager.take_event_refresh_request()
            if refresh_command:
                idemp = str(uuid.uuid4())
                refresh_data = ({"sector_id": int(game_state["player_location_sector"])}
                                if refresh_command == "sector.info" and game_state.get("player_location_sector") is not None
                                else {})
                refresh_cmd = {
                    "id": f"c-event-refresh-{idemp[:8]}",
                    "command": refresh_command,
                    "data": refresh_data,
                    "meta": {"client_version": config.get("client_version"),
                             "idempotency_key": idemp,
                             "session_token": game_state.get("session_id")},
                }
                if game_conn.send_command(refresh_cmd):
                    state_manager.add_pending_command(refresh_cmd["id"], refresh_cmd)
                    state_manager.record_ai_metric("commands_sent")
                continue

            # 4. Get Current State & Strategy
            game_state = state_manager.get_all()
            
            # --- FIX: Cooldown if out of turns ---
            current_turns = (game_state.get("player_info") or {}).get("player", {}).get("turns_remaining", 1)
            if current_turns is not None and int(current_turns) <= 0:
                # Keep the authenticated socket and local state intact. Poll the
                # server's normal turn allocation every 30 seconds so reset is
                # detected automatically without flooding player.my_info.
                now = time.time()
                last_poll = float(state_manager.get("turn_reset_last_poll", 0) or 0)
                if turn_refresh_due(last_poll, now, config.get("turn_reset_poll_seconds", 30)):
                    request_id = str(uuid.uuid4())
                    full_command = build_player_command(
                        "player.my_info", {}, game_state.get("session_id"),
                        config.get("client_version"), game_state.get("server_commands"), request_id,
                    )
                    if game_conn.send_command(full_command):
                        state_manager.add_pending_command(full_command["id"], full_command)
                        state_manager.set("turn_reset_last_poll", now)
                        state_manager.record_ai_metric("turn_reset_polls")
                        logger.info("Turn budget exhausted; requested authoritative reset check.")
                # Allow only explicitly configured roles to consider public
                # operations that do not spend turns, such as corporation
                # formation or equipment prerequisites. The command-level
                # turn budget remains authoritative for every selected action.
                if not config.get("allow_zero_turn_strategy", False):
                    _pause_for_turn_refresh(
                        state_manager, seconds=5,
                        heartbeat_interval=session_heartbeat_interval(config))
                    continue

            strategy_plan = game_state.get("strategy_plan", [])
            valid_plan = discard_stale_exploration_goals(strategy_plan, game_state)
            valid_plan = discard_protected_combat_goals(valid_plan, game_state)
            if config.get("playtest_bot") and valid_plan:
                role_options = _role_goal_options(
                    config.get("role_name"), game_state,
                    {
                        "current_port": (game_state.get("port_info_by_sector") or {}).get(
                            str(game_state.get("player_location_sector"))),
                        "known_port_sector_ids": list((game_state.get("known_ports") or {}).keys()),
                    },
                    config,
                )
                role_valid_plan = discard_unavailable_role_goal_plan(valid_plan, role_options)
                if role_valid_plan != valid_plan:
                    logger.info(
                        "Dropping persisted role goal %s; current public state permits %s.",
                        valid_plan[0], role_options,
                    )
                valid_plan = role_valid_plan
            if valid_plan != strategy_plan:
                if valid_plan != discard_stale_exploration_goals(strategy_plan, game_state):
                    logger.warning("Dropping persisted exploration goals for already-known non-port sectors.")
                strategy_plan = valid_plan
                state_manager.set("strategy_plan", strategy_plan)
            current_goal = None

            # 5. Check if we need a new strategy
            
            # --- Bootstrap Check (NEW) ---
            bootstrap_cmd = planner.ensure_minimum_world_state(game_state)
            if bootstrap_cmd:
                command_name = bootstrap_cmd["command"]
                if not planner._is_command_ready(command_name, game_state.get("command_retry_info", {})):
                    logger.debug(f"Bootstrap command '{command_name}' is on cooldown. Waiting.")
                    time.sleep(1)
                    continue
                
                idempotency_key = str(uuid.uuid4())
                full_command = {
                    "id": f"c-{idempotency_key[:8]}",
                    "command": bootstrap_cmd["command"],
                    "data": bootstrap_cmd.get("data", {}),
                    "meta": {
                        "client_version": config.get("client_version"),
                        "idempotency_key": idempotency_key,
                        "session_token": game_state.get("session_id")
                    }
                }
                logger.info(f"Bootstrap: Sending {bootstrap_cmd['command']}")
                logger.info("Bootstrap command: %s", redact_secrets(full_command))
                if game_conn.send_command(full_command):
                    state_manager.record_command_sent(bootstrap_cmd["command"])
                    state_manager.record_ai_metric("commands_sent")
                    # Don't add to pending_commands if we don't need to track specific reply ID for logic
                    # But we should for debugging.
                    state_manager.add_pending_command(full_command['id'], full_command)
                
                time.sleep(1) # Wait for response
                continue

            if not strategy_plan:
                # game_state is already fresh from above
                current_sector_id = str(game_state.get("player_location_sector"))
                current_sector_data = game_state.get("sector_data", {}).get(current_sector_id, {})
                at_port = current_sector_data.get("has_port", False)
                
                port_info = game_state.get("port_info_by_sector", {}).get(current_sector_id)
                survey_needed = at_port and port_info is None

                if config.get("qa_mode"):
                    # Workflow-based QA mode: Execute deterministic command sequences
                    current_workflow_queue = state_manager.get("qa_workflow_queue", [])
                    
                    # If queue is empty, load next workflow
                    if not current_workflow_queue:
                        workflow_idx = state_manager.get("qa_workflow_index", 0)
                        workflow_name = QA_WORKFLOW_ORDER[workflow_idx % len(QA_WORKFLOW_ORDER)]
                        workflow_commands = QA_WORKFLOWS[workflow_name]
                        
                        # Special handling: TRADE workflow requires being at a port
                        if workflow_name == "trade":
                            if not at_port:
                                # Try to navigate to a known port, or skip workflow
                                known_ports = state_manager.get("known_ports", {})
                                if known_ports:
                                    # Get first available port
                                    first_port_info = next(iter(known_ports.values()))
                                    target_sector = first_port_info.get("sector_id")
                                    if target_sector and target_sector != current_sector_id:
                                        logger.info(f"QA: Not at port. Prepending navigation to sector {target_sector}")
                                        # Insert navigation commands at front (use goto: format for strategic goal)
                                        workflow_commands = [f"goto: {target_sector}", "sector:info"] + workflow_commands
                                    else:
                                        logger.info(f"QA: Skipping TRADE workflow (not at a port and no new ports to navigate to)")
                                        state_manager.set("qa_workflow_index", workflow_idx + 1)
                                        continue
                                else:
                                    logger.info(f"QA: Skipping TRADE workflow (not at a port, sector {current_sector_id} has no ports). Rotating to next workflow.")
                                    state_manager.set("qa_workflow_index", workflow_idx + 1)
                                    continue
                        
                        # Log coverage stats
                        tested_cmds = state_manager.get("qa_tested_commands", [])
                        coverage = qa_coverage_snapshot(
                            tested_cmds,
                            state_manager.get("server_commands", []),
                            state_manager.get("qa_scenario_ids", []),
                        )
                        logger.info(
                            "QA Workflow rotation: %s (%d). Coverage: %.1f%% (%d/%d advertised commands; scenarios=%d)",
                            workflow_name.upper(), workflow_idx % len(QA_WORKFLOW_ORDER),
                            coverage["coverage_percent"], coverage["tested_count"],
                            coverage["inventory_size"], len(coverage["scenario_ids"]),
                        )
                        
                        current_workflow_queue = workflow_commands.copy()
                        state_manager.set("qa_workflow_queue", current_workflow_queue)
                        state_manager.set("qa_workflow_index", workflow_idx + 1)
                    
                    # Convert first command from queue into a goal
                    if current_workflow_queue:
                        next_cmd = current_workflow_queue.pop(0)
                        state_manager.set("qa_workflow_queue", current_workflow_queue)
                        
                        # Smart goal format conversion:
                        # If it's already a goal format (contains strategic keywords), use as-is
                        # Otherwise wrap with "execute:" for raw commands
                        if any(keyword in next_cmd for keyword in ["survey:", "sell:", "buy:", "goto:"]):
                            goal = next_cmd  # Already a strategic goal
                        else:
                            goal = f"execute: {next_cmd}"  # Raw command, needs execution wrapper
                        strategy_plan = [goal]
                        state_manager.set("strategy_plan", strategy_plan)
                        state_manager.set("strategy_plan_source", "qa_workflow")
                        logger.info(f"QA: Next workflow command: {next_cmd}")
                else: # Not in QA mode, revert to old strategy logic
                    if survey_needed:
                        logger.info("At a port, but survey is incomplete. Setting goal to survey.")
                        state_manager.set("strategy_plan", ["survey: port"])
                        state_manager.set("strategy_plan_source", "controller")
                    else:
                        if game_state.get("player_info") and game_state.get("ship_info"): # Only ask if we are 'in-game'
                            logger.info("Strategy plan is empty. Selecting a new plan.")
                            stop_keepalive = (
                                _start_llm_keepalive(game_conn, state_manager, config)
                                if config.get("playtest_bot") and config.get("strategy_mode") == "ollama"
                                else (lambda: None)
                            )
                            try:
                                new_plan = select_strategy_plan(game_state, config, state_manager, planner)
                            finally:
                                stop_keepalive()
                            state_manager.set("strategy_plan", new_plan)
                            strategy_plan = new_plan
                            if state_manager.get("strategy_plan_source") == "fallback":
                                time.sleep(2) # Wait a moment before continuing
            
            # Drop goals already satisfied by authoritative state. This also
            # reconciles a persisted goto/survey goal after a process restart.
            remaining_plan = advance_completed_goals(strategy_plan, game_state)
            if len(remaining_plan) != len(strategy_plan):
                logger.info("Advancing persisted plan past goals satisfied by current state.")
                strategy_plan = remaining_plan
                state_manager.set("strategy_plan", strategy_plan)

            # 6. Get the current goal
            if strategy_plan:
                current_goal = strategy_plan[0] # Get the top goal
                logger.info(f"Current strategic goal: {current_goal}")

            # Trim multi-step plans to the affordable turn budget while
            # preserving configured turns for role-priority goals. The server
            # remains authoritative and rejects any stale/incorrect estimate.
            if strategy_plan:
                player = (game_state.get("player_info") or {}).get("player") or {}
                prefixes = config.get("turn_reserve_priority_prefixes", [])
                budgeted_plan = affordable_plan(
                    strategy_plan, game_state, player.get("turns_remaining"),
                    config.get("turn_reserve", 12), prefixes,
                    path_finder=getattr(state_manager, "find_path", None),
                )
                if budgeted_plan != strategy_plan:
                    logger.info("Turn budget trimmed plan from %s to %s (turns=%s reserve=%s).",
                                strategy_plan, budgeted_plan, player.get("turns_remaining"),
                                config.get("turn_reserve", 12))
                    state_manager.set("strategy_plan", budgeted_plan)
                    strategy_plan = budgeted_plan
                    current_goal = strategy_plan[0] if strategy_plan else None

            # 7. Get Next Command from Planner
            next_command_dict = planner.get_next_command(game_state, current_goal)
            
            if next_command_dict:
                command_name = next_command_dict.get("command")
                turn_cost = command_turn_cost(command_name)
                player = (game_state.get("player_info") or {}).get("player") or {}
                try:
                    turns_remaining = int(player.get("turns_remaining", 0))
                    reserve = max(0, int(config.get("turn_reserve", 12)))
                except (TypeError, ValueError):
                    turns_remaining, reserve = 0, 0
                priority_prefixes = config.get("turn_reserve_priority_prefixes", [])
                priority_goal = bool(priority_prefixes) and isinstance(current_goal, str) and current_goal.startswith(tuple(priority_prefixes))
                # Navigation is a prerequisite, not the protected strategic
                # operation. Keep the configured reserve intact while moving;
                # only the goal's terminal gameplay action may spend it.
                priority_action = priority_goal and command_name not in (
                    "move.warp", "move.transwarp", "move.pathfind"
                )
                if turn_cost and not command_fits_turn_budget(
                        command_name, turns_remaining, 0, priority=True):
                    logger.info("Turn budget exhausted; deferring %s (cost=%d turns=%d goal=%s).",
                                command_name, turn_cost, turns_remaining, current_goal)
                    state_manager.set("strategy_plan", [])
                    state_manager.record_ai_metric("turn_budget_deferrals")
                    time.sleep(1)
                    continue
                if turn_cost and not command_fits_turn_budget(
                        command_name, turns_remaining, reserve, priority_action):
                    logger.info("Turn budget deferred %s (cost=%d turns=%d reserve=%d goal=%s).",
                                command_name, turn_cost, turns_remaining, reserve, current_goal)
                    state_manager.set("strategy_plan", [])
                    state_manager.record_ai_metric("turn_budget_deferrals")
                    time.sleep(1)
                    continue
                # Trade handlers require the data and meta copies of the
                # idempotency key to agree. The planner may already have put a
                # key in the trade payload, so reuse it for the envelope.
                data_payload = dict(next_command_dict.get("data", {}) or {})
                idempotency_key = align_command_idempotency(
                    command_name, data_payload, str(uuid.uuid4())
                )

                
                # Build through the V3 safety gate so a planner or persisted
                # QA workflow cannot submit a response-only/privileged command.
                try:
                    full_command = build_player_command(
                        command_name, data_payload, game_state.get("session_id"),
                        config.get("client_version"), game_state.get("server_commands"),
                        idempotency_key,
                    )
                except (TypeError, ValueError) as exc:
                    logger.error("Refusing unsafe or unavailable command: %s", exc)
                    state_manager.record_command_failure(command_name or "unknown")
                    state_manager.set("strategy_plan", [])
                    continue

                logger.info("Sending command: %s", redact_secrets(full_command))
                readback_requests = uncertain_mutation_readback_requests(
                    command_name, data_payload, game_state
                ) if population_turn_readback_required(command_name, config) else []
                if readback_requests and hasattr(state_manager, "mark_uncertain_mutation"):
                    state_manager.mark_uncertain_mutation(
                        full_command["id"], command_name, data_payload,
                        mutation_reconciliation_contract(command_name),
                        idempotency_key=idempotency_key,
                        readback_types=[item[0] for item in readback_requests],
                        readback_requests=readback_requests,
                        manual_review_after_readback=command_name in {
                            "bank.transfer", "corp.member.transfer", "ship.transfer_cargo"
                        },
                    )
                if command_name in ("trade.buy", "trade.sell"):
                    state_manager.set("uncertain_trade", {
                        "request_id": full_command["id"], "command": command_name,
                        "idempotency_key": idempotency_key,
                        "data": dict(data_payload),
                    })
                elif command_name == "move.warp":
                    state_manager.set("uncertain_warp", {
                        "request_id": full_command["id"], "command": command_name,
                        "idempotency_key": idempotency_key,
                        "to_sector_id": data_payload.get("to_sector_id"),
                        "from_sector_id": game_state.get("player_location_sector"),
                    })

                if game_conn.send_command(full_command):
                    state_manager.add_pending_command(full_command['id'], full_command)
                    state_manager.record_ai_metric("commands_sent")
                    state_manager.record_command_sent(command_name)

                    if (config.get("live_gameplay_audit")
                            and command_name in ("move.warp", "trade.buy", "trade.sell")):
                        state_before = _audit_state_snapshot(game_state)
                        audit_before_states = getattr(
                            state_manager, "_live_audit_command_before", {}
                        )
                        audit_before_states[full_command["id"]] = state_before
                        state_manager._live_audit_command_before = audit_before_states
                        bug_reporter.log_command(
                            full_command, state_before=state_before
                        )
                    
                    # Track tested commands in QA mode
                    if config.get("qa_mode"):
                        qa_commands = state_manager.get("qa_commands_for_objective", 0)
                        state_manager.set("qa_commands_for_objective", qa_commands + 1)
                        
                        # Track command in coverage list
                        tested_cmds = state_manager.get("qa_tested_commands", [])
                        if command_name not in tested_cmds:
                            tested_cmds.append(command_name)
                            state_manager.set("qa_tested_commands", tested_cmds)
                        
                        bug_reporter.log_command(full_command)
                        
                        # In QA mode, clear the strategy plan after sending a workflow command
                        # so the next workflow command gets popped from the queue
                        if current_goal and current_goal.startswith("execute:"):
                            state_manager.set("strategy_plan", [])
                    
                    # If an invariant command was sent, prioritize waiting for its response
                    if next_command_dict.get("is_invariant"):
                        logger.info(f"Sent invariant command '{command_name}'. Pausing to await response.")
                        time.sleep(1) # Wait a bit longer for crucial state updates
                        continue # Immediately re-loop to process responses
                else:
                    logger.warning("Failed to send command (connection issue).")

            else:
                if current_goal:
                    # None can mean the action is on cooldown or this goal is
                    # waiting for required state. Preserve the multi-step plan;
                    # known permanent navigation refusals remove their goal in
                    # process_responses. QA commands remain one-shot.
                    if config.get("qa_mode") and current_goal.startswith("execute:"):
                        logger.warning("QA planner could not execute '%s'; advancing the workflow.", current_goal)
                        state_manager.set("strategy_plan", [])
                        state_manager.set("current_qa_objective", None)
                    else:
                        logger.info("No ready action for goal '%s'; retaining plan for retry.", current_goal)
                else:
                    # If there was no goal, just wait for cooldowns.
                    logger.debug("Planner returned no command (no goal). Waiting for cooldowns.")
            
            # Prevent busy-looping
            time.sleep(0.5)

        except KeyboardInterrupt:
            logger.info("Shutdown signal received. Exiting.")
            shutdown_flag = True
        except Exception as e:
            logger.critical(f"Unhandled exception in main loop: {e}", exc_info=True)
            time.sleep(5) # Avoid rapid crash-loop
            
    # --- Cleanup ---
    if game_conn:
        game_conn.disconnect()
    logger.info("AI simulation metrics: %s", json.dumps(state_manager.get("ai_metrics", {}), sort_keys=True))
    state_manager.save_state()
    logger.info("Bot has shut down.")

# --- Response Processing Function ---

SHIP_TYPE_HOLDS_MAP = {
    "Scout Marauder": 25,
    # Add other ship types as needed if similar discrepancies are found
}

def process_responses(responses, game_conn, state_manager, bug_reporter, bandit_policy, config, planner=None):
    """
    Processes a list of responses from the server, updates state,
    and checks for goal completion.
    """
    for response in responses:
        game_state_before = state_manager.get_all()
        if not isinstance(response, dict):
            logger.warning("Ignoring non-object frame from server: %r", response)
            continue
        logger.info("Server response: %s", json.dumps(redact_secrets(response), separators=(',', ':')))

        # Unsolicited events have no reply_to. They are not command failures;
        # retain/deduplicate them and schedule authoritative refreshes instead.
        if response.get("reply_to") is None and (
            response.get("id") == "evt" or response.get("event") or
            (response.get("type") and response.get("status") is None)
        ):
            accepted = state_manager.ingest_server_event(response)
            if accepted:
                event_type = str(response.get("event") or response.get("type", "unknown"))
                logger.info("Received server event %s", event_type)
            continue
        
        request_id = response.get("reply_to")
        sent_command = None
        if request_id:
            sent_command = state_manager.get_pending_command(request_id)

            # A disconnect or timeout removes the pending request, but a
            # server may still return the original idempotent result. Rebuild
            # only the exact uncertain mutation when its response type is
            # structurally valid; unrelated late replies remain ignored.
            if sent_command is None:
                uncertain_mutation = (state_manager.get("uncertain_mutations", {}) or {}).get(
                    str(request_id)
                )
                if isinstance(uncertain_mutation, dict):
                    sent_command = {
                        "command": uncertain_mutation.get("command", "unknown"),
                        "data": uncertain_mutation.get("data", {}),
                        "meta": {"idempotency_key": uncertain_mutation.get("idempotency_key")},
                    }

            if sent_command is None:
                late_data = response.get("data") or {}
                uncertain_trade = state_manager.get("uncertain_trade") or {}
                expected_trade_type = {
                    "trade.buy": "trade.buy_receipt_v1",
                    "trade.sell": "trade.sell_receipt_v1",
                }.get(uncertain_trade.get("command")) if isinstance(uncertain_trade, dict) else None
                uncertain_warp = state_manager.get("uncertain_warp") or {}
                if (isinstance(uncertain_trade, dict)
                        and uncertain_trade.get("request_id") == request_id
                        and response.get("status") == "ok"
                        and response.get("type") == expected_trade_type
                        and isinstance(late_data, dict)
                        and late_data.get("lines")
                        and late_data.get("credits_remaining") is not None):
                    sent_command = {
                        "command": uncertain_trade["command"],
                        "data": {"port_id": late_data.get("port_id")},
                    }
                elif (isinstance(uncertain_warp, dict)
                      and uncertain_warp.get("request_id") == request_id
                      and response.get("status") == "ok"
                      and response.get("type") == "move.result"
                      and isinstance(late_data, dict)
                      and str(late_data.get("to_sector_id")) == str(uncertain_warp.get("to_sector_id"))):
                    sent_command = {
                        "command": "move.warp",
                        "data": {"to_sector_id": late_data.get("to_sector_id")},
                    }

            # Ignore replies to expired or previous-session requests. Schema
            # bootstrap requests are separately correlated in their own map.
            schema_request_ids = set((state_manager.get("pending_schema_requests", {}) or {}).values())
            if sent_command is None and request_id not in schema_request_ids:
                logger.warning("Ignoring late or uncorrelated reply %s (%s).", request_id, response.get("type"))
                continue

        audit_before_states = getattr(state_manager, "_live_audit_command_before", {})
        command_state_before = audit_before_states.pop(request_id, None) if request_id else None
        state_manager._live_audit_command_before = audit_before_states

        command_name = "unknown"
        if sent_command:
            command_name = sent_command.get("command", "unknown")

        response_type = response.get("type", "unknown")
        response_data = response.get("data", {}) or {}
        if not isinstance(response_data, dict):
            logger.warning("Expected object payload in %s reply; ignoring malformed payload.", response_type)
            response_data = {}
        if response_type in ("hardware.purchase_v1", "hardware.buy"):
            # A successful purchase receipt carries authoritative post-purchase
            # ship counts. Reconcile them before the next planning pass; using
            # the pre-purchase snapshot can otherwise buy the same equipment
            # repeatedly with a fresh idempotency key.
            purchased_ship = response_data.get("ship")
            if isinstance(purchased_ship, dict):
                ship = dict(state_manager.get("ship_info") or {})
                ship.update(purchased_ship)
                state_manager.update_player_info({"ship": ship})
            elif any(key in response_data for key in ("fighters", "shields", "photons", "credits")):
                ship = dict(state_manager.get("ship_info") or {})
                for key in ("fighters", "shields", "photons"):
                    if key in response_data:
                        ship[key] = response_data[key]
                state_manager.update_player_info({"ship": ship})
        if response_type == "player.list_online" and isinstance(response_data.get("players"), list):
            state_manager.set("online_players", response_data["players"])
            state_manager.set("online_players_refreshed_at", time.time())
        elif response_type == "corp.list.success" and isinstance(response_data.get("corporations"), list):
            state_manager.set("corporations", response_data["corporations"])
            # A corporation can be created after this player's previous list
            # snapshot. Keep its age so invitation workflows can refresh the
            # public catalogue instead of waiting forever on stale state.
            state_manager.set("corporations_refreshed_at", time.time())
        elif response_type in ("corp.create.success", "corp.join.success"):
            # The public success receipt includes the authoritative corporation
            # ID. Reconcile it immediately so the next planning pass does not
            # repeat create/join or fail to expose member operations.
            corp_id = response_data.get("corp_id")
            player_info = state_manager.get("player_info") or {}
            player = dict(player_info.get("player") or {})
            if corp_id is not None:
                player["corp_id"] = int(corp_id)
                state_manager.update_player_info({"player": player})
        elif response_type == "corp.invite.success":
            state_manager.set("corporation_invitation_sent", True)
            if isinstance(response_data, dict) and response_data.get("target_player_id") is not None:
                state_manager.set("corporation_invited_player_id", int(response_data["target_player_id"]))
        elif response_type == "planet.info":
            # Planet state is required by ordinary playtest clients as well as
            # population-mode tests. Keep this outside the turn-accounting
            # branch so a landed bot can restore context after any restart.
            planet = response_data.get("planet", response_data)
            if isinstance(planet, dict):
                state_manager.set("planet_info", planet)
                _remember_owned_planet(state_manager, planet)
        elif response_type in ("planet.genesis_created_v1", "planet.create.success"):
            planet = response_data.get("planet", response_data)
            state_manager.set("planet_info", planet)
            if isinstance(planet, dict):
                _remember_owned_planet(state_manager, planet)
            for refresh_name in ("sector.info", "player.my_info"):
                refresh_id = f"c-solo-{uuid.uuid4().hex[:8]}"
                refresh = {
                    "id": refresh_id,
                    "command": refresh_name,
                    "data": {"sector_id": state_manager.get("player_location_sector")} if refresh_name == "sector.info" else {},
                    "meta": {
                        "client_version": config.get("client_version"),
                        "idempotency_key": str(uuid.uuid4()),
                        "session_token": state_manager.get("session_id"),
                    },
                }
                if game_conn and game_conn.send_command(refresh):
                    state_manager.add_pending_command(refresh_id, refresh)
                    state_manager.record_ai_metric("commands_sent")
        elif response_type == "planet.colonists.get":
            state_manager.set("planet_colonist_state", response_data)
        elif response_type == "planet.colonists.set":
            transfer_state = dict(response_data)
            state_manager.set("planet_colonist_state", transfer_state)
            ship = dict(state_manager.get("ship_info") or {})
            if transfer_state.get("ship_colonists") is not None:
                ship["colonists"] = transfer_state["ship_colonists"]
                state_manager.update_player_info({"ship": ship})
            planet_id = transfer_state.get("planet_id")
            refreshes = [("planet.info", {"planet_id": planet_id}), ("ship.info", {})]
            for refresh_name, refresh_data in refreshes:
                refresh_id = f"c-colonist-refresh-{uuid.uuid4().hex[:8]}"
                refresh = {
                    "id": refresh_id, "command": refresh_name, "data": refresh_data,
                    "meta": {"client_version": config.get("client_version"),
                             "idempotency_key": str(uuid.uuid4()),
                             "session_token": state_manager.get("session_id")},
                }
                if refresh_name == "planet.info" and int(planet_id or 0) == 1:
                    continue
                if game_conn and game_conn.send_command(refresh):
                    state_manager.add_pending_command(refresh_id, refresh)
                    state_manager.record_ai_metric("commands_sent")
        elif response_type == "citadel.upgrade_started":
            state_manager.set("planet_info", None)
            planet_id = (sent_command or {}).get("data", {}).get("planet_id")
            refresh_id = f"c-solo-{uuid.uuid4().hex[:8]}"
            refresh = {
                "id": refresh_id, "command": "planet.info",
                "data": {"planet_id": planet_id},
                "meta": {"client_version": config.get("client_version"),
                         "idempotency_key": str(uuid.uuid4()),
                         "session_token": state_manager.get("session_id")},
            }
            if planet_id is not None and game_conn and game_conn.send_command(refresh):
                state_manager.add_pending_command(refresh_id, refresh)
                state_manager.record_ai_metric("commands_sent")
        elif response_type in ("planet.colonists.allocate", "planet.fighters.transferred_v1"):
            planet_id = response_data.get("planet_id")
            if response_type == "planet.colonists.allocate":
                state_manager.set("planet_info", {
                    **(state_manager.get("planet_info") or {}), **response_data,
                })
            refresh_id = f"c-planet-refresh-{uuid.uuid4().hex[:8]}"
            refresh = {
                "id": refresh_id, "command": "planet.info",
                "data": {"planet_id": planet_id},
                "meta": {"client_version": config.get("client_version"),
                         "idempotency_key": str(uuid.uuid4()),
                         "session_token": state_manager.get("session_id")},
            }
            if planet_id is not None and game_conn and game_conn.send_command(refresh):
                state_manager.add_pending_command(refresh_id, refresh)
                state_manager.record_ai_metric("commands_sent")
        if command_name == "system.hello" and response_type != "system.welcome":
            # Compatibility with pre-V3 servers: one failed hello must not
            # prevent the legacy authenticated flow from starting.
            state_manager.set("handshake_complete", True)
            logger.info("Server did not return system.welcome; continuing with legacy protocol compatibility.")
        if (request_id and response.get("status") in ("ok", "refused", "error")
                and hasattr(state_manager, "resolve_uncertain_mutation")):
            # A correlated terminal reply resolves the original write. If its
            # ack was late, the durable request ID still identifies it.
            state_manager.resolve_uncertain_mutation(request_id)
        if response.get("status") == "ok":
            state_manager.record_uncertain_mutation_readback(response_type)

        if config.get("qa_mode"):
            bug_reporter.log_response(response)
        elif (config.get("live_gameplay_audit") and sent_command
              and (command_name in ("move.warp", "trade.buy", "trade.sell")
                   or command_name in ("player.my_info", "ship.info"))):
            bug_reporter.log_response(
                response, command=sent_command,
                state_before=(command_state_before
                              or _audit_state_snapshot(game_state_before)),
            )

        # Diagnostic log for trade results
        if planner and command_name in ("trade.buy", "trade.sell"):
             planner.handle_trade_response(sent_command, response) # Pass the full sent_command dict

        if request_id:
            state_manager.remove_pending_command(request_id)

        try:
            if response.get("status") == "ok":
                if response_type == "corp.member.asset_transferred_v1":
                    state_manager.set("corporation_transfer_completed", True)
                if sent_command:
                    state_manager.record_ai_metric("commands_succeeded")
                    if command_name in ("player.ping", "session.ping"):
                        state_manager.record_ai_metric("session_pings_acknowledged")
                    if command_name not in ("player.ping", "session.ping", "player.my_info",
                                            "system.hello", "system.cmd_list", "system.describe_schema"):
                        state_manager.annotate_last_decision(
                            command=command_name, response_type=response_type,
                            response_status="ok", outcome="server_acknowledged",
                            progress_category=("navigation" if command_name.startswith("move.") else
                                               "trading" if command_name.startswith(("trade.", "port.")) else
                                               "planetary" if command_name.startswith(("planet.", "citadel.")) else
                                               "corporation" if command_name.startswith("corp.") else
                                               "combat" if command_name.startswith(("combat.", "photon.")) else
                                               "other"))
                non_gameplay_commands = {
                    "system.hello", "system.cmd_list", "system.describe_schema",
                    "auth.login", "auth.register", "player.my_info", "player.info",
                    "player.ping", "ship.info", "ship.status", "sector.info",
                    "trade.port_info", "trade.quote", "planet.info", "corp.status",
                    "corp.list", "corp.roster", "player.list_online", "ship.inspect",
                    "bank.balance", "corp.balance", "session.ping",
                    "move.autopilot.status", "shipyard.list",
                }
                if (config.get("playtest_bot") and command_name != "unknown"
                        and command_name not in non_gameplay_commands):
                    state_manager.record_ai_metric("playtest_gameplay_actions")
                    category = (
                        "movement" if command_name.startswith("move.") else
                        "trading" if command_name.startswith(("trade.", "port.")) else
                        "planet" if command_name.startswith(("planet.", "citadel.")) else
                        "corporation" if command_name.startswith("corp.") else
                        "combat" if command_name.startswith(("combat.", "photon.")) else
                        "banking" if command_name.startswith("bank.") else
                        "ship" if command_name.startswith(("ship.", "shipyard.")) else "other"
                    )
                    state_manager.record_ai_metric("playtest_category_" + category)
                    action_number = (state_manager.get("ai_metrics", {})
                                     or {}).get("playtest_gameplay_actions", 0)
                    logger.info("PLAYTEST_ACTION bot=%s number=%s command=%s",
                                config.get("playtest_bot"), action_number, command_name)
                    if not state_manager.get("playtest_first_gameplay_action_at"):
                        stamp = time.time()
                        state_manager.set("playtest_first_gameplay_action_at", stamp)
                        logger.info("PLAYTEST_FIRST_GAMEPLAY_ACTION bot=%s command=%s request_id=%s",
                                    config.get("playtest_bot"), command_name, request_id)
                # --- Feedback Loop: Record Success ---
                state_manager.record_command_success(command_name)
                if command_name == "auth.register":
                    state_manager.record_ai_metric("accounts_registered")

                state_manager.set_last_action_result({
                    "status": "ok",
                    "command": command_name,
                    "request_id": request_id,
                    "response_type": response_type,
                    "ts": response.get("ts")
                })
                # -------------------------------------

                last_action = game_state_before.get("last_action")
                last_context = game_state_before.get("last_context_key")
                reward = 0.1 # Default small reward for any successful action

                if response_type == "trade.sell_receipt_v1":
                    sold_items = response_data.get("lines", [])
                    port_id = sent_command.get("data", {}).get("port_id") if sent_command else None
                    credits_remaining = response_data.get("credits_remaining")
                    profit = 0
                    if port_id:
                        profit = state_manager.update_cargo_after_sell(
                            sold_items, port_id, credits_remaining=credits_remaining,
                            fees=response_data.get("fees"),
                        )
                        reward = profit / 1000.0 # Normalize profit for the reward
                        state_manager.record_ai_metric("trade_profit_credits", profit)
                    state_manager.record_ai_metric("trade_sales")
                    state_manager.set("trade_successful", True)
                    logger.info("Ship cargo state updated after successful trade.sell.")

                    # --- Feedback Loop: Record Success with Profit ---
                    state_manager.set_last_action_result({
                        "status": "ok",
                        "command": command_name,
                        "request_id": request_id,
                        "response_type": response_type,
                        "ts": response.get("ts"),
                        "profit_loss": profit
                    })
                    # -------------------------------------

                elif response_type == "trade.buy_receipt_v1":
                    bought_items = response_data.get("lines", [])
                    port_id = sent_command.get("data", {}).get("port_id") if sent_command else None
                    credits_remaining = response_data.get("credits_remaining")
                    if port_id:
                        state_manager.update_cargo_after_buy(
                            bought_items, port_id, credits_remaining=credits_remaining,
                            fees=response_data.get("fees"),
                        )
                    state_manager.set("trade_successful", True)
                    logger.info("Ship cargo state updated after successful trade.buy.")

                # Give feedback for the last action that led to this success
                if last_action and last_context:
                    bandit_policy.give_feedback(last_action, last_context, reward)
                    state_manager.record_learning_outcome(last_action, last_context, reward, True)

                # --- Handle Schema Responses ---
                if response_type == "system.schema":
                    schema_name = response_data.get("name")
                    if schema_name and 'schema' in response_data:
                        state_manager.add_schema(schema_name, response_data['schema'])
                        logger.debug(f"Cached schema for command: {schema_name}")
                    pending_schemas = state_manager.get("pending_schema_requests", {}) or {}
                    state_manager.set("pending_schema_requests", {
                        name: rid for name, rid in pending_schemas.items() if rid != request_id
                    })
                    continue

                if response_type in ("system.welcome", "system.capabilities"):
                    if response_data:
                        state_manager.set("server_capabilities", response_data)
                    if response_type == "system.welcome":
                        state_manager.set("handshake_complete", True)
                    state_manager.set(
                        "capability_snapshot",
                        capability_snapshot(
                            state_manager.get("ordinary_player_commands", []),
                            state_manager.get("command_schemas", {}),
                        ),
                    )
                    continue

                if response_type == "system.cmd_list":
                    commands = cache_server_command_catalogue(response_data, state_manager)
                    for command_name_to_fetch in schema_commands_to_fetch(
                        commands,
                        state_manager.get("command_schemas", {}),
                        include_all=bool(config.get("schema_bootstrap_all_commands", config.get("qa_mode", False))),
                    ):
                        # A previous payload refusal is not evidence that the
                        # public command has no schema. Retry its schema on a
                        # fresh authoritative catalogue instead of preserving
                        # a permanent stale blacklist across restarts.
                        state_manager.remove_from_schema_blacklist(command_name_to_fetch)
                        idempotency_key = str(uuid.uuid4())
                        request_id_new = f"c-{idempotency_key[:8]}"
                        schema_request = {
                            "id": request_id_new,
                            "command": "system.describe_schema",
                            "data": {"type": "command", "name": command_name_to_fetch},
                            "meta": {
                                "client_version": config.get("client_version"),
                                "idempotency_key": idempotency_key,
                                "session_token": state_manager.get("session_id"),
                            },
                        }
                        if game_conn.send_command(schema_request):
                            state_manager.record_ai_metric("commands_sent")
                            state_manager.state["pending_schema_requests"][command_name_to_fetch] = request_id_new
                        time.sleep(0.1)
                    continue

                if response_type == "auth.session":
                    state_manager.record_ai_metric("authentication_successes")
                    # Fix: Use session_token, not session (server returns session_token)
                    session_token = response_data.get("session_token") or response_data.get("session")
                    state_manager.set("session_id", session_token)
                    if config.get("register_if_missing"):
                        state_manager.set("registration_attempted", True)

                    # A saved player snapshot may be older than the server
                    # state after an interrupted action. Re-fetch the public
                    # player record before allowing a restored plan to act.
                    state_manager.set("player_info", None)
                    state_manager.set("player_location_sector", None)
                    state_manager.set("current_path", [])

                    # Clear cached ship_info to force resync from server.
                    # This prevents stale cargo from persisting if items were deleted from the DB.
                    state_manager.set("ship_info", None)
                    logger.info("Cleared cached ship_info to force resync from server.")

                    # Wrap in 'player' key if it looks like player info but is missing the wrapper
                    p_info = response_data if "player" in response_data else {"player": response_data}

                    # The session token is needed in memory for public commands,
                    # but must never be copied into the persistent player snapshot.
                    p_info = {"player": dict(p_info.get("player") or {})}
                    p_info["player"].pop("session_token", None)
                    p_info["player"].pop("session", None)

                    # Ensure player_id is present in the player object for downstream logic
                    if "player_id" in response_data and "id" not in p_info["player"]:
                        p_info["player"]["id"] = response_data["player_id"]

                    state_manager.update_player_info(p_info)

                elif response_type == "player.info" or response_type == "player.my_info":
                    if "player" not in response_data:
                        # Server might return flat data, wrap it if so
                        response_data = {"player": response_data}
                    state_manager.update_player_info(response_data)
                    _confirm_planet_landing_readback(state_manager, response_type)
                    if config.get("population_mode") and not config.get("qa_mode"):
                        state_manager.set("population_turns_unverified", False)

                elif response_type in ("ship.info", "ship.status", "ship.claimed"):
                    ship_obj = response_data.get("ship", response_data)
                    # update_player_info expects {"ship": {...}}
                    state_manager.update_player_info({"ship": ship_obj})
                    _confirm_planet_landing_readback(state_manager, response_type)
                    if response_type in ("ship.info", "ship.status"):
                        state_manager.set("shipyard_recovery_pending_refresh", False)
                    if isinstance(ship_obj, dict):
                        state_manager.record_fleet_snapshot([ship_obj])
                    if response_type == "ship.claimed":
                        logger.info(f"Bootstrap: Successfully claimed ship {ship_obj.get('id', '?')}")

                elif response_type in ("planet.land.success", "planet.launch.success"):
                    transition, planet_id = reconcile_planet_transition_receipt(
                        state_manager, response_type, response_data
                    )
                    refresh_names = (("player.my_info", "ship.info") if transition == "landed"
                                     and planet_id is not None else
                                     ("player.my_info", "ship.info") if transition == "launched" else ())
                    for refresh_name in refresh_names:
                        refresh_id = str(uuid.uuid4())
                        refresh = {
                            "id": f"c-planet-context-{refresh_id[:8]}",
                            "command": refresh_name,
                            "data": {"planet_id": planet_id} if refresh_name == "planet.info" else {},
                            "meta": {
                                "client_version": config.get("client_version"),
                                "idempotency_key": refresh_id,
                                "session_token": state_manager.get("session_id"),
                            },
                        }
                        if game_conn.send_command(refresh):
                            state_manager.add_pending_command(refresh["id"], refresh)
                            state_manager.record_ai_metric("commands_sent")

                elif response_type == "ship.repair":
                    # The repair receipt is authoritative for hull, but not
                    # sufficient for credits or the complete ship snapshot.
                    if isinstance(response_data.get("hull"), int):
                        ship = dict(state_manager.get("ship_info") or {})
                        ship["hull"] = response_data["hull"]
                        state_manager.update_player_info({"ship": ship})
                    for refresh_name in ("player.my_info", "ship.status"):
                        refresh_id = str(uuid.uuid4())
                        refresh = {
                            "id": f"c-repair-refresh-{refresh_id[:8]}",
                            "command": refresh_name,
                            "data": {},
                            "meta": {
                                "client_version": config.get("client_version"),
                                "idempotency_key": refresh_id,
                                "session_token": state_manager.get("session_id"),
                            },
                        }
                        if game_conn.send_command(refresh):
                            state_manager.add_pending_command(refresh["id"], refresh)
                            state_manager.record_ai_metric("commands_sent")

                elif response_type == "hardware.list_v1":
                    state_manager.set("hardware_catalog", response_data)
                    sector_key = str(response_data.get("sector_id", state_manager.get("player_location_sector")))
                    catalogs = state_manager.get("hardware_catalog_by_sector", {}) or {}
                    catalogs[sector_key] = response_data
                    state_manager.set("hardware_catalog_by_sector", catalogs)
                    checked = state_manager.get("hardware_catalog_checked_sectors", []) or []
                    if sector_key not in {str(value) for value in checked}:
                        checked.append(sector_key)
                    state_manager.set("hardware_catalog_checked_sectors", checked)

                elif response_type in ("deploy.fighters.list", "deploy.mines.list"):
                    recall_name = "fighters.recall" if response_type == "deploy.fighters.list" else "mines.recall"
                    entries = response_data.get("items", response_data.get("assets", []))
                    if isinstance(entries, list):
                        deployed = state_manager.get("deployed_assets", {})
                        deployed[recall_name] = entries
                        state_manager.set("deployed_assets", deployed)

                elif response_type in ("combat.fighters.deployed", "combat.mines.recalled"):
                    # Recall handlers have legacy response names; refresh the
                    # authoritative sector and ship views before planning on
                    # the returned asset counts.
                    for refresh_name in ("sector.info", "ship.status"):
                        refresh_id = str(uuid.uuid4())
                        refresh = {
                            "id": f"c-asset-refresh-{refresh_id[:8]}",
                            "command": refresh_name,
                            "data": ({"sector_id": int(state_manager.get("player_location_sector"))}
                                     if refresh_name == "sector.info"
                                     and state_manager.get("player_location_sector") is not None else {}),
                            "meta": {
                                "client_version": config.get("client_version"),
                                "idempotency_key": refresh_id,
                                "session_token": state_manager.get("session_id"),
                            },
                        }
                        if game_conn.send_command(refresh):
                            state_manager.add_pending_command(refresh["id"], refresh)
                            state_manager.record_ai_metric("commands_sent")

                elif response_type == "notice.list":
                    entries = response_data.get("items", response_data.get("notices", []))
                    if isinstance(entries, list):
                        state_manager.set("notices", entries)

                if response_type == "move.result":
                    if response.get("status") == "ok":
                        new_sector = response_data.get("to_sector_id")
                        if new_sector:
                            hazards = response_data.get("hazards") or []
                            if isinstance(hazards, list) and hazards and hasattr(state_manager, "record_sector_danger"):
                                hazard_types = [
                                    item.get("hazard_type", item.get("type"))
                                    for item in hazards if isinstance(item, dict)
                                ]
                                severity = sum(
                                    max(1, int(item.get("hull_lost", 0) or 0)
                                        + int(item.get("fighters_lost", 0) or 0)
                                        + int(item.get("shields_lost", 0) or 0))
                                    for item in hazards if isinstance(item, dict)
                                )
                                previous_danger = (state_manager.get("sector_danger", {}) or {}).get(str(new_sector))
                                state_manager.record_sector_danger(new_sector, hazard_types, severity or 1)
                                if (config.get("persist_dangerous_sectors", True)
                                        and not previous_danger
                                        and "nav.avoid.add" in game_state_before.get("server_commands", [])
                                        and game_conn is not None):
                                    avoid_id = str(uuid.uuid4())
                                    avoid_cmd = {
                                        "id": f"c-avoid-{avoid_id[:8]}",
                                        "command": "nav.avoid.add",
                                        "data": {"sector_id": int(new_sector)},
                                        "meta": {
                                            "client_version": config.get("client_version"),
                                            "idempotency_key": avoid_id,
                                            "session_token": state_manager.get("session_id"),
                                        },
                                    }
                                    if game_conn.send_command(avoid_cmd):
                                        state_manager.add_pending_command(avoid_cmd["id"], avoid_cmd)
                                        state_manager.record_ai_metric("commands_sent")
                            state_manager.record_ai_metric("completed_warps")
                            state_manager.set("player_location_sector", new_sector)
                            current_path = game_state_before.get("current_path", [])
                            if current_path:
                                try:
                                    normalized_path = [int(sector) for sector in current_path]
                                    arrived_at = normalized_path.index(int(new_sector))
                                    state_manager.set("current_path", current_path[arrived_at:])
                                except (TypeError, ValueError):
                                    # A confirmed move outside the cached route makes it stale.
                                    state_manager.set("current_path", [])
                            # Update recent sectors directly
                            recents = state_manager.get("recent_sectors", [])
                            recents.append(new_sector)
                            state_manager.set("recent_sectors", recents[-10:]) # Keep last 10
                            logger.info(f"Move confirmed. Location updated to sector {new_sector}")

                            # AUTO-REQUEST sector.info for the new location
                            logger.info(f"Auto-requesting sector.info for sector {new_sector}")
                            idemp = str(uuid.uuid4())
                            next_cmd = {
                                "id": f"c-auto-{idemp[:8]}",
                                "command": "sector.info",
                                "data": {"sector_id": int(new_sector)},
                                "meta": {
                                    "client_version": config.get("client_version"),
                                    "idempotency_key": idemp,
                                    "session_token": state_manager.get("session_id")
                                }
                            }
                            if game_conn.send_command(next_cmd):
                                state_manager.add_pending_command(next_cmd["id"], next_cmd)
                                state_manager.record_ai_metric("commands_sent")

                # --- NEW: Handle Pathfind Response ---
                if sent_command and sent_command.get("command") in ("move.pathfind", "move.autopilot.start"):
                    if response.get("status") == "ok":
                        # Pathfind uses `steps`; autopilot returns `path` and
                        # persists the route on the server for reconnects.
                        steps = response_data.get("steps") or response_data.get("path", [])
                        if steps:
                            state_manager.set("current_path", steps)
                            state_manager.record_ai_metric("planned_route_hops", max(0, len(steps) - 1))
                            if sent_command.get("command") == "move.autopilot.start":
                                state_manager.set("autopilot_status", response_data)
                            logger.info(f"Received path (steps) from server: {steps}")
                        else:
                            logger.warning("Server returned OK for move.pathfind but steps is empty.")
                    else:
                        logger.warning(f"move.pathfind failed: {response.get('error')}")
                # -------------------------------------

                elif response_type == "sector.info":
                    # Do not update player_location_sector here directly.
                    # It should be updated by update_player_info when ship.info arrives,
                    # which is the authoritative source.
                    sector_id = response_data.get("sector_id")
                    if sector_id:
                        # Add refresh timestamp so planner can detect stale data
                        response_data["_last_refreshed"] = time.time()
                        state_manager.update_sector_data(str(sector_id), response_data)
                        public_players = response_data.get("players", response_data.get("ships_present", []))
                        if isinstance(public_players, list):
                            state_manager.record_opponent_observation(public_players, sector_id=sector_id)

                elif response_type == "sector.search_results_v1":
                    # This is a public result from the authenticated sector.search
                    # command, not an inferred scan or hidden database lookup.
                    shipyards = normalize_public_shipyard_search(response_type, response_data)
                    state_manager.set("shipyard_search_results", shipyards)
                    state_manager.set("shipyard_search_complete", True)

                elif response_type in ("shipyard.list_v1", "shipyard.list"):
                    # V3 publishes a catalogue with `available` and
                    # `used_available`; it is not an owned-fleet snapshot.
                    catalog = normalize_shipyard_catalog(response_type, response_data)
                    state_manager.set("shipyard_catalog", catalog)
                    available = catalog["available"]
                    state_manager.set("shipyard_list", available)
                    logger.info("Shipyard catalogue refreshed at sector %s (%d hulls, %d used).",
                                catalog.get("sector_id"),
                                len(available) if isinstance(available, list) else 0,
                                len(catalog.get("used_available", []) or []))

                elif response_type == "shipyard.upgraded_v1":
                    # The receipt confirms the mutation but omits capacity.
                    # Store only fields the server returned, then hold this
                    # goal until a fresh ship snapshot arrives. Otherwise a
                    # stale zero-hold snapshot can buy another upgrade using
                    # a new idempotency key.
                    ship = dict(state_manager.get("ship_info") or {})
                    ship.update({
                        "id": response_data.get("ship_id"),
                        "type_id": response_data.get("new_type_id"),
                        "name": response_data.get("new_ship_name"),
                    })
                    state_manager.update_player_info({"ship": ship})
                    state_manager.set("shipyard_recovery_pending_refresh", True)
                    for refresh_name in ("ship.info", "player.my_info"):
                        refresh_id = str(uuid.uuid4())
                        refresh = {
                            "id": f"c-shipyard-refresh-{refresh_id[:8]}",
                            "command": refresh_name,
                            "data": {},
                            "meta": {
                                "client_version": config.get("client_version"),
                                "idempotency_key": refresh_id,
                                "session_token": state_manager.get("session_id"),
                            },
                        }
                        if game_conn.send_command(refresh):
                            state_manager.add_pending_command(refresh["id"], refresh)
                            state_manager.record_ai_metric("commands_sent")

                elif response_type == "dock.status_v1":
                    ship = dict(state_manager.get("ship_info") or {})
                    ship["ported"] = response_data.get("port_id", 0)
                    state_manager.set("ship_info", ship)

                elif response_type == "trade.port_info":
                    port_id = response_data.get("port", {}).get("id")
                    sector_id = response_data.get("port", {}).get("sector_id") # Note: Server might send 'sector' not 'sector_id' in port object, let's check logs.
                    # Log says: "sector":1.
                    if not sector_id:
                         sector_id = response_data.get("port", {}).get("sector")

                    if port_id and sector_id:
                        state_manager.update_port_info(str(sector_id), response_data.get("port"))

                elif response_type == "trade.quote":
                    state_manager.update_price_cache(response_data)

                elif response_type in ("player.computer.trade_routes", "player.computer.trade_routes_v1"):
                    state_manager.set("market_recommendations", response_data.get("routes", []))
                    state_manager.set("market_recommendations_refreshed_at", time.time())

                elif response_type in ("move.autopilot.status_v1", "move.autopilot.controlled_v1", "move.autopilot.route_v1"):
                    state_manager.set("autopilot_status", response_data)
                    route = response_data.get("path")
                    if isinstance(route, list) and route:
                        state_manager.set("current_path", route)

                elif response_type == "move.autopilot.stopped_v1":
                    state_manager.set("autopilot_status", response_data)
                    state_manager.set("current_path", response_data.get("path", []))

                elif response_type == "bank.balance":
                    state_manager.set("bank_balance", response_data.get("balance", 0))
                elif response_type == "bank.deposit.confirmed":
                    if response_data.get("new_balance") is not None:
                        state_manager.set("bank_balance", response_data["new_balance"])

                # Victory status is intentionally copied only from an
                # authoritative server response; local estimates cannot claim
                # a win.  Combat receipts likewise feed recovery and opponent
                # threat memory without storing private credentials.
                if isinstance(response_data, dict):
                    victory = response_data.get("victory") or response_data.get("game_status")
                    if isinstance(victory, dict):
                        state_manager.set("victory_status", victory)
                    if response_type.startswith("combat.") or response_type.startswith("player.attack"):
                        state_manager.record_combat_outcome(response_data)
                        target = response_data.get("target_player_id") or response_data.get("player_id")
                        if target is not None:
                            state_manager.record_opponent_observation([{
                                "player_id": target,
                                "sector_id": state_manager.get("player_location_sector"),
                                "hostile": True,
                            }])

                    game_state_after = state_manager.get_all()
                    strategy_plan = game_state_after.get("strategy_plan", [])

                    if not state_manager.validate_state():
                        bug_reporter.report_state_inconsistency(game_state_after, "State inconsistency detected after server response.")

                    if strategy_plan:
                        current_goal = strategy_plan[0]
                        if is_goal_complete(current_goal, game_state_after, response_data, response_type):
                            logger.info(f"Strategic goal '{current_goal}' is complete. Removing from plan.")
                            state_manager.record_ai_metric("goals_completed")
                            if str(current_goal).strip().lower().startswith("goto:"):
                                state_manager.record_ai_metric("navigation_goals_completed")
                            else:
                                state_manager.record_ai_metric("strategic_goals_completed")
                            strategy_plan.pop(0)
                            state_manager.set("strategy_plan", strategy_plan)

            else: # Error or refused
                if sent_command:
                    state_manager.record_ai_metric("commands_refused")
                error_data = response.get("error", {})
                error_msg = error_data.get("message", "Unknown error")
                error_code = error_data.get("code")
                annotate = getattr(state_manager, "annotate_last_decision", None)
                if callable(annotate):
                    annotate(command=command_name, response_type=response_type,
                             response_status="refused", error_code=error_code,
                             server_reason=str(error_msg)[:240], outcome="server_refused")
                if str(error_code) == "505" or "deadline exceeded" in str(error_msg).lower():
                    state_manager.record_ai_metric("server_request_deadline_errors")
                if command_name in ("auth.login", "auth.register"):
                    state_manager.record_ai_metric("authentication_failures")
                
                logger.warning("Server refused command '%s': %s (Code: %s)", command_name, error_msg, error_code)
                
                # --- Feedback Loop: Record Failure ---
                state_manager.set_last_action_result({
                    "status": "error",
                    "command": command_name,
                    "error_code": error_code,
                    "error_msg": error_msg,
                    "sector_id": game_state_before.get("player_location_sector"),
                    "sent_command": sent_command
                })
                # -------------------------------------
                
                state_manager.record_command_failure(command_name)

                if command_name == "move.autopilot.status":
                    state_manager.set("autopilot_status_checked", True)
                    state_manager.set("autopilot_status", {"state": "unavailable"})

                # A refused hop must not leave a persisted route silently
                # running. Stop it; replanning will request a fresh route.
                if (command_name == "move.warp"
                        and "move.autopilot.stop" in game_state_before.get("server_commands", [])
                        and (game_state_before.get("autopilot_status") or {}).get("state") == "running"):
                    idemp = str(uuid.uuid4())
                    stop_cmd = {
                        "id": f"c-ap-stop-{idemp[:8]}",
                        "command": "move.autopilot.stop",
                        "data": {},
                        "meta": {"client_version": config.get("client_version"),
                                 "idempotency_key": idemp,
                                 "session_token": state_manager.get("session_id")},
                    }
                    if game_conn.send_command(stop_cmd):
                        state_manager.add_pending_command(stop_cmd["id"], stop_cmd)
                        state_manager.record_ai_metric("commands_sent")
                    state_manager.set("autopilot_status", {"state": "stopped"})
                    state_manager.set("current_path", [])

                last_action = game_state_before.get("last_action")
                last_context = game_state_before.get("last_context_key")
                if last_action and last_context:
                    bandit_policy.give_feedback(last_action, last_context, -1.0) # Negative reward for failure
                    state_manager.record_learning_outcome(last_action, last_context, -1.0, False)
                
                # 1104 is AUTH_ERR_BAD_REQUEST, not a schema lookup error.
                # Do not permanently disable a real command because one
                # request had an invalid or stale payload.


                # -----------------------------------------------------

                if error_code == 1489:
                    logger.warning("Received 1489 Insufficient Turns. Setting turns to 0.")
                    state_manager.set_turns(0)

                if error_code in (401, 1401):
                    logger.warning(f"Received {error_code} Not Authenticated. Clearing session_id to force re-login.")
                    state_manager.set("session_id", None)
                    time.sleep(1) # Cool down to avoid tight loop
                    break # Stop processing further responses in this batch

                if sent_command and command_name == "move.warp" and error_code == 1453:
                    to_sector_id = sent_command.get("data", {}).get("to_sector_id")
                    if to_sector_id is not None:
                        logger.warning(f"Move.warp to sector {to_sector_id} failed with 'No warp link'. RE-SYNCING location.")
                        state_manager.add_to_warp_blacklist(to_sector_id)
                        state_manager.set("current_path", [])
                        strategy_plan = state_manager.get("strategy_plan", [])
                        if strategy_plan and strategy_plan[0].startswith("goto:"):
                            state_manager.set("strategy_plan", strategy_plan[1:])
                        
                        # FORCE RE-SYNC: Clear cached location and fetch fresh info
                        state_manager.set("player_location_sector", None)
                        
                        for cmd_name in ["player.my_info", "ship.info"]:
                            idemp = str(uuid.uuid4())
                            re_sync_cmd = {
                                "id": f"c-resync-{idemp[:8]}",
                                "command": cmd_name,
                                "data": {},
                                "meta": {
                                    "client_version": config.get("client_version"),
                                    "idempotency_key": idemp,
                                    "session_token": state_manager.get("session_id")
                                }
                            }
                            if game_conn.send_command(re_sync_cmd):
                                state_manager.add_pending_command(re_sync_cmd["id"], re_sync_cmd)
                                state_manager.record_ai_metric("commands_sent")

                elif sent_command and command_name == "move.warp":
                    # Other refusals (hazards, insufficient turns, etc.) can
                    # also leave local route assumptions stale. Drop the
                    # pending plan and refresh location before selecting again.
                    state_manager.set("current_path", [])
                    state_manager.set("strategy_plan", [])
                    state_manager.set("player_info", None)
                    state_manager.set("ship_info", None)
                    state_manager.set("player_location_sector", None)
                    idemp = str(uuid.uuid4())
                    refresh_cmd = {
                        "id": f"c-resync-move-{idemp[:8]}",
                        "command": "player.my_info",
                        "data": {},
                        "meta": {"client_version": config.get("client_version"),
                                 "idempotency_key": idemp,
                                 "session_token": state_manager.get("session_id")},
                    }
                    if game_conn.send_command(refresh_cmd):
                        state_manager.add_pending_command(refresh_cmd["id"], refresh_cmd)
                        state_manager.record_ai_metric("commands_sent")

                if sent_command and command_name in ("move.pathfind", "move.autopilot.start"):
                    strategy_plan = state_manager.get("strategy_plan", [])
                    if strategy_plan and strategy_plan[0].startswith("goto:"):
                        logger.info("Pathfinding was refused; discarding the unreachable navigation goal.")
                        state_manager.set("current_path", [])
                        state_manager.set("autopilot_status", {"state": "stopped"})
                        state_manager.set("strategy_plan", strategy_plan[1:])
                
                if sent_command and command_name == "trade.port_info" and error_code == 1800:
                    current_sector = game_state_before.get("player_location_sector")
                    if current_sector is not None:
                        logger.warning(f"trade.port_info failed for sector {current_sector}. Marking has_port=False.")
                        state_manager.update_sector_data(str(current_sector), {"has_port": False})
                        # Also clear it from port_info_by_sector
                        port_info_by_sector = state_manager.get("port_info_by_sector", {})
                        if str(current_sector) in port_info_by_sector:
                            del port_info_by_sector[str(current_sector)]
                            state_manager.set("port_info_by_sector", port_info_by_sector)

                # --- FIX: Handle "Port not buying" (1701) by clearing price cache ---
                if sent_command and command_name == "trade.sell" and error_code == 1701:
                    port_id = sent_command.get("data", {}).get("port_id")
                    items = sent_command.get("data", {}).get("items", [])
                    if port_id and items:
                        port_id_str = str(port_id)
                        price_cache = state_manager.get("price_cache", {})
                        if port_id_str in price_cache and "sell" in price_cache[port_id_str]:
                            for item in items:
                                comm = canon_commodity(item.get("commodity"))
                                if comm in price_cache[port_id_str]["sell"]:
                                    logger.info(f"Received 1701 (Port Full/Not Buying) for {comm} at port {port_id}. Clearing cached sell price to stop retries.")
                                    # Set to None to indicate unknown/invalid, forcing a re-quote or skip
                                    price_cache[port_id_str]["sell"][comm] = None 
                            state_manager.set("price_cache", price_cache)
                
                # --- FIX: Handle "Not Enough Commodity" (1453) by re-syncing ship info ---
                if sent_command and command_name == "trade.sell" and error_code == 1453:
                    logger.warning("Received 1453 (Not Enough Commodity). Forcing re-sync of ship info.")
                    idemp = str(uuid.uuid4())
                    resync_cmd = {
                        "id": f"c-resync-1453-{idemp[:8]}",
                        "command": "ship.info",
                        "data": {},
                        "meta": {
                            "client_version": config.get("client_version"),
                            "idempotency_key": idemp,
                            "session_token": state_manager.get("session_id")
                        }
                    }
                    if game_conn.send_command(resync_cmd):
                        state_manager.add_pending_command(resync_cmd["id"], resync_cmd)
                        state_manager.record_ai_metric("commands_sent")

                # --- FIX: Handle "Insufficient Funds" (1403) by re-syncing player info ---
                if sent_command and command_name == "trade.buy" and error_code == 1403:
                    logger.warning("Received 1403 (Insufficient Funds). Forcing re-sync of player info.")
                    idemp = str(uuid.uuid4())
                    resync_cmd = {
                        "id": f"c-resync-1403-{idemp[:8]}",
                        "command": "player.my_info",
                        "data": {},
                        "meta": {
                            "client_version": config.get("client_version"),
                            "idempotency_key": idemp,
                            "session_token": state_manager.get("session_id")
                        }
                    }
                    if game_conn.send_command(resync_cmd):
                        state_manager.add_pending_command(resync_cmd["id"], resync_cmd)
                        state_manager.record_ai_metric("commands_sent")
                # --------------------------------------------------------------------

                if config.get("qa_mode") and classify_protocol_outcome(response, config) != "expected_refusal":
                    bug_reporter.triage_protocol_error(command_name, response, game_state_before, error_code, error_msg)
                if command_name == "auth.register" and error_code == 1000:
                    # Database outages are transient; permit one later public
                    # registration attempt after the command backoff expires.
                    state_manager.set("registration_attempted", False)

            if (config.get("population_mode") and not config.get("qa_mode") and sent_command
                    and population_turn_readback_required(command_name, config)):
                # Fail closed until an authoritative player.my_info confirms
                # the server's current turn balance, including after refusals.
                state_manager.set("population_turns_unverified", True)
                audit_already_reads_player = (
                    config.get("live_gameplay_audit")
                    and command_name in ("move.warp", "trade.buy", "trade.sell")
                    and response.get("status") == "ok"
                )
                if not audit_already_reads_player:
                    idempotency_key = str(uuid.uuid4())
                    refresh = {
                        "id": f"c-pop-turns-{idempotency_key[:8]}",
                        "command": "player.my_info",
                        "data": {},
                        "meta": {
                            "client_version": config.get("client_version"),
                            "idempotency_key": idempotency_key,
                            "session_token": state_manager.get("session_id"),
                        },
                    }
                    if game_conn.send_command(refresh):
                        state_manager.add_pending_command(refresh["id"], refresh)
                        state_manager.record_ai_metric("commands_sent")

        except Exception as e:
            logger.error("Critical error in process_responses: %s", e, exc_info=True)
        else:
            uncertain = state_manager.get("uncertain_trade") or {}
            expected_receipt = {"trade.buy": "trade.buy_receipt_v1",
                                "trade.sell": "trade.sell_receipt_v1"}.get(command_name)
            if (expected_receipt and isinstance(uncertain, dict) and uncertain.get("request_id") == request_id
                    and ((response.get("status") == "ok" and response_type == expected_receipt
                          and response_data.get("lines") and response_data.get("credits_remaining") is not None)
                         # A correlated explicit protocol failure is not an
                         # ambiguous timeout: the server has returned the
                         # transaction result (including rollback failures).
                         or response.get("status") in ("refused", "error"))):
                state_manager.set("uncertain_trade", None)
            warp = state_manager.get("uncertain_warp") or {}
            if (command_name == "move.warp" and isinstance(warp, dict) and warp.get("request_id") == request_id
                    and ((response.get("status") == "ok" and response_type == "move.result"
                          and response_data.get("to_sector_id") is not None
                          and str(response_data["to_sector_id"]) == str(warp.get("to_sector_id")))
                         # A correlated protocol error is an explicit refusal,
                         # not an ambiguous timeout. Retrying it forever can
                         # hammer a known-invalid edge until the server rate
                         # limiter closes the connection.
                         or response.get("status") in ("refused", "error"))):
                state_manager.set("uncertain_warp", None)
            readback_warp = state_manager.get("uncertain_warp") or {}
            if (command_name in ("player.my_info", "player.info")
                    and isinstance(readback_warp, dict)
                    and readback_warp.get("readback_request_id") == request_id
                    and response.get("status") == "ok"):
                readback_player = response_data.get("player", response_data)
                observed_sector = readback_player.get(
                    "current_sector", readback_player.get("sector_id", readback_player.get("sector")))
                if observed_sector is not None:
                    state_manager.set("player_location_sector", observed_sector)
                    outcome = reconcile_warp_readback(readback_warp, observed_sector)
                    target_sector = readback_warp.get("to_sector_id")
                    if outcome == "arrived":
                        logger.info("Uncertain warp confirmed at destination sector %s.", target_sector)
                    elif outcome == "not_moved":
                        logger.info("Uncertain warp did not move; planner may safely re-evaluate from sector %s.",
                                    observed_sector)
                    else:
                        logger.warning("Uncertain warp resolved at unexpected sector %s; discarding stale route.",
                                       observed_sector)
                        state_manager.set("current_path", [])
                    state_manager.set("uncertain_warp", None)

        # In live audit mode, verify every successful movement/trade against
        # fresh public player and ship reads. Refusals are logged as refusals
        # and are never escalated as gameplay defects.
        if config.get("live_gameplay_audit") and sent_command:
            if request_id in getattr(state_manager, "_live_audit_requests", {}):
                action_id = state_manager._live_audit_requests.pop(request_id)
                contexts = getattr(state_manager, "_live_audit_contexts", {})
                context = contexts.get(action_id)
                if context:
                    if response.get("status") == "ok":
                        context["received"].add(command_name)
                    else:
                        context["failed_readbacks"].append(command)
                    if context["received"] == context["required"] or context["failed_readbacks"]:
                        bug_reporter.log_gameplay_outcome(
                            identity=config.get("player_username", "unknown"),
                            seed=config.get("test_seed"),
                            command=context["command"], response=context["response"],
                            state_before=context["state_before"],
                            state_after=state_manager.get_all(),
                            events=state_manager.get("server_events", []),
                            state_after_source=("readback unavailable" if context["failed_readbacks"]
                                                else "player.my_info + ship.info readback"),
                            verification_complete=(not context["failed_readbacks"]
                                                   and context.get("before_verified", False)),
                        )
                        if context["failed_readbacks"]:
                            logger.warning("Live gameplay readback incomplete for %s.", action_id)
                        contexts.pop(action_id, None)

            elif (command_name in ("move.warp", "trade.buy", "trade.sell")
                  and response.get("status") == "ok"):
                action_id = request_id or str(uuid.uuid4())
                context = {
                    "command": sent_command,
                    "response": response,
                    "state_before": (command_state_before
                                     or _audit_state_snapshot(game_state_before)),
                    "before_verified": command_state_before is not None,
                    "required": {"player.my_info", "ship.info"},
                    "received": set(),
                    "failed_readbacks": [],
                }
                contexts = getattr(state_manager, "_live_audit_contexts", {})
                contexts[action_id] = context
                requests = getattr(state_manager, "_live_audit_requests", {})
                for refresh_name in sorted(context["required"]):
                    idempotency_key = str(uuid.uuid4())
                    refresh = {
                        "id": f"c-audit-{idempotency_key[:8]}",
                        "command": refresh_name,
                        "data": {},
                        "meta": {
                            "client_version": config.get("client_version"),
                            "idempotency_key": idempotency_key,
                            "session_token": state_manager.get("session_id"),
                        },
                    }
                    if game_conn.send_command(refresh):
                        state_manager.add_pending_command(refresh["id"], refresh)
                        requests[refresh["id"]] = action_id
                        state_manager.record_ai_metric("commands_sent")
                        bug_reporter.log_command(
                            refresh, state_before=_audit_state_snapshot(state_manager.get_all())
                        )
                    else:
                        context["failed_readbacks"].append(refresh_name)
                state_manager._live_audit_contexts = contexts
                state_manager._live_audit_requests = requests
                if context["failed_readbacks"]:
                    bug_reporter.log_gameplay_outcome(
                        identity=config.get("player_username", "unknown"),
                        seed=config.get("test_seed"), command=context["command"],
                        response=context["response"], state_before=context["state_before"],
                        state_after=state_manager.get_all(),
                        events=state_manager.get("server_events", []),
                        state_after_source="readback unavailable",
                        verification_complete=False,
                    )
                    contexts.pop(action_id, None)
            elif command_name in ("move.warp", "trade.buy", "trade.sell"):
                bug_reporter.log_gameplay_outcome(
                    identity=config.get("player_username", "unknown"),
                    seed=config.get("test_seed"), command=sent_command,
                    response=response, state_before=game_state_before,
                    state_after=state_manager.get_all(),
                    events=state_manager.get("server_events", []),
                    state_after_source="refusal response; no mutation expected",
                )



if __name__ == "__main__":
    # Allow: python main.py config_bot_001.json
    cfg = sys.argv[1] if len(sys.argv) > 1 else "config.json"
    main(cfg)
