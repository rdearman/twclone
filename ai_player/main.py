import copy
import socket
import json
import time
import datetime
import ollama
import logging
import os
import signal
import sys
import argparse
import random
import uuid
from collections import deque
from typing import Any, Dict, List, Optional, Tuple, Union

from planner import FiniteStatePlanner
from bug_reporter import BugReporter
from bandit_policy import BanditPolicy, make_context_key
from parse_protocol import parse_protocol_markdown
from jsonschema import validate, ValidationError
from planner import FiniteStatePlanner

# --- Global Shutdown Flag ---
shutdown_flag = False

def signal_handler(sig, frame):
    """Handles Ctrl+C to trigger a graceful shutdown."""
    global shutdown_flag
    logger.info("Shutdown signal received. Cleaning up...")
    shutdown_flag = True

class Config:
    """Loads and holds configuration from a JSON file."""
    def __init__(self, path='config.json'):
        # Construct the absolute path to config.json relative to the script's directory
        script_dir = os.path.dirname(__file__)
        config_path = os.path.join(script_dir, path)
        try:
            with open(config_path, 'r') as f: # Use config_path here
                self.settings = json.load(f)
        except FileNotFoundError:
            print(f"FATAL: Configuration file not found at {config_path}") # Update message
            sys.exit(1)
        except json.JSONDecodeError:
            print(f"FATAL: Could not decode JSON from {config_path}") # Update message
            sys.exit(1)

    def get(self, key, default=None):
        return self.settings.get(key, default)

class StateManager:
    """Handles loading, saving, and accessing the AI's state."""
    def __init__(self, state_file_path, config): # Add config parameter
        self.path = state_file_path
        self.config = config # Store config
        self.state = self._get_default_state() # Initialize with default state
        self.load()

    def _get_default_state(self):
        initial_credits = self.config.get("initial_credits", 0.0)
        default_state = {
            "stage": "bootstrap",
            "session_token": None,
            "player_location_sector": 0,
            "last_server_response": {},
            "last_command_sent_id": "",
            "pending_commands": {},
            "last_commands_history": [],
            "working_commands": [],
            "broken_commands": [],
            "received_responses": [],

            # Exploration / mapping
            "visited_sectors": set(),
            "visited_ports": set(),
            "previous_sector_id": None,
            "sectors_with_info": set(),

            # Schema / protocol knowledge
            "normalized_commands": {},
            "schemas_to_fetch": [],
            "cached_schemas": {},

            # Info/quote throttling & QA
            "sector_info_fetched_for": {},
            "last_sector_info_request_time": 0,
            "last_ship_info_request_time": 0,
            "last_player_info_request_time": 0,
            "port_info_failures_per_sector": {},
            "sector_snapshot_by_id": {}, # For adjacency checks

            # Bandit / learning
            "q_table": {},
            "n_table": {},
            "last_reward": 0.0,
            "last_context_key": None,
            "last_action": None,
            "last_stage": None,

            # QA flags
            "bug_reported_this_tick": False,
            
            # Existing keys from previous version to merge
            "player_username": self.config.get("player_username"),
            "client_version": self.config.get("client_version"),
            "current_credits": initial_credits,
            "previous_credits": initial_credits,
            "last_responses_history": [],
            "commands_to_try": [],
            "ship_info": None,
            "port_info_by_sector": {},
            "last_bank_info_request_time": 0,
            "bank_balance": 0.0, # New field to store the bank balance
            "price_cache": {},
            "next_allowed": {},
            "server_capabilities": {},
            "last_processed_command_name": None,
            "sector_data": {},
            "new_sector_discovered": False,
            "trade_successful": False,
            "new_port_discovered": False,
            "last_trade_revenue": 0.0, # New field to store revenue from the last trade.sell
            "last_trade_cost": 0.0,    # New field to store cost from the last trade.buy
            "rate_limit_info": {},
            "waiting_for_turns": False, # New flag to indicate if the bot is waiting for turns
            "next_allowed_action_time": 0, # Timestamp when the bot can act again after running out of turns
            "llm_recovery_command": None, # New field to store LLM suggested recovery command
            "commands_seen_ok": set(),
            "commands_seen_error": set(),
            "error_codes_seen": set(),
            "ports_visited_by_class": {}, # e.g., {"port_class_type": set(port_ids)}
            "special_ports_seen": set(),
            "zones_covered": set(),
            "total_steps": 0,
            "commands_never_tried": set(), # Will be populated from normalized_commands
            "deduplicated_bugs": {}, # Stores {"bug_hash": {"count": N, "latest_timestamp": "..."}},

            # Command Cooldowns (new)
            "command_cooldowns": {}, # Stores {"command_name": {"cooldown_until": timestamp}}

            # QA Metrics (new)
            "qa_metrics": {
                "commands": {}, # Stores metrics per command, e.g., {"command_name": {"total_calls": N, "successful_calls": N, "failed_calls": N, "last_called_timestamp": T, "error_codes_seen": set()}}
                "scenarios": { # Stores flags for specific scenario coverage
                    "seen_1402_error": False, # "Not enough cargo"
                    "seen_1403_error": False, # "Insufficient cargo"
                    "seen_1307_error": False, # "Out of turns"
                    "seen_1405_sell_rejection": False, # "Port is not buying this commodity right now."
                    "tested_zero_holds": False,
                    "tested_full_holds": False,
                    "tested_empty_credits": False,
                    "tested_high_credits": False,
                }
            }
        }
        logger.debug(f"StateManager._get_default_state: Initial player_location_sector: {default_state['player_location_sector']}")
        return default_state

    def _merge_state(self, target_dict, source_dict):
        for key, value in source_dict.items():
            if key in target_dict:
                if isinstance(target_dict[key], dict) and isinstance(value, dict):
                    self._merge_state(target_dict[key], value)
                elif isinstance(target_dict[key], list) and isinstance(value, list):
                    # Extend the list with unique elements from the source list
                    for item in value:
                        if item not in target_dict[key]:
                            target_dict[key].append(item)
                elif isinstance(target_dict[key], set):
                    # If target is a set, ensure source value is also treated as a set for merging
                    if key == "error_codes_seen":
                        logger.debug(f"Merging error_codes_seen: target_type={type(target_dict[key])}, source_type={type(value)}, target_value={target_dict[key]}, source_value={value}")
                    if isinstance(value, list):
                        target_dict[key].update(set(value))
                    elif isinstance(value, set):
                        target_dict[key].update(value)
                    elif value is not None:
                        # If target is a set, but source value is not iterable (e.g., a string, int),
                        # this indicates a type mismatch. Overwrite if not None.
                        target_dict[key] = value
                elif value is not None: # Only overwrite if source value is not None
                    target_dict[key] = value
            elif value is not None: # Add new keys if not None
                target_dict[key] = value

    def load(self):
        if os.path.exists(self.path):
            try:
                with open(self.path, 'r') as f:
                    loaded_data = json.load(f)
                    # Convert lists back to sets after loading
                    if 'visited_sectors' in loaded_data and isinstance(loaded_data['visited_sectors'], list):
                        loaded_data['visited_sectors'] = set(loaded_data['visited_sectors'])
                    if 'visited_ports' in loaded_data and isinstance(loaded_data['visited_ports'], list):
                        loaded_data['visited_ports'] = set(loaded_data['visited_ports'])
                    if 'sectors_with_info' in loaded_data and isinstance(loaded_data['sectors_with_info'], list):
                        loaded_data['sectors_with_info'] = set(loaded_data['sectors_with_info'])
                    if 'commands_seen_ok' in loaded_data and isinstance(loaded_data['commands_seen_ok'], list):
                        loaded_data['commands_seen_ok'] = set(loaded_data['commands_seen_ok'])
                    if 'commands_seen_error' in loaded_data and isinstance(loaded_data['commands_seen_error'], list):
                        loaded_data['commands_seen_error'] = set(loaded_data['commands_seen_error'])
                    if 'error_codes_seen' in loaded_data and isinstance(loaded_data['error_codes_seen'], list):
                        loaded_data['error_codes_seen'] = set(loaded_data['error_codes_seen'])
                    if 'special_ports_seen' in loaded_data and isinstance(loaded_data['special_ports_seen'], list):
                        loaded_data['special_ports_seen'] = set(loaded_data['special_ports_seen'])
                    if 'zones_covered' in loaded_data and isinstance(loaded_data['zones_covered'], list):
                        loaded_data['zones_covered'] = set(loaded_data['zones_covered'])
                    if 'commands_never_tried' in loaded_data and isinstance(loaded_data['commands_never_tried'], list):
                        loaded_data['commands_never_tried'] = set(loaded_data['commands_never_tried'])
                    if 'deduplicated_bugs' in loaded_data and isinstance(loaded_data['deduplicated_bugs'], dict):
                        # Ensure inner dicts are loaded correctly, no set conversion needed
                        pass 
                    
                    # NEW: Handle qa_metrics deserialization
                    if 'qa_metrics' in loaded_data and isinstance(loaded_data['qa_metrics'], dict):
                        if 'commands' in loaded_data['qa_metrics'] and isinstance(loaded_data['qa_metrics']['commands'], dict):
                            for cmd_name, cmd_metrics in loaded_data['qa_metrics']['commands'].items():
                                if 'error_codes_seen' in cmd_metrics and isinstance(cmd_metrics['error_codes_seen'], list):
                                    cmd_metrics['error_codes_seen'] = set(cmd_metrics['error_codes_seen'])
                        # Scenarios are booleans, no special handling needed

                    self._merge_state(self.state, loaded_data) # Merge loaded data into current state
                    logger.debug(f"StateManager.load: player_location_sector after merge: {self.state['player_location_sector']}")
                logger.info(f"Successfully loaded state from {self.path}")
            except json.JSONDecodeError:
                logger.error(f"Could not decode state file {self.path}. Starting with fresh state.")
        else:
            logger.info("No state file found. Starting with fresh state.")

        # Never persist in-flight commands across restarts
        if self.state.get("pending_commands"):
            logger.warning(
                "Clearing %d pending_commands on load; "
                "do not persist in-flight commands across restarts.",
                len(self.state["pending_commands"])
            )
        self.state["pending_commands"] = {}
        self.state["last_command_sent_id"] = ""
        
        # Post-load reconciliation: Force bootstrap if session token is missing or authentication failed
        session_token_present = self.state.get("session_token") is not None and self.state.get("session_token") != ""
        auth_success = self.state.get("last_server_response", {}).get("command") == "auth.login" and self.state.get("last_server_response", {}).get("status") == "ok"

        if not session_token_present and not auth_success:
            if self.state["stage"] != "bootstrap":
                logger.warning(f"Session not active. Forcing stage from '{self.state['stage']}' to 'bootstrap'.")
            self.state["stage"] = "bootstrap"

    def save(self):
        # Create a serializable deep copy of the state
        state_to_save = copy.deepcopy(self.state)
        if 'visited_sectors' in state_to_save:
            state_to_save['visited_sectors'] = list(state_to_save['visited_sectors'])
        if 'visited_ports' in state_to_save:
            state_to_save['visited_ports'] = list(state_to_save['visited_ports'])
        if 'sectors_with_info' in state_to_save:
            state_to_save['sectors_with_info'] = list(state_to_save['sectors_with_info'])
        if 'commands_seen_ok' in state_to_save:
            state_to_save['commands_seen_ok'] = list(state_to_save['commands_seen_ok'])
        if 'commands_seen_error' in state_to_save:
            state_to_save['commands_seen_error'] = list(state_to_save['commands_seen_error'])
        if 'error_codes_seen' in state_to_save:
            state_to_save['error_codes_seen'] = list(state_to_save['error_codes_seen'])
        if 'special_ports_seen' in state_to_save:
            state_to_save['special_ports_seen'] = list(state_to_save['special_ports_seen'])
        if 'zones_covered' in state_to_save:
            state_to_save['zones_covered'] = list(state_to_save['zones_covered'])
        if 'commands_never_tried' in state_to_save:
            state_to_save['commands_never_tried'] = list(state_to_save['commands_never_tried'])
        # No special handling for deduplicated_bugs, as it's already a dict of dicts

        # NEW: Handle qa_metrics serialization
        if 'qa_metrics' in state_to_save and isinstance(state_to_save['qa_metrics'], dict):
            if 'commands' in state_to_save['qa_metrics'] and isinstance(state_to_save['qa_metrics']['commands'], dict):
                for cmd_name, cmd_metrics in state_to_save['qa_metrics']['commands'].items():
                    if 'error_codes_seen' in cmd_metrics and isinstance(cmd_metrics['error_codes_seen'], set):
                        cmd_metrics['error_codes_seen'] = list(cmd_metrics['error_codes_seen'])


        try:
            with open(self.path, 'w') as f:
                json.dump(state_to_save, f, indent=4)
            logger.info(f"Successfully saved state to {self.path}")
        except (IOError, TypeError) as e:
            logger.error(f"Could not write state to {self.path}: {e}")

    def get(self, key: str, default: Any = None) -> Any:
        value = self.state.get(key, default)
        logger.debug(f"StateManager.get: Key='{key}', Type={type(value)}, Value={value}")
        if key == "qa_metrics" and isinstance(value, dict):
            if "commands" in value and isinstance(value["commands"], dict):
                for cmd_name, cmd_metrics in value["commands"].items():
                    if "error_codes_seen" in cmd_metrics:
                        logger.debug(f"StateManager.get:   - Command='{cmd_name}', error_codes_seen Type={type(cmd_metrics['error_codes_seen'])}, Value={cmd_metrics['error_codes_seen']}")
        return value

    def set(self, key, value):
        logger.debug(f"StateManager.set: Key='{key}', Type={type(value)}")
        if key == "qa_metrics" and isinstance(value, dict):
            if "commands" in value and isinstance(value["commands"], dict):
                for cmd_name, cmd_metrics in value["commands"].items():
                    if "error_codes_seen" in cmd_metrics:
                        logger.debug(f"StateManager.set:   - Command='{cmd_name}', error_codes_seen Type={type(cmd_metrics['error_codes_seen'])}, Value={cmd_metrics['error_codes_seen']}")
        self.state[key] = value

    def get_all(self):
        return self.state

class GameConnection:
    """A resilient class for handling TCP connection to the game server."""
    def __init__(self, host, port, state_manager, bug_reporter):
        self.host = host
        self.port = port
        self.sock = None
        self.buffer = ""
        self.state_manager = state_manager # Store state_manager
        self.bug_reporter = bug_reporter # Store bug_reporter
        self.connect()

    def connect(self):
        """Connects to the server with exponential backoff."""
        attempt = 0
        while not shutdown_flag:
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.settimeout(1.0)
                logger.info(f"Attempting to connect to {self.host}:{self.port}...")
                self.sock.connect((self.host, self.port))
                logger.info("Connection successful.")
                self.post_connect() # Call post_connect after successful connection
                return True
            except socket.error as e:
                self.sock = None
                wait_time = min(2 ** attempt, 60)
                logger.error(f"Connection failed: {e}. Retrying in {wait_time} seconds...")
                time.sleep(wait_time)
                attempt += 1
        return False

    def post_connect(self):
        logger.info("Performing post-connection handshake...")
        hello_command_id = str(uuid.uuid4())
        hello_command = {
            "id": hello_command_id,
            "ts": datetime.datetime.now().isoformat() + "Z",
            "command": "system.hello",
            "data": {"client_version": self.state_manager.config.get("client_version")}, # Move client_version to data
            "meta": {} # Clear meta as client_version is now in data
        }
        self.send_command(hello_command)
        self.state_manager.set("last_command_sent_id", hello_command_id)
        logger.info("Handshake command sent.")
        # Remove the immediate system.capabilities send; let planner issue it next.

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None
            logger.info("Socket closed.")

    def send_command(self, command_json: dict) -> str:
        if not self.sock:
            logger.error("No active connection to send command.")
            if not self.connect():
                return None

        pending_cmds = self.state_manager.get("pending_commands", {})
        if pending_cmds:
            logger.warning(f"Attempted to send command '{command_json.get('command')}' but there are already pending commands. Enforcing single in-flight policy. Pending: {list(pending_cmds.keys())}")
            return None # Prevent sending if a command is already in flight

        # Add idempotency key to meta if not already present
        # Idempotency key is now generated upstream in the main loop
        # if "meta" not in command_json:
        #     command_json["meta"] = {}
        # if "idempotency_key" not in command_json["meta"]:
        #     command_json["meta"]["idempotency_key"] = str(uuid.uuid4())

        cmd_name = command_json.get("command")
        # Update last_*_request_time timestamps
        if cmd_name == "ship.info":
            self.state_manager.set("last_ship_info_request_time", time.time())
        elif cmd_name == "sector.info":
            self.state_manager.set("last_sector_info_request_time", time.time())
        elif cmd_name == "player.my_info":
            self.state_manager.set("last_player_info_request_time", time.time())
        elif cmd_name.startswith("bank."):
            self.state_manager.set("last_bank_info_request_time", time.time())
            if cmd_name == "bank.deposit":
                # Store pre-deposit state for invariant check
                self.state_manager.set("pre_deposit_hand_credits", self.state_manager.get("current_credits", 0.0))
                self.state_manager.set("pre_deposit_bank_balance", self.state_manager.get("bank_balance", 0.0))
                logger.debug(f"Stored pre-deposit state: hand={self.state_manager.get('pre_deposit_hand_credits')}, bank={self.state_manager.get('pre_deposit_bank_balance')}")
            elif cmd_name == "trade.buy":
                # Store pre-buy cargo state for invariant check
                current_cargo = self.state_manager.get("ship_info", {}).get("cargo", {})
                commodities_to_buy = command_json.get("data", {}).get("items", [])
                pre_buy_cargo_snapshot = {}
                for item in commodities_to_buy:
                    commodity = item.get("commodity", "").lower()
                    pre_buy_cargo_snapshot[commodity] = current_cargo.get(commodity, 0)
                self.state_manager.set("pre_buy_cargo_snapshot", pre_buy_cargo_snapshot)
                logger.debug(f"Stored pre-buy cargo snapshot: {pre_buy_cargo_snapshot}")

        full_command = json.dumps(command_json) + "\n"
        try:
            self.sock.sendall(full_command.encode('utf-8'))
            logger.info(f"Sent command: {full_command.strip()}")
            
            pending_cmds[command_json['id']] = command_json
            self.state_manager.set("pending_commands", pending_cmds)
            
            return command_json["id"]
        except socket.error as e:
            logger.error(f"Failed to send command: {e}. Closing socket.")
            self.close()
            return None

    def read_responses(self):
        if not self.sock:
            return []

        responses = []
        try:
            data = self.sock.recv(4096).decode('utf-8')
            if not data:
                logger.warning("Connection closed by server.")
                self.close()
                return []
            
            self.buffer += data
            while '\n' in self.buffer:
                line, self.buffer = self.buffer.split('\n', 1)
                if line:
                    try:
                        response_json = json.loads(line)
                        responses.append(response_json)
                        logger.debug(f"Received response: {response_json}")
                    except json.JSONDecodeError as e:
                        self.bug_reporter.triage_protocol_error(
                            {"command": "unknown", "id": "unknown"}, # Cannot determine sent command for malformed response
                            {"raw_line": line, "error_message": str(e)}, # Raw data for debugging
                            self.state_manager.get_all(),
                            f"JSON DECODE ERROR: {e} - Raw line: {line}",
                            self.state_manager.get("last_commands_history", []),
                            self.state_manager.get("last_responses_history", []),
                            sent_schema=None, # No schema for malformed response
                            validated=False # Not applicable
                        )
        except socket.timeout:
            pass # Normal for idle state
        except socket.error as e:
            logger.error(f"Socket error during read: {e}. Closing socket.")
            self.close()
        
        return responses

def process_responses(state_manager: StateManager, game_conn: GameConnection, bug_reporter: BugReporter):
    responses = game_conn.read_responses()
    now = time.time() # Moved definition of 'now' here
    for response in responses:
        state_manager.set("last_server_response", response)
        logger.info(f"Server response: {json.dumps(response)}")
        
        history = state_manager.get("last_responses_history")
        history.append(response)
        state_manager.set("last_responses_history", history[-5:])
        
        rate_limit_data = response.get("meta", {}).get("rate_limit")
        if rate_limit_data:
            reset_window_length = rate_limit_data.get("reset", 0)
            rate_limit_data["reset_at"] = time.time() + reset_window_length if reset_window_length > 0 else 0
            state_manager.set("rate_limit_info", rate_limit_data)

        reply_to_id = response.get("reply_to")
        if reply_to_id:
            pending_cmds = state_manager.get("pending_commands", {})
            sent_command = pending_cmds.pop(reply_to_id, None)
            state_manager.set("pending_commands", pending_cmds)

            if sent_command:
                command_name = sent_command.get("command")
                state_manager.set("last_processed_command_name", command_name)
                
                if response.get("status") == "ok":
                    commands_seen_ok = state_manager.get("commands_seen_ok", set())
                    if command_name not in commands_seen_ok:
                        commands_seen_ok.add(command_name)
                        state_manager.set("commands_seen_ok", commands_seen_ok)
                        state_manager.set("new_command_discovered", True) # Flag for reward
                    
                    # --- QA Metrics Update: Successful Call ---
                    qa_metrics = state_manager.get_all()["qa_metrics"]
                    # Ensure 'commands' key exists
                    if "commands" not in qa_metrics:
                        qa_metrics["commands"] = {}
                    cmd_metrics = qa_metrics["commands"].setdefault(command_name, {
                        "total_calls": 0, "successful_calls": 0, "failed_calls": 0,
                        "last_called_timestamp": 0, "error_codes_seen": set()
                    })
                    cmd_metrics["total_calls"] += 1
                    cmd_metrics["successful_calls"] += 1
                    cmd_metrics["last_called_timestamp"] = time.time()
                    # --- END QA Metrics Update ---

                    # Remove from commands_never_tried if successful
                    commands_never_tried = state_manager.get("commands_never_tried", set())
                    if command_name in commands_never_tried:
                        commands_never_tried.remove(command_name)
                        state_manager.set("commands_never_tried", commands_never_tried)

                    # Validate response against schema
                    cached_schemas = state_manager.get("cached_schemas", {})
                    command_schemas = cached_schemas.get(command_name)
                    if command_schemas and command_schemas.get("response_schema"):
                        try:
                            validate(instance=response.get("data", {}), schema=command_schemas["response_schema"])
                            logger.debug(f"Response for {command_name} validated successfully against schema.")
                        except ValidationError as e:
                            logger.error(f"Response validation error for {command_name}: {e.message}")
                            bug_reporter.triage_protocol_error(
                                sent_command=sent_command,
                                response=response,
                                agent_state=state_manager.get_all(),
                                error_message=f"Response schema validation failed: {e.message}",
                                last_commands_history=state_manager.get("last_commands_history", []),
                                last_responses_history=state_manager.get("last_responses_history", []),
                                sent_schema=command_schemas.get("request_schema"),
                                validated=False
                            )
                        except Exception as e:
                            logger.error(f"Unexpected error during response schema validation for {command_name}: {e}")
                    
                    # --- Domain-specific state updates based on response type ---
                    response_type = response.get("type")
                    response_data = response.get("data", {}) or {}
                    
                    # --- ADDED BLOCK: Force sync planner location with ship's true location ---
                    ship_data = response_data.get("ship") or response_data
                    if response_type in ("ship.info", "ship.status", "player.info") and ship_data.get("location"):
                        true_sector = ship_data.get("location").get("sector_id")
                        if true_sector is not None:
                            state_manager.set("player_location_sector", true_sector)
                            logger.info(f"Re-syncing planner location to ship's true location: {true_sector}")
                    # --- END ADDED BLOCK ---

                    # 1) Auth / session
                    if command_name == "auth.login" and response.get("status") == "ok":
                        # Try common field names for the session token
                        session_token = (
                            response_data.get("session_token")
                            or response_data.get("session")
                            or response_data.get("token")
                        )
                        if session_token:
                            state_manager.set("session_token", session_token)
                            logger.info(f"Authenticated. Session token set: {session_token[:8]}…")
                        else:
                            logger.warning("auth.login returned ok but no obvious session token found in data")

                    if response_type == "session.hello":
                        current_sector = response_data.get("current_sector")
                        if current_sector is not None:
                            state_manager.set("player_location_sector", current_sector)
                            visited = state_manager.get("visited_sectors", set())
                            visited.add(current_sector)
                            state_manager.set("visited_sectors", visited)
                            logger.info(f"Session hello: current sector set to {current_sector}")

                    # 2) Ship info (ship.info / ship.status)
                    if response_type in ("ship.info", "ship.status"):
                        # Normalise shape: TWClone uses data.ship in ship.status
                        ship = response_data.get("ship") or response_data
                        if ship:
                            # Use ship.type.name for actual holds, as ship.holds seems to be max holds
                            holds_str = ship.get("type", {}).get("name", "0")
                            try:
                                ship["holds"] = int(holds_str)
                            except ValueError:
                                logger.warning(f"Could not parse ship holds from type.name: {holds_str}. Defaulting to 0.")
                                ship["holds"] = 0
                            state_manager.set("ship_info", ship)
                            loc = ship.get("location", {})
                            sector_id = loc.get("sector_id")
                            if sector_id is not None:
                                state_manager.set("player_location_sector", sector_id)
                                visited = state_manager.get("visited_sectors", set())
                                visited.add(sector_id)
                                state_manager.set("visited_sectors", visited)
                                logger.info(f"Ship in sector {sector_id}")
                    
                    # NEW: Update cargo after successful trade.sell
                    if response_type == "trade.sell_receipt_v1" and response.get("status") == "ok":
                        sold_items = response_data.get("lines", [])
                        ship_info = state_manager.get("ship_info", {})
                        current_cargo = ship_info.get("cargo", {})
                        
                        total_revenue = 0
                        for item in sold_items:
                            commodity = item.get("commodity", "").lower()
                            quantity_sold = item.get("quantity", 0)
                            item_value = item.get("value", 0)
                            total_revenue += item_value
                            if commodity in current_cargo:
                                current_cargo[commodity] = max(0, current_cargo[commodity] - quantity_sold)
                                logger.info(f"Updated cargo: Sold {quantity_sold} of {commodity}. Remaining: {current_cargo[commodity]}")
                        
                        ship_info["cargo"] = current_cargo
                        state_manager.set("ship_info", ship_info)
                        state_manager.set("trade_successful", True) # For reward calculation
                        state_manager.set("last_trade_revenue", total_revenue) # Store revenue
                        logger.info("Ship cargo updated after successful trade.sell.")

                    # NEW: Update cargo after successful trade.buy
                    if response_type == "trade.buy_receipt_v1" and response.get("status") == "ok":
                        bought_items = response_data.get("lines", [])
                        ship_info = state_manager.get("ship_info", {})
                        current_cargo = ship_info.get("cargo", {})

                        total_cost = 0
                        for item in bought_items:
                            # Server response might use uppercase 'ORE'
                            commodity = item.get("commodity", "").lower() 
                            quantity_bought = item.get("quantity", 0)
                            item_value = item.get("value", 0)
                            total_cost += item_value
                            if commodity:
                                current_cargo[commodity] = current_cargo.get(commodity, 0) + quantity_bought
                                logger.info(f"Updated cargo: Bought {quantity_bought} of {commodity}. New total: {current_cargo[commodity]}")
                        
                        ship_info["cargo"] = current_cargo
                        state_manager.set("ship_info", ship_info)
                        state_manager.set("trade_successful", True) # For reward calculation
                        state_manager.set("last_trade_cost", total_cost) # Store cost
                        logger.info("Ship cargo updated after successful trade.buy.")

                        # --- Invariant Check: Trade Buy Cargo Increase ---
                        pre_buy_cargo_snapshot = state_manager.get("pre_buy_cargo_snapshot", {})
                        invariant_violated = False
                        for item in bought_items:
                            commodity = item.get("commodity", "").lower()
                            quantity_bought = item.get("quantity", 0)
                            
                            pre_buy_qty = pre_buy_cargo_snapshot.get(commodity, 0)
                            post_buy_qty = current_cargo.get(commodity, 0)

                            if (post_buy_qty - pre_buy_qty) != quantity_bought:
                                invariant_violated = True
                                bug_reporter.triage_invariant_failure(
                                    invariant_name="trade_buy_cargo_increase",
                                    current_value={
                                        "commodity": commodity,
                                        "pre_buy_quantity": pre_buy_qty,
                                        "post_buy_quantity": post_buy_qty,
                                        "quantity_bought_expected": quantity_bought
                                    },
                                    expected_condition=f"Cargo of {commodity} increased by {quantity_bought}",
                                    agent_state=state_manager.get_all(),
                                    error_message=f"Trade buy invariant violated for {commodity}: Expected increase of {quantity_bought}, but got {post_buy_qty - pre_buy_qty}"
                                )
                                logger.error(f"Trade buy invariant violated for {commodity}: Expected increase of {quantity_bought}, but got {post_buy_qty - pre_buy_qty}")
                        
                        if not invariant_violated:
                            logger.info("Trade buy cargo increase invariant check passed.")
                        
                        # Clear pre-buy state
                        state_manager.set("pre_buy_cargo_snapshot", {})
                        # --- END Invariant Check: Trade Buy Cargo Increase ---

                    # NEW: Robust credit handling
                    credits = None
                    if response_type in ("ship.status", "player.info") and response.get("status") == "ok":
                        data = response.get("data", {}) or {}
                        if "credits" in data:
                            credits = data["credits"]
                        elif "player" in data and "credits" in data["player"]:
                            credits = data["player"]["credits"]
                        elif "ship" in data and "credits" in data["ship"]:
                            credits = data["ship"]["credits"]
                    elif response_type == "bank.balance" and response.get("status") == "ok":
                        data = response.get("data", {}) or {}
                        if "balance" in data:
                            credits = data["balance"]
                            state_manager.set("bank_balance", credits) # Store bank balance
                        elif "bank_account" in data and "balance" in data["bank_account"]:
                            credits = data["bank_account"]["balance"]
                            state_manager.set("bank_balance", credits) # Store bank balance
                    
                    # --- Invariant Check: Bank Deposit ---
                    if command_name == "bank.deposit" and response.get("status") == "ok":
                        pre_deposit_hand_credits = state_manager.get("pre_deposit_hand_credits")
                        pre_deposit_bank_balance = state_manager.get("pre_deposit_bank_balance")
                        
                        post_deposit_hand_credits = state_manager.get("current_credits")
                        post_deposit_bank_balance = state_manager.get("bank_balance")

                        if pre_deposit_hand_credits is not None and pre_deposit_bank_balance is not None:
                            total_credits_before = pre_deposit_hand_credits + pre_deposit_bank_balance
                            total_credits_after = post_deposit_hand_credits + post_deposit_bank_balance

                            # Use a small tolerance for floating point comparisons
                            if abs(total_credits_before - total_credits_after) > 0.001:
                                bug_reporter.triage_invariant_failure(
                                    invariant_name="bank_deposit_conservation",
                                    current_value={
                                        "before_hand": pre_deposit_hand_credits,
                                        "before_bank": pre_deposit_bank_balance,
                                        "after_hand": post_deposit_hand_credits,
                                        "after_bank": post_deposit_bank_balance
                                    },
                                    expected_condition="total credits before deposit == total credits after deposit",
                                    agent_state=state_manager.get_all(),
                                    error_message=f"Bank deposit invariant violated: Total credits changed from {total_credits_before} to {total_credits_after}"
                                )
                                logger.error(f"Bank deposit invariant violated: Total credits changed from {total_credits_before} to {total_credits_after}")
                            else:
                                logger.info("Bank deposit invariant check passed.")
                        
                        # Clear pre-deposit state
                        state_manager.set("pre_deposit_hand_credits", None)
                        state_manager.set("pre_deposit_bank_balance", None)
                    # --- END Invariant Check: Bank Deposit ---

                    if credits is not None:
                        try:
                            credits_float = float(credits)
                            prev = state_manager.get("current_credits", 0.0)
                            state_manager.set("previous_credits", prev)
                            state_manager.set("current_credits", credits_float)
                            logger.info("Updated credits: %s -> %s", prev, credits_float)
                        except (ValueError, TypeError):
                            logger.error(f"Could not parse credits value: {credits}")

                    # 3) Sector info
                    if response_type == "sector.info":
                        sector_id = response_data.get("sector_id")
                        if sector_id is not None:
                            # Record timing for freshness / cooldown logic
                            fetched_for = state_manager.get("sector_info_fetched_for", {})
                            fetched_for[str(sector_id)] = now
                            state_manager.set("sector_info_fetched_for", fetched_for)

                            # Cache full sector snapshot
                            sector_data = state_manager.get("sector_data", {}) or {}
                            sector_data[str(sector_id)] = response_data
                            state_manager.set("sector_data", sector_data)

                            # Update current location & visited
                            state_manager.set("player_location_sector", sector_id)
                            visited = state_manager.get("visited_sectors", set())
                            visited.add(sector_id)
                            state_manager.set("visited_sectors", visited)

                            # Flag whether this is a newly seen sector
                            new_sector_flag = str(sector_id) not in sector_data
                            state_manager.set("new_sector_discovered", bool(new_sector_flag))

                            # Add to sectors_with_info
                            sectors_with_info = state_manager.get("sectors_with_info", set())
                            sectors_with_info.add(sector_id)
                            state_manager.set("sectors_with_info", sectors_with_info)

                            # Update zones_covered
                            zone = response_data.get("zone")
                            if zone:
                                zones_covered = state_manager.get("zones_covered", set())
                                zones_covered.add(zone)
                                state_manager.set("zones_covered", zones_covered)

                            logger.info(f"Updated sector.info cache for sector {sector_id}")

                    # 4) Port info
                    if response_type == "trade.port_info" and response.get("status") == "ok":
                        port = response_data.get("port", {})
                        port_id = port.get("id") or response_data.get("port_id") # More robust
                        current_sector = str(state_manager.get("player_location_sector"))

                        if port_id is not None and current_sector:
                            visited_ports = state_manager.get("visited_ports", set())
                            if port_id not in visited_ports:
                                visited_ports.add(port_id)
                                state_manager.set("visited_ports", visited_ports)
                                state_manager.set("new_port_discovered", True)
                                logger.info(f"Discovered new port_id={port_id}")
                            else:
                                logger.info(f"Refreshed port info for port_id={port_id}")
                            
                            # Extract commodities from on_hand data
                            commodities_at_port = []
                            if port.get("ore_on_hand") is not None:
                                commodities_at_port.append("ore")
                            if port.get("organics_on_hand") is not None:
                                commodities_at_port.append("organics")
                            if port.get("equipment_on_hand") is not None:
                                commodities_at_port.append("equipment")
                            
                            # Add the extracted commodities list to the response_data for easier access
                            response_data["commodities"] = commodities_at_port

                            # Update port_info_by_sector map
                            pi_by_sector = state_manager.get("port_info_by_sector", {})
                            sector_ports = pi_by_sector.setdefault(current_sector, {})
                            sector_ports[str(port_id)] = response_data # Store the modified response_data
                            state_manager.set("port_info_by_sector", pi_by_sector)
                            logger.info(f"Cached port {port_id} info for sector {current_sector}. Commodities: {commodities_at_port}")

                            # Update ports_visited_by_class and special_ports_seen
                            port_class = port.get("class")
                            if port_class:
                                ports_by_class = state_manager.get("ports_visited_by_class", {})
                                ports_by_class.setdefault(port_class, set()).add(port_id)
                                state_manager.set("ports_visited_by_class", ports_by_class)
                            
                            special_ports = state_manager.get("special_ports_seen", set())
                            special_ports.add(port_id) # For now, all ports are "special" for tracking
                            state_manager.set("special_ports_seen", special_ports)

                        else:
                            logger.warning(f"trade.port_info 'ok' but missing port_id ({port_id}) or current_sector ({current_sector})")

                    # 5) Trade quote → price cache
                    if response_type == "trade.quote" and response.get("status") == "ok":
                        port_id = response_data.get("port_id")
                        commodity = response_data.get("commodity")
                        buy_price = response_data.get("buy_price")
                        sell_price = response_data.get("sell_price")

                        if port_id is not None and commodity:
                            price_cache = state_manager.get("price_cache", {}) or {}
                            # Try to key by *current* sector and port; fall back to port only
                            current_sector = state_manager.get("player_location_sector")
                            if current_sector is None:
                                # Fallback: treat sector_id 0 as "unknown"
                                current_sector = 0

                            sector_prices = price_cache.setdefault(str(current_sector), {})
                            port_prices = sector_prices.setdefault(str(port_id), {})
                            port_prices[commodity] = {
                                "buy": buy_price,
                                "sell": sell_price,
                                "timestamp": now,
                            }

                            state_manager.set("price_cache", price_cache)
                            state_manager.set("price_cache_updated_this_tick", True)
                            logger.info(f"Updated price cache: sector={current_sector}, port={port_id}, {commodity}={buy_price}/{sell_price}")
                        else:
                            logger.warning("trade.quote ok but missing port_id or commodity")

                    # 6) Movement (move.warp etc.) — if server echoes new sector in data
                    if response_type == "move.result" and response.get("status") == "ok":
                        new_sector = response_data.get("current_sector") or response_data.get("to_sector_id")
                        if new_sector is not None:
                            state_manager.set("player_location_sector", new_sector)
                            visited = state_manager.get("visited_sectors", set())
                            visited.add(new_sector)
                            state_manager.set("visited_sectors", visited)
                            state_manager.set("new_sector_visited_this_tick", True)
                            logger.info(f"Moved to sector {new_sector}")
                else: # Error or refused
                    # --- QA Metrics Update: Failed Call ---
                    qa_metrics = state_manager.get_all()["qa_metrics"]
                    if "commands" not in qa_metrics:
                        qa_metrics["commands"] = {}
                    cmd_metrics = qa_metrics["commands"].setdefault(command_name, {
                        "total_calls": 0, "successful_calls": 0, "failed_calls": 0,
                        "last_called_timestamp": 0, "error_codes_seen": set()
                    })
                    cmd_metrics["total_calls"] += 1
                    cmd_metrics["failed_calls"] += 1
                    cmd_metrics["last_called_timestamp"] = time.time()
                    # --- END QA Metrics Update ---

                    commands_seen_error = state_manager.get("commands_seen_error", set())
                    if command_name not in commands_seen_error:
                        commands_seen_error.add(command_name)
                        state_manager.set("commands_seen_error", commands_seen_error)
                        state_manager.set("new_command_discovered", True) # Flag for reward

                    # Remove from commands_never_tried if it resulted in an error
                    commands_never_tried = state_manager.get("commands_never_tried", set())
                    if command_name in commands_never_tried:
                        commands_never_tried.remove(command_name)
                        state_manager.set("commands_never_tried", commands_never_tried)

                    error_data = response.get('error', {})
                    error_msg = error_data.get('message', 'Unknown error')
                    error_code = error_data.get('code')

                    # Add error code to qa_metrics
                    if error_code:
                        logger.debug(f"Type of cmd_metrics['error_codes_seen'] before add: {type(cmd_metrics['error_codes_seen'])}")
                        cmd_metrics["error_codes_seen"].add(error_code)
                    error_codes_seen = state_manager.get("error_codes_seen", set())
                    if error_code not in error_codes_seen:
                        error_codes_seen.add(error_code)
                        state_manager.set("error_codes_seen", error_codes_seen)
                        state_manager.set("new_error_code_discovered", True) # Flag for reward

                    # --- QA Metrics Update: Scenario Flags ---
                    # Ensure 'scenarios' key exists
                    if "scenarios" not in qa_metrics:
                        qa_metrics["scenarios"] = {}
                    qa_scenarios = qa_metrics["scenarios"]
                    if error_code == 1402: # "Not enough cargo"
                        qa_scenarios["seen_1402_error"] = True
                    elif error_code == 1403: # "Insufficient cargo"
                        qa_scenarios["seen_1403_error"] = True
                    elif error_code == 1307: # "You have run out of turns"
                        qa_scenarios["seen_1307_error"] = True
                    elif error_code == 1405 and command_name == "trade.sell": # "Port is not buying this commodity right now."
                        qa_scenarios["seen_1405_sell_rejection"] = True
                    # --- END QA Metrics Update: Scenario Flags ---


                    # --- START NEW FIX ---
                    if error_code == 1403: # "Insufficient cargo space"
                        logger.warning("Received 'Insufficient cargo' error. Forcing ship.info refresh.")
                        # By setting ship_info to None, the planner's invariant check
                        # will force a 'ship.info' command on the next tick.
                        state_manager.set("ship_info", None) 
                        state_manager.set("last_ship_info_request_time", 0) # Bypass cooldown
                    elif error_code == 1307: # "You have run out of turns"
                        logger.warning("Received 'Out of turns' error. Pausing all actions for 1 hour.")
                        # Set a global cooldown for all commands for 1 hour
                        state_manager.set("next_allowed_action_time", time.time() + 3600) # 1 hour cooldown
                        state_manager.set("waiting_for_turns", True)
                        # Do NOT increment command_retry_info for this error, as it's a global game state, not a command-specific failure.
                    elif error_code == 1405 and command_name == "trade.sell": # "Port is not buying this commodity right now."
                        logger.warning(f"Received 'Port not buying commodity' error for {sent_command.get('data', {}).get('items', [{}])[0].get('commodity')} at port {sent_command.get('data', {}).get('port_id')}. Marking as unsellable in price_cache.")
                        port_id = sent_command.get('data', {}).get('port_id')
                        commodity = sent_command.get('data', {}).get('items', [{}])[0].get('commodity')
                        current_sector = str(state_manager.get("player_location_sector"))

                        if port_id and commodity and current_sector:
                            price_cache = state_manager.get("price_cache", {})
                            sector_prices = price_cache.setdefault(current_sector, {})
                            port_prices = sector_prices.setdefault(str(port_id), {})
                            
                            # Mark the commodity as unsellable at this port
                            if commodity in port_prices:
                                port_prices[commodity]["sell"] = 0 # Set sell price to 0 or None
                                port_prices[commodity]["not_sellable_until"] = time.time() + state_manager.config.get("trade_retry_cooldown", 300) # Cooldown for this specific commodity
                                state_manager.set("price_cache", price_cache)
                                logger.info(f"Updated price_cache for {commodity} at port {port_id} in sector {current_sector}: marked as unsellable.")
                        # Do NOT increment command_retry_info for trade.sell for this error, as the command itself isn't broken.
                    else: # Generic error handling
                        logger.warning(f"Unhandled error code {error_code} for command '{command_name}': {error_msg}. Consulting LLM for recovery.")
                        # Call LLM for recovery
                        recovery_llm_command = get_ollama_response(
                            state_manager.get_all(),
                            state_manager.config.get("ollama_model"),
                            "UNHANDLED_ERROR_RECOVERY" # New stage for LLM prompt
                        )
                        if recovery_llm_command:
                            logger.info(f"LLM suggested recovery: {recovery_llm_command}")
                            # Store this command to be picked up by the planner in the next tick
                            state_manager.set("llm_recovery_command", recovery_llm_command)
                        else:
                            logger.error("LLM recovery failed for unhandled error. Falling back to default error handling.")
                            # Fallback to existing bug reporting and retry logic
                            if state_manager.config.get("qa_mode"):
                                bug_reporter.triage_protocol_error(
                                    sent_command=sent_command,
                                    response=response,
                                    agent_state=state_manager.get_all(),
                                    error_message=response.get("error", "unknown"),
                                    last_commands_history=state_manager.get("last_commands_history", []),
                                    last_responses_history=state_manager.get("last_responses_history", []),
                                    sent_schema=None, # schema is not available here
                                    validated=False # validation is not available here
                                )

                            # NEW: Apply cooldown for failed command
                            command_cooldown_duration = state_manager.config.get("command_cooldown_duration", 300)
                            command_cooldowns = state_manager.get("command_cooldowns", {})
                            command_cooldowns[command_name] = {"cooldown_until": time.time() + command_cooldown_duration}
                            state_manager.set("command_cooldowns", command_cooldowns)
                            logger.warning(f"Command '{command_name}' failed with: {error_msg}. Placed on cooldown until {time.time() + command_cooldown_duration}.")
                    # --- END NEW FIX ---
        state_manager.set("qa_metrics", qa_metrics) # Update state_manager with modified qa_metrics

def load_schemas_from_docs(state_manager: StateManager):
    logger.info("Loading schemas from protocol markdown file...")
    protocol_path = state_manager.config.get("protocol_doc_path")
    if not protocol_path:
        logger.error("FATAL: 'protocol_doc_path' not found in config. Cannot load schemas.")
        sys.exit(1)

    # Construct the absolute path to protocol_path relative to the script's directory
    script_dir = os.path.dirname(__file__)
    absolute_protocol_path = os.path.join(script_dir, "..", protocol_path) # Assuming protocol_path is relative to project root

    try:
        # Use the parser you wrote
        extracted_commands = parse_protocol_markdown(absolute_protocol_path)
        
        cached_schemas = {}
        for cmd in extracted_commands:
            try:
                request_schema_dict = json.loads(cmd['request_schema'])
                response_schema_dict = json.loads(cmd['response_schema'])
                cached_schemas[cmd['name']] = {
                    "request_schema": request_schema_dict,
                    "response_schema": response_schema_dict,
                    "response_type": cmd['response_type']
                }
            except json.JSONDecodeError:
                logger.warning(f"Could not parse schema for {cmd['name']}. Request: {cmd['request_schema']}, Response: {cmd['response_schema']}")
                cached_schemas[cmd['name']] = {
                    "request_schema": {},
                    "response_schema": {},
                    "response_type": cmd['response_type']
                }

        state_manager.set("cached_schemas", cached_schemas)
        # We no longer need to fetch schemas, so clear the list
        state_manager.set("schemas_to_fetch", []) 
        logger.info(f"Successfully loaded and cached {len(cached_schemas)} schemas from docs.")
    
    except Exception as e:
        logger.critical(f"Failed to load schemas from protocol file: {e}", exc_info=True)
        # You might want to sys.exit(1) here if schemas are critical

def calculate_and_apply_reward(state_manager: StateManager, bandit_policy: BanditPolicy):
    last_action = state_manager.get("last_action")
    last_context_key = state_manager.get("last_context_key")

    if not last_action or not last_context_key:
        return # Nothing to apply reward to

    reward_weights = state_manager.config.get("reward_weights", {})

    # 1. Calculate reward based on state deltas
    previous_credits = state_manager.get("previous_credits", state_manager.get("current_credits", 0.0))
    current_credits = state_manager.get("current_credits", 0.0)
    
    # Base reward from credit change
    reward = (current_credits - previous_credits) * reward_weights.get("credits_change", 0.0)
    state_manager.set("previous_credits", current_credits)

    # --- QA-Oriented Rewards ---
    qa_metrics = state_manager.get("qa_metrics", {})
    qa_scenarios = qa_metrics.get("scenarios", {})

    # Reward for first time an action is taken in a specific context
    # This check needs to be done against the n_table directly, as it's the source of truth for action counts
    q_table, n_table = bandit_policy.get_tables()
    context_n_table = n_table.get(last_context_key, {})
    if context_n_table.get(last_action, 0) == 1: # If this is the first time this action was taken in this context
        reward += reward_weights.get("first_action_in_context", 0.0)
        logger.info(f"QA Reward: +{reward_weights.get('first_action_in_context', 0.0)} for first action '{last_action}' in context '{last_context_key}'.")

    # Reward for new scenario flags being set (only reward once per flag)
    for scenario_key, is_set in qa_scenarios.items():
        # Check if the flag was just set in this tick and hasn't been rewarded before
        # This requires a mechanism to track if a scenario has been "discovered" and rewarded
        # For simplicity, we'll assume setting it to True is a "new discovery" for now.
        # A more robust solution would involve a separate set of "rewarded_scenarios".
        if is_set and state_manager.get(f"rewarded_scenario_{scenario_key}", False) == False:
            reward += reward_weights.get(f"new_scenario_{scenario_key}", 0.0)
            logger.info(f"QA Reward: +{reward_weights.get(f'new_scenario_{scenario_key}', 0.0)} for new scenario '{scenario_key}'.")
            state_manager.set(f"rewarded_scenario_{scenario_key}", True) # Mark as rewarded
    # --- END QA-Oriented Rewards ---

    # Add intrinsic rewards for exploration, etc.
    if state_manager.get("new_sector_discovered"):
        reward += reward_weights.get("new_sector_discovered", 0.0)
        state_manager.set("new_sector_discovered", False)
    
    if state_manager.get("new_port_discovered"):
        reward += reward_weights.get("new_port_discovered", 0.0)
        state_manager.set("new_port_discovered", False)

    if state_manager.get("new_command_discovered"):
        reward += reward_weights.get("new_command_discovered", 0.0)
        state_manager.set("new_command_discovered", False)

    if state_manager.get("new_error_code_discovered"):
        reward += reward_weights.get("new_error_code_discovered", 0.0)
        state_manager.set("new_error_code_discovered", False)

    # Incorporate trade profit/loss directly
    if state_manager.get("trade_successful"):
        last_trade_revenue = state_manager.get("last_trade_revenue", 0.0)
        last_trade_cost = state_manager.get("last_trade_cost", 0.0)
        trade_profit_loss = last_trade_revenue - last_trade_cost
        
        reward += trade_profit_loss * reward_weights.get("trade_profit_loss", 0.0)
        logger.info(f"Trade resulted in profit/loss: {trade_profit_loss}. Adjusted reward by {trade_profit_loss * reward_weights.get('trade_profit_loss', 0.0)}.")
        
        state_manager.set("trade_successful", False)
        state_manager.set("last_trade_revenue", 0.0)
        state_manager.set("last_trade_cost", 0.0)

    # 2. Update the bandit policy
    bandit_policy.update_q_value(last_action, reward, last_context_key)
    logger.info(f"Applied reward {reward} to action '{last_action}' in context '{last_context_key}'.")

    # 3. Persist the updated tables back to the state
    q_table, n_table = bandit_policy.get_tables()
    state_manager.set("q_table", q_table)
    state_manager.set("n_table", n_table)

    # 4. Reset the last action/context placeholders
    state_manager.set("last_action", None)
    state_manager.set("last_context_key", None)
    state_manager.set("last_stage", None)

def compare_commands_with_docs(state_manager: StateManager, bug_reporter: BugReporter):
    """
    Compares commands discovered live with documented commands and logs discrepancies.
    """
    cached_schemas = state_manager.get("cached_schemas", {})
    commands_seen_ok = state_manager.get("commands_seen_ok", set())
    commands_seen_error = state_manager.get("commands_seen_error", set())

    documented_commands = set(cached_schemas.keys())
    live_commands = commands_seen_ok.union(commands_seen_error)

    # Commands in docs but not seen live
    undiscovered_commands = documented_commands - live_commands
    if undiscovered_commands:
        logger.warning(f"Undiscovered commands (in docs but not seen live): {undiscovered_commands}")
        # Optionally, report as a bug if this is unexpected
        # bug_reporter.triage_protocol_error(
        #     sent_command={"command": "system.cmd_list", "id": "n/a"},
        #     response={"status": "warning", "error": {"message": f"Undiscovered commands: {undiscovered_commands}"}},
        #     agent_state=state_manager.get_all(),
        #     error_message=f"Commands in documentation not yet seen live: {undiscovered_commands}",
        #     last_commands_history=[],
        #     last_responses_history=[],
        #     sent_schema=None,
        #     validated=True
        # )

    # Commands seen live but not in docs
    undocumented_commands = live_commands - documented_commands
    if undocumented_commands:
        logger.error(f"Undocumented commands (seen live but not in docs): {undocumented_commands}")
        bug_reporter.triage_protocol_error(
            sent_command={"command": "system.cmd_list", "id": "n/a"},
            response={"status": "error", "error": {"message": f"Undocumented commands: {undocumented_commands}"}},
            agent_state=state_manager.get_all(),
            error_message=f"Commands seen live but not found in documentation: {undocumented_commands}",
            last_commands_history=[],
            last_responses_history=[],
            sent_schema=None,
            validated=True
        )

def check_invariants(state_manager: StateManager, bug_reporter: BugReporter):
    """
    Verifies critical invariants of the game state and reports violations.
    """
    # Invariant 1: Player location must always be a positive integer
    player_location_sector = state_manager.get("player_location_sector")
    if not isinstance(player_location_sector, int) or player_location_sector <= 0:
        bug_reporter.triage_invariant_failure(
            invariant_name="player_location_sector_positive",
            current_value=player_location_sector,
            expected_condition="player_location_sector > 0",
            agent_state=state_manager.get_all(),
            error_message=f"Player location sector is invalid: {player_location_sector}"
        )

    # Invariant 2: Current credits should not be negative
    current_credits = state_manager.get("current_credits")
    if current_credits < 0:
        bug_reporter.triage_invariant_failure(
            invariant_name="current_credits_non_negative",
            current_value=current_credits,
            expected_condition="current_credits >= 0",
            agent_state=state_manager.get_all(),
            error_message=f"Current credits are negative: {current_credits}"
        )

    # Invariant 3: Session token must be present after authentication stage
    if state_manager.get("stage") != "bootstrap" and not state_manager.get("session_token"):
        bug_reporter.triage_invariant_failure(
            invariant_name="session_token_present_after_bootstrap",
            current_value=state_manager.get("session_token"),
            expected_condition="session_token is not None and not empty",
            agent_state=state_manager.get_all(),
            error_message="Session token is missing after bootstrap stage"
        )

    # Invariant 4: commands_never_tried should only contain commands not yet attempted
    commands_never_tried = state_manager.get("commands_never_tried", set())
    commands_seen_ok = state_manager.get("commands_seen_ok", set())
    commands_seen_error = state_manager.get("commands_seen_error", set())

    intersection_ok = commands_never_tried.intersection(commands_seen_ok)
    if intersection_ok:
        bug_reporter.triage_invariant_failure(
            invariant_name="commands_never_tried_no_overlap_ok",
            current_value=list(intersection_ok),
            expected_condition="commands_never_tried and commands_seen_ok should be disjoint",
            agent_state=state_manager.get_all(),
            error_message=f"Commands in commands_never_tried also in commands_seen_ok: {intersection_ok}"
        )
    
    intersection_error = commands_never_tried.intersection(commands_seen_error)
    if intersection_error:
        bug_reporter.triage_invariant_failure(
            invariant_name="commands_never_tried_no_overlap_error",
            current_value=list(intersection_error),
            expected_condition="commands_never_tried and commands_seen_error should be disjoint",
            agent_state=state_manager.get_all(),
            error_message=f"Commands in commands_never_tried also in commands_seen_error: {intersection_error}"
        )

    # Invariant 5: If waiting_for_turns is True, next_allowed_action_time must be in the future
    if state_manager.get("waiting_for_turns"):
        next_allowed_action_time = state_manager.get("next_allowed_action_time", 0)
        if time.time() >= next_allowed_action_time:
            bug_reporter.triage_invariant_failure(
                invariant_name="waiting_for_turns_time_consistency",
                current_value={"waiting_for_turns": True, "next_allowed_action_time": next_allowed_action_time, "current_time": time.time()},
                expected_condition="current_time < next_allowed_action_time when waiting_for_turns is True",
                agent_state=state_manager.get_all(),
                error_message="waiting_for_turns is True but next_allowed_action_time is not in the future"
            )

def write_coverage_report(state_manager: StateManager):
    report_dir = state_manager.config.get("coverage_report_dir", "coverage_reports")
    os.makedirs(report_dir, exist_ok=True)
    timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    report_filename = os.path.join(report_dir, f"coverage_report_{timestamp}.json")

    coverage_data = {
        "timestamp": timestamp,
        "total_steps": state_manager.get("total_steps", 0),
        "commands_seen_ok": list(state_manager.get("commands_seen_ok", set())),
        "commands_seen_error": list(state_manager.get("commands_seen_error", set())),
        "commands_never_tried": list(state_manager.get("commands_never_tried", set())),
        "error_codes_seen": list(state_manager.get("error_codes_seen", set())),
        "ports_visited_by_class": {k: list(v) for k, v in state_manager.get("ports_visited_by_class", {}).items()},
        "special_ports_seen": list(state_manager.get("special_ports_seen", set())),
        "zones_covered": list(state_manager.get("zones_covered", set())),
        "q_table_size": len(state_manager.get("q_table", {})),
        "n_table_size": len(state_manager.get("n_table", {})),
        "qa_metrics": {
            "commands": {
                cmd_name: {
                    "total_calls": metrics["total_calls"],
                    "successful_calls": metrics["successful_calls"],
                    "failed_calls": metrics["failed_calls"],
                    "last_called_timestamp": metrics["last_called_timestamp"],
                    "error_codes_seen": list(metrics["error_codes_seen"]) # Convert set to list
                }
                for cmd_name, metrics in state_manager.get("qa_metrics", {}).get("commands", {}).items()
            },
            "scenarios": state_manager.get("qa_metrics", {}).get("scenarios", {})
        }
    }

    try:
        with open(report_filename, "w") as f:
            json.dump(coverage_data, f, indent=4)
        logger.info(f"Coverage report written to {report_filename}")
    except IOError as e:
        logger.error(f"Failed to write coverage report to {report_filename}: {e}")

def get_oracle_llm_response(game_state_json: dict, model: str, bug_report_context: str):
    """
    Consults the LLM for bug analysis and potential recovery suggestions.
    This is a separate "oracle" LLM call, not part of the main planning loop.
    """
    serializable_state = game_state_json.copy()
    if 'visited_sectors' in serializable_state:
        serializable_state['visited_sectors'] = list(serializable_state['visited_sectors'])
    if 'visited_ports' in serializable_state:
        serializable_state['visited_ports'] = list(serializable_state['visited_ports'])
    if 'sectors_with_info' in serializable_state:
        serializable_state['sectors_with_info'] = list(serializable_state['sectors_with_info'])

    formatted_game_state = format_game_state_for_llm(game_state_json)

    system_prompt = f"""You are an 'AI_QA_Oracle'. Your task is to analyze a bug report and the current game state, then provide a concise analysis of the root cause and, if possible, suggest a single, valid JSON command to recover or further diagnose the issue.

Bug Report Context:
{bug_report_context}

Current Game State Summary:
{formatted_game_state}

Your response MUST be a JSON object with two fields:
1. "analysis": A string explaining the likely root cause of the bug.
2. "suggested_command": (Optional) A JSON object representing a single game command (e.g., {{"command": "ship.info", "data": {{}}}}). If you cannot suggest a command, omit this field.

DO NOT include any prose or extra text outside the JSON object.
"""
    messages = [
        {"role": "system", "content": system_prompt},
        {"role": "user", "content": "Analyze the bug and state, then provide your analysis and a suggested command (if any)."}
    ]
    try:
        response = ollama.chat(
            model=model,
            messages=messages,
            format='json',
            options={'temperature': 0.1, 'seed': 42}
        )
        oracle_response_str = response['message']['content']
        oracle_response_dict = json.loads(oracle_response_str)
        logger.info(f"Ollama Oracle responded with: {oracle_response_str}")
        return oracle_response_dict
    except json.JSONDecodeError as e:
        logger.error(f"Ollama Oracle response JSON DECODE ERROR: {e} - Raw data: {oracle_response_str}")
    except Exception as e:
        logger.error(f"Ollama Oracle API Error: {e}")
    return None

def format_game_state_for_llm(game_state_json: dict) -> str:
    """
    Formats the game state into a human-readable string for the LLM.
    """
    formatted_state = []

    # Player Location
    current_sector_id = game_state_json.get("player_location_sector", 1)
    last_server_response = game_state_json.get("last_server_response", {})
    
    # Ensure 'data' field is a dictionary before accessing its 'name' key
    last_server_response_data = last_server_response.get("data")
    if not isinstance(last_server_response_data, dict):
        last_server_response_data = {}

    sector_name = last_server_response_data.get("name", f"Sector {current_sector_id}")
    formatted_state.append(f"Location: {sector_name} (Sector {current_sector_id})")

    # Ship Info
    ship_info = game_state_json.get("ship_info")
    if ship_info:
        cargo = ship_info.get("cargo", {})
        formatted_cargo = ", ".join([f"{k}: {v}" for k, v in cargo.items()])
        formatted_state.append(f"Cargo: {{{formatted_cargo}}}")
    else:
        formatted_state.append("Cargo: Unknown (ship.info not available)")

    # Port Info
    port_info = game_state_json.get("port_info")
    if port_info:
        port_id = port_info.get("id")
        port_name = port_info.get("name")
        stock = port_info.get("stock", {})
        formatted_stock = ", ".join([f"{k}: {v}" for k, v in stock.items()])
        formatted_state.append(f"Port {port_id} ({port_name}) Stock: {{{formatted_stock}}}")
    else:
        formatted_state.append("Port Info: Not available or outdated")

    # Broken Commands
    broken_commands = game_state_json.get("broken_commands", [])
    if broken_commands:
        formatted_broken = ", ".join([f"'{item['command']}' (Error: {item['error']})" for item in broken_commands])
        formatted_state.append(f"Broken Commands: [{formatted_broken}]")
    else:
        formatted_state.append("Broken Commands: None")

    # Cached Price Data
    price_cache = game_state_json.get("price_cache", {})
    current_sector = game_state_json.get("player_location_sector", 1)
    
    if current_sector in price_cache and price_cache[current_sector]:
        formatted_prices = []
        for port_id, port_prices in price_cache.get(current_sector, {}).items():
            for commodity, prices in port_prices.items():
                formatted_prices.append(
                    f"port {port_id} / {commodity}: Buy={prices.get('buy', 'N/A')}, Sell={prices.get('sell', 'N/A')}"
                )
        formatted_state.append("Current Price Cache: " + ", ".join(formatted_prices))
    else:
        formatted_state.append("Current Price Cache: Empty/Needs update.")
    
    return "\n".join(formatted_state)

def get_ollama_response(game_state_json: dict, model: str, current_stage: str):
    logger.debug(f"Type of game_state_json in get_ollama_response: {type(game_state_json)}")
    
    # Create a serializable copy for logging
    serializable_state = game_state_json.copy()
    if 'visited_sectors' in serializable_state:
        serializable_state['visited_sectors'] = list(serializable_state['visited_sectors'])
    if 'visited_ports' in serializable_state:
        serializable_state['visited_ports'] = list(serializable_state['visited_ports'])
    if 'sectors_with_info' in serializable_state:
        serializable_state['sectors_with_info'] = list(serializable_state['sectors_with_info'])
    if 'commands_seen_ok' in serializable_state:
        serializable_state['commands_seen_ok'] = list(serializable_state['commands_seen_ok'])
    if 'commands_seen_error' in serializable_state:
        serializable_state['commands_seen_error'] = list(serializable_state['commands_seen_error'])
    if 'error_codes_seen' in serializable_state:
        serializable_state['error_codes_seen'] = list(serializable_state['error_codes_seen'])
    if 'special_ports_seen' in serializable_state:
        serializable_state['special_ports_seen'] = list(serializable_state['special_ports_seen'])
    if 'zones_covered' in serializable_state:
        serializable_state['zones_covered'] = list(serializable_state['zones_covered'])
    if 'commands_never_tried' in serializable_state:
        serializable_state['commands_never_tried'] = list(serializable_state['commands_never_tried'])
    
    # Handle nested sets in qa_metrics
    if 'qa_metrics' in serializable_state and isinstance(serializable_state['qa_metrics'], dict):
        if 'commands' in serializable_state['qa_metrics'] and isinstance(serializable_state['qa_metrics']['commands'], dict):
            for cmd_name, cmd_metrics in serializable_state['qa_metrics']['commands'].items():
                if 'error_codes_seen' in cmd_metrics and isinstance(cmd_metrics['error_codes_seen'], set):
                    cmd_metrics['error_codes_seen'] = list(cmd_metrics['error_codes_seen'])
        
    logger.debug(f"Content of game_state_json in get_ollama_response: {json.dumps(serializable_state, indent=2)}")
    
    formatted_game_state = format_game_state_for_llm(game_state_json)
    
    # --- Dynamic Command List Generation ---
    command_list_str = """
    - `system.cmd_list` (to get an updated list of commands)
    - `ship.info`
    - `sector.info`
    """
    
    last_response = game_state_json.get("last_server_response", {})
    if last_response and last_response.get("type") == "system.cmd_list":
        commands = last_response.get("data", {}).get("commands", [])
        if commands:
            command_list_str = "\\n".join([f"- `{c.get('cmd')}`: {c.get('summary', '')}" for c in commands])

    last_commands_history = game_state_json.get("last_commands_history", [])
    last_commands_str = ""
    if last_commands_history:
        last_commands_str = "\\n\\nRecently executed commands:\\n" + "\\n".join([f"- {cmd}" for cmd in last_commands_history])

    system_prompt = ""
    qa_mode_enabled = game_state_json.get("config", {}).get("qa_mode", False)

    if current_stage == "bootstrap":
        system_prompt = f"""You are 'AI_QA_Bot'. Your SOLE task is to output a single, valid JSON object for the next game command. DO NOT include any prose or extra text. Your objective is to discover and categorize all available commands and their schemas. Use the provided game state to formulate the next logical action.

Here is the list of available commands:
{command_list_str}
{last_commands_str}

You must choose a command from this list. Your response MUST be a JSON object with a "command" field containing the command name. If a command requires parameters, include them in a "data" field. For example: {{'command': 'system.describe_schema', 'data': {{'name': 'players'}}}}

CRITICAL: If the last server response contains an "Unknown command" error, you MUST choose a different command from the list. Do not repeat a command that has just resulted in an error.

Your primary goal in this stage is to systematically try every command in the `commands_to_try` list and fetch its schema. Once a command's schema is fetched, it will be removed from `schemas_to_fetch`. If a command consistently fails or requires complex parameters you cannot guess, categorize it as broken.
"""
    elif current_stage == "survey":
        working_commands_str = "\\n".join([f"- `{c}`" for c in game_state_json.get("working_commands", [])])
        system_prompt = f"""You are 'AI_QA_Bot'. Your SOLE task is to output a single, valid JSON object for the next game command. DO NOT include any prose or extra text. Your objective is to gather comprehensive information about the current sector, your ship, and local trade opportunities.

Here is the list of commands that have been confirmed to be WORKING:
{working_commands_str}
{last_commands_str}

Your current credits are: {game_state_json.get("current_credits", 0.0):.2f}

To achieve your objective, you should:
1.  Use `ship.info` to get details about your ship (cargo, holds, etc.).
2.  Use `sector.info` to get details about the current sector (ports, adjacent sectors).
3.  If there are ports in the current sector, use `trade.quote` to get buy/sell prices for commodities.

You must choose a command from the list of WORKING commands. Your response MUST be a JSON object with a "command" field containing the command name. If a command requires parameters, include them in a "data" field.
"""
    elif current_stage == "explore":
        working_commands_str = "\\n".join([f"- `{c}`" for c in game_state_json.get("working_commands", [])])
        system_prompt = f"""You are 'AI_QA_Bot'. Your SOLE task is to output a single, valid JSON object for the next game command. DO NOT include any prose or extra text. Your objective is to explore new sectors to find new trade opportunities or valuable resources.

Here is the list of commands that have been confirmed to be WORKING:
{working_commands_str}
{last_commands_str}

Your current credits are: {game_state_json.get("current_credits", 0.0):.2f}

To achieve your objective, you should:
1.  Use `move.warp` to move to an adjacent sector. Prioritize sectors that are unknown or have a higher potential value (e.g., more ports, fresh data).
2.  After warping, use `sector.info` to gather information about the new sector.
3.  If you find ports, use `trade.quote` to update your price cache.

You must choose a command from the list of WORKING commands. Your response MUST be a JSON object with a "command" field containing the command name. If a command requires parameters, include them in a "data" field.
"""
    elif current_stage == "economic_optimization" or current_stage == "exploit": # Use for exploit stage as well
        working_commands_str = "\\n".join([f"- `{c}`" for c in game_state_json.get("working_commands", [])])
        system_prompt = f"""You are 'AI_QA_Bot'. Your SOLE task is to output a single, valid JSON object for the next game command. DO NOT include any prose or extra text. Your objective is to maximize your current credits through strategic trading.

Here is the list of commands that have been confirmed to be WORKING:
{working_commands_str}
{last_commands_str}

Your current credits are: {game_state_json.get("current_credits", 0.0):.2f}
Your ship cargo: {(game_state_json.get("ship_info") or {}).get("cargo", {})}
Your ship holds free: {(game_state_json.get("ship_info") or {}).get("holds", 0) - sum((game_state_json.get("ship_info") or {}).get("cargo", {}).values())}

**Current Price Cache (Sector {game_state_json.get("player_location_sector")}):**
{json.dumps(game_state_json.get("price_cache", {}).get(game_state_json.get("player_location_sector"), {}), indent=2)}

To achieve your objective of maximizing your credits, you **must calculate a price differential** before issuing a `trade.buy` or `trade.sell` command. **You cannot buy and sell at the same port.**

**THOUGHT REQUIREMENT:** Your `thought` field **MUST** include a comparison: `Profit Margin Check: Buy Price at Port X vs. Expected Sell Price at Port Y = [Value]`.

To achieve your objective of maximizing your credits, follow these steps:
1.  **Identify Profitable Trades:** Analyze the `price_cache` across all known sectors and ports to find the most profitable trade route (buy low in one sector/port, sell high in another).
2.  **Execute Trades:** If a profitable trade is available in the current sector, use `trade.buy` or `trade.sell` with appropriate parameters (port_id, commodity, quantity).
3.  **Move to New Sectors:** If no profitable trades are available in the current sector, or if a trade route requires moving, use `move.warp` to move to the next sector in the most profitable trade route.
4.  **Gather Information:** Use `sector.info` and `trade.quote` to keep your price cache updated, especially after warping to a new sector.

You must choose a command from the list of WORKING economic commands. Your response MUST be a JSON object with a "command" field containing the command name. If a command requires parameters, include them in a "data" field.

Prioritize actions that directly increase your credits or gather information crucial for future profit. Avoid repeating commands that don't yield new information or contribute to your economic goal.
"""
    elif current_stage == "RECOVERY_STUCK": # New prompt for getting unstuck
        working_commands_str = "\\n".join([f"- `{c}`" for c in game_state_json.get("working_commands", [])])
        system_prompt = f"""You are 'AI_QA_Bot' in a recovery situation. Your SOLE task is to output a single, valid JSON object for the next game command to get the bot unstuck. DO NOT include any prose or extra text. The bot's internal planner has failed to find a valid action.

Your goal is to analyze the current state and suggest a single command to break the deadlock.

Here is the list of commands that have been confirmed to be WORKING:
{working_commands_str}
{last_commands_str}

Current State Summary:
{formatted_game_state}

Common reasons for getting stuck include:
- All available actions are on cooldown.
- A critical command (like 'move.warp') has been temporarily disabled after too many failures.
- The bot is in the wrong stage for its location (e.g., 'explore' stage while at a port that needs surveying).

Based on the state, suggest a command to un-stick the bot. Good options might be:
- A command that has been on cooldown for a while.
- A command that forces a state refresh, like `ship.info` or `sector.info`.
- A command that might transition the bot to a more appropriate stage.

Your response MUST be a JSON object with a "command" field and an optional "data" field.
"""
    elif current_stage == "UNHANDLED_ERROR_RECOVERY": # New stage for unhandled errors
        working_commands_str = "\\n".join([f"- `{c}`" for c in game_state_json.get("working_commands", [])])
        last_server_response = game_state_json.get("last_server_response", {})
        error_message = last_server_response.get("error", {}).get("message", "Unknown error")
        error_code = last_server_response.get("error", {}).get("code", "Unknown code")

        system_prompt = f"""You are 'AI_QA_Bot' and have encountered an unhandled server error. Your SOLE task is to output a single, valid JSON object for the next game command to attempt to recover from this error. DO NOT include any prose or extra text.

The last server response was an error:
Error Code: {error_code}
Error Message: {error_message}

Here is the list of commands that have been confirmed to be WORKING:
{working_commands_str}
{last_commands_str}

Current State Summary:
{formatted_game_state}

Your goal is to suggest a command that might help resolve or bypass this unhandled error. Consider:
- Requesting `ship.info` or `sector.info` to refresh local state.
- Attempting a `bank.balance` to check financial status.
- If the error seems related to a specific action, try a different action.
- If unsure, a `system.hello` might re-initialize the session.

Your response MUST be a JSON object with a "command" field and an optional "data" field.
"""
    else:
        # Fallback for unknown stages, or initial generic prompt
        system_prompt = f"""You are 'AI_QA_Bot'. Your SOLE task is to output a single, valid JSON object for the next game command. DO NOT include any prose or extra text. Your objective is profit and anomaly logging. Use the provided game state to formulate the next logical action.

Here is the list of available commands:
{command_list_str}
{last_commands_str}

You must choose a command from this list. Your response MUST be a JSON object with a "command" field containing the command name. If a command requires parameters, include them in a "data" field. For example: {{'command': 'system.describe_schema', 'data': {{'name': 'players'}}}}

CRITICAL: If the last server response contains an "Unknown command" error, you MUST choose a different command from the list. Do not repeat a command that has just resulted in an error.
"""

    messages = [
        {"role": "system", "content": system_prompt},
        {"role": "user", "content": f"Analyze the last server response and current state below, then output the next command JSON. State:\\n{formatted_game_state}"}
    ]
    try:
        response = ollama.chat(
            model=model,
            messages=messages,
            format='json',
            options={'temperature': 0.1, 'seed': 42}
        )
        command_str = response['message']['content']
        llm_command_dict = json.loads(command_str)
        logger.info(f"Ollama responded with: {command_str}")
        return llm_command_dict
    except json.JSONDecodeError as e:
        logger.error(f"Ollama response JSON DECODE ERROR: {e} - Raw data: {command_str}")
    except Exception as e:
        logger.error(f"Ollama API Error: {e}")
    return None
def main():
    """Main execution function."""
    global config, logger, shutdown_flag
    
    parser = argparse.ArgumentParser(description="AI Player for TWClone")
    parser.add_argument('--config', type=str, default='config.json',
                        help='Path to the configuration file.')
    args = parser.parse_args()

    # --- Initialization ---
    config = Config(args.config)
    
    # Set random seed for reproducibility
    random.seed(config.get("random_seed", time.time()))

    logging.basicConfig(
        level=logging.DEBUG, # Change INFO to DEBUG
        format='%(asctime)s - %(levelname)s - %(message)s',
        handlers=[
            logging.FileHandler(config.get('log_file', 'ai_player.log')),
            logging.StreamHandler()
        ]
    )
    logger = logging.getLogger(__name__)
    
    signal.signal(signal.SIGINT, signal_handler)
    
    state_manager = StateManager(config.get('state_file', 'state.json'), config)
    # Clear broken_commands at startup to allow re-attempting them after fixes
    state_manager.set("broken_commands", [])
    # Clear command_cooldowns at startup to allow re-attempting them after fixes
    state_manager.set("command_cooldowns", {})
    
    # Load JSON Schemas from PROTOCOL docs once at startup
    try:
        load_schemas_from_docs(state_manager)
    except Exception as e:
        logger.error("Schema loading failed; continuing without validation: %s", e)

    # Initialize commands_never_tried after schemas are loaded
    # This will be further refined once system.cmd_list is processed
    if not state_manager.get("commands_never_tried"):
        all_known_commands = set(state_manager.get("cached_schemas", {}).keys())
        state_manager.set("commands_never_tried", all_known_commands)

    bug_reporter = BugReporter(
        config.get('bug_report_dir', 'bugs'),
        state_manager=state_manager,
        config=config,
        get_oracle_llm_response_func=get_oracle_llm_response,
    )
    game_conn = GameConnection(config.get('game_host'), config.get('game_port'), state_manager, bug_reporter)
    bandit_policy = BanditPolicy(epsilon=config.get("bandit_epsilon", 0.1), 
                                 q_table=state_manager.get("q_table"), 
                                 n_table=state_manager.get("n_table"))
    planner = FiniteStatePlanner(state_manager, game_conn, config, bug_reporter, bandit_policy)

    # --- Main Loop ---
    while not shutdown_flag:
        try:
            llm_command = None # ADDED: Reset LLM command at the start of each loop iteration
            if not game_conn.sock:
                logger.warning("No game connection. Attempting to reconnect...")
                game_conn.connect()
                if not game_conn.sock:
                    time.sleep(1) # Wait a bit before retrying connection
                    continue

            # --- Process all incoming responses ---
            process_responses(state_manager, game_conn, bug_reporter)
            calculate_and_apply_reward(state_manager, bandit_policy)

            # Check invariants after state updates
            check_invariants(state_manager, bug_reporter)

            # Increment total steps
            state_manager.set("total_steps", state_manager.get("total_steps", 0) + 1)

            # Periodically write coverage report
            if state_manager.get("total_steps") % state_manager.config.get("coverage_report_interval", 100) == 0:
                write_coverage_report(state_manager)
            
            # Periodically compare commands with docs
            if state_manager.get("total_steps") % state_manager.config.get("command_comparison_interval", 500) == 0:
                compare_commands_with_docs(state_manager, bug_reporter)

            # --- Check if waiting for turns cooldown ---
            if state_manager.get("waiting_for_turns"):
                next_allowed_time = state_manager.get("next_allowed_action_time", 0)
                if time.time() < next_allowed_time:
                    sleep_duration = max(1, int(next_allowed_time - time.time()))
                    logger.info(f"Waiting for turns cooldown. Sleeping for {sleep_duration} seconds...")
                    time.sleep(sleep_duration)
                    continue # Skip command generation until cooldown is over
                else:
                    logger.info("Turns cooldown over. Resuming operations.")
                    state_manager.set("waiting_for_turns", False) # Cooldown is over, reset flag

            # --- Decision Making and Command Sending via Planner ---
            # Adjust epsilon based on stage
            current_stage = state_manager.get("stage")
            if current_stage == "bootstrap":
                bandit_policy.epsilon = state_manager.config.get("epsilon_bootstrap", 0.5)
            elif current_stage == "survey":
                bandit_policy.epsilon = state_manager.config.get("epsilon_survey", 0.3)
            elif current_stage == "explore":
                bandit_policy.epsilon = state_manager.config.get("epsilon_explore", 0.2)
            elif current_stage == "exploit":
                bandit_policy.epsilon = state_manager.config.get("epsilon_exploit", 0.1)
            else:
                bandit_policy.epsilon = state_manager.config.get("bandit_epsilon", 0.1) # Default

            # Prioritize LLM recovery command if available
            llm_recovery_command = state_manager.get("llm_recovery_command")
            if llm_recovery_command:
                logger.info(f"Prioritizing LLM recovery command: {llm_recovery_command}")
                next_command_dict = llm_recovery_command
                state_manager.set("llm_recovery_command", None) # Clear after use
            else:
                # If there are pending commands, don't send new ones yet
                logger.debug(f"Pending commands before check: {state_manager.get('pending_commands')}")
                if state_manager.get("pending_commands"):
                    time.sleep(0.5) # Increased sleep
                    continue

                # Get LLM suggestion (if applicable for the current stage)
                llm_command = None
                # Only consult LLM for exploit stage for now, as per the planner's design
                if planner.current_stage == "exploit":
                    game_state = state_manager.get_all()
                    llm_command = get_ollama_response(game_state, config.get("ollama_model"), state_manager.get("stage"))
                    if llm_command:
                        logger.info(f"LLM suggested command for exploit stage: {llm_command.get('command')} with data: {llm_command.get('data')}")
                    else:
                        logger.warning("LLM failed to generate a valid command for exploit stage.")

                next_command_dict = planner.get_next_command(llm_command)

                if not next_command_dict:
                    logger.warning("Planner returned no command. Attempting LLM recovery...")
                    game_state = state_manager.get_all()
                    recovery_llm_command = get_ollama_response(
                        game_state, 
                        config.get("ollama_model"), 
                        "RECOVERY_STUCK"
                    )
                    if recovery_llm_command:
                        logger.info(f"LLM recovery suggested: {recovery_llm_command}")
                        next_command_dict = recovery_llm_command
                    else:
                        logger.error("LLM recovery failed. The bot is truly stuck.")

            if next_command_dict:
                next_command_id = str(uuid.uuid4())
                idempotency_key = str(uuid.uuid4())
                game_command = {
                    "id": next_command_id,
                    "ts": datetime.datetime.now().isoformat() + "Z",
                    "command": next_command_dict["command"],
                    "auth": {"session": state_manager.get("session_token")},
                    "meta": {"client_version": config.get("client_version")},
                }
                # Conditionally add idempotency_key to data or meta based on command type
                if next_command_dict["command"] in ["trade.buy", "trade.sell"]:
                    game_command["data"] = {**next_command_dict.get("data", {}), "idempotency_key": idempotency_key}
                else:
                    game_command["data"] = next_command_dict.get("data", {})
                    game_command["meta"]["idempotency_key"] = idempotency_key

                game_conn.send_command(game_command)
                state_manager.set("last_command_sent_id", next_command_id)

                history = state_manager.get("last_commands_history")
                history.append(game_command) # Store the full command dictionary
                state_manager.set("last_commands_history", history[-5:]) # Keep only the last 5 commands
            else:
                logger.warning("Planner and LLM recovery failed to produce a command. Waiting...")
            
            time.sleep(0.5) # Small delay to prevent busy loop

        except Exception as e:
            logger.critical(f"An unhandled exception occurred in the main loop: {e}", exc_info=True)
            time.sleep(1) # Wait before retrying
    
    # --- Shutdown ---
    logger.info("Shutdown sequence initiated.")
    state_manager.save()
    game_conn.close()
    logger.info("AI Player script finished.")

if __name__ == "__main__":
    main()
