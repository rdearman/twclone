import socket
import json
import time
import uuid
import uuid
import datetime
import ollama
import logging
import os
import signal
import sys # Add this line
import argparse
import random # Add this line
from planner import FiniteStatePlanner
from bug_reporter import BugReporter
from bandit_policy import BanditPolicy, make_context_key
from parse_protocol import parse_protocol_markdown

import argparse
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
        return {
            "stage": "bootstrap",
            "session_token": None,
            "player_location_sector": 1,
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
            "port_info": None,
            "last_bank_info_request_time": 0,
            "price_cache": {},
            "next_allowed": {},
            "server_capabilities": {},
            "last_processed_command_name": None,
            "sector_data": {},
            "new_sector_discovered": False,
            "trade_successful": False,
            "new_port_discovered": False,
            "rate_limit_info": {},
            "command_retry_info": {},
            "ports_by_sector": {},
        }

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
                elif value is not None: # Only overwrite if source value is not None
                    target_dict[key] = value
            elif value is not None: # Add new keys if not None
                target_dict[key] = value

    def load(self):
        self.state = self._get_default_state() # Start with a fresh default state
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
                    self._merge_state(self.state, loaded_data) # Merge loaded data into current state
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
        # Create a serializable copy of the state
        state_to_save = self.state.copy()
        if 'visited_sectors' in state_to_save:
            state_to_save['visited_sectors'] = list(state_to_save['visited_sectors'])
        if 'visited_ports' in state_to_save:
            state_to_save['visited_ports'] = list(state_to_save['visited_ports'])
        if 'sectors_with_info' in state_to_save:
            state_to_save['sectors_with_info'] = list(state_to_save['sectors_with_info'])

        try:
            with open(self.path, 'w') as f:
                json.dump(state_to_save, f, indent=4)
            logger.info(f"Successfully saved state to {self.path}")
        except (IOError, TypeError) as e:
            logger.error(f"Could not write state to {self.path}: {e}")

    def get(self, key, default=None):
        return self.state.get(key, default)

    def set(self, key, value):
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
                    # --- Domain-specific state updates based on response type ---
                    response_type = response.get("type")
                    response_data = response.get("data", {}) or {}
                    now = time.time()

                    # --- Invariant Checks ---
                    if state_manager.config.get("qa_mode"):
                        if response_data.get("credits") is not None and response_data.get("credits") < 0:
                            bug_reporter.triage_invariant_failure(
                                "Negative Credits",
                                f"Credits dropped to {response_data.get('credits')} in response to {command_name}",
                                state_manager.get_all()
                            )
                        if response_type in ("ship.info", "ship.status"):
                            ship = response_data.get("ship") or response_data
                            if ship:
                                holds = ship.get("holds", 0)
                                cargo_total = sum(ship.get("cargo", {}).values())
                                if cargo_total > holds:
                                    bug_reporter.triage_invariant_failure(
                                        "Cargo Exceeds Holds",
                                        f"Total cargo ({cargo_total}) exceeds holds capacity ({holds})",
                                        state_manager.get_all()
                                    )

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
                            state_manager.set("ship_info", ship)
                            loc = ship.get("location", {})
                            sector_id = loc.get("sector_id")
                            if sector_id is not None:
                                state_manager.set("player_location_sector", sector_id)
                                visited = state_manager.get("visited_sectors", set())
                                visited.add(sector_id)
                                state_manager.set("visited_sectors", visited)
                                logger.info(f"Ship in sector {sector_id}")

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
                        elif "bank_account" in data and "balance" in data["bank_account"]:
                            credits = data["bank_account"]["balance"]

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

                            logger.info(f"Updated sector.info cache for sector {sector_id}")

                    # 4) Port info
                    if response_type == "trade.port_info" and response.get("status") == "ok":
                        state_manager.set("port_info", response_data)
                        port = response_data.get("port", {})
                        port_id = port.get("id") or response_data.get("port_id") # More robust
                        if port_id is not None:
                            visited_ports = state_manager.get("visited_ports", set())
                            if port_id not in visited_ports:
                                visited_ports.add(port_id)
                                state_manager.set("visited_ports", visited_ports)
                                state_manager.set("new_port_discovered", True)
                                logger.info(f"Updated port info for new port_id={port_id}")
                            else:
                                logger.info(f"Refreshed port info for port_id={port_id}")
                            
                            # Update ports_by_sector map
                            ports_by_sector = state_manager.get("ports_by_sector", {})
                            current_sector = state_manager.get("player_location_sector")
                            if current_sector is not None:
                                ports_by_sector.setdefault(str(current_sector), {})[str(port_id)] = response_data
                                state_manager.set("ports_by_sector", ports_by_sector)
                                logger.info(f"Cached port {port_id} in sector {current_sector} in ports_by_sector.")
                        else:
                            logger.warning("trade.port_info ok but no port_id in data")

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
                            state_manager.set("port_info", None) # Clear port_info on sector change
                            logger.info(f"Moved to sector {new_sector}")
                else: # Error or refused
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

                    broken_cmds = state_manager.get("broken_commands")
                    error_msg = response.get('error', {}).get('message', 'Unknown error')

                    # NEW: track repeated failures for QA & avoidance
                    fail_counts = state_manager.get("command_retry_info", {})
                    info = fail_counts.get(command_name, {"failures": 0, "next_retry_time": 0})
                    info["failures"] += 1
                    fail_counts[command_name] = info
                    state_manager.set("command_retry_info", fail_counts)

                    logger.warning(f"Command '{command_name}' failed with: {error_msg}")
            else:
                logger.warning(f"Received response for unknown or already processed command ID: {reply_to_id}")
        else:
            logger.debug(f"Received unsolicited server message: {json.dumps(response)}")

def load_schemas_from_docs(state_manager: StateManager):
    logger.info("Loading schemas from protocol markdown file...")
    try:
        # Use the parser you wrote
        extracted_commands = parse_protocol_markdown('/home/rick/twclone/docs/PROTOCOL.v2.md')
        
        cached_schemas = {}
        for cmd in extracted_commands:
            try:
                # Convert the string schema back into a dict for the validator
                schema_dict = json.loads(cmd['data_schema'])
                cached_schemas[cmd['name']] = {"request_schema": schema_dict}
            except json.JSONDecodeError:
                # Handle cases like "{}" which are valid
                if cmd['data_schema'] == "{}":
                    cached_schemas[cmd['name']] = {"request_schema": {}}
                else:
                    logger.warning(f"Could not parse schema for {cmd['name']}: {cmd['data_schema']}")

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

    # 1. Calculate reward based on state deltas
    previous_credits = state_manager.get("previous_credits", state_manager.get("current_credits", 0.0))
    current_credits = state_manager.get("current_credits", 0.0)
    reward = (current_credits - previous_credits) / 1000.0 # Scale to a small number
    state_manager.set("previous_credits", current_credits)

    # Add intrinsic rewards for exploration, etc.
    if state_manager.get("new_sector_discovered"):
        reward += 10.0
        state_manager.set("new_sector_discovered", False)
    if state_manager.get("trade_successful"):
        reward += 5.0
        state_manager.set("trade_successful", False)
    if state_manager.get("new_port_discovered"):
        reward += 15.0
        state_manager.set("new_port_discovered", False)

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
    
    # Load JSON Schemas from PROTOCOL docs once at startup
    try:
        load_schemas_from_docs(state_manager)
    except Exception as e:
        logger.error("Schema loading failed; continuing without validation: %s", e)

    bug_reporter = BugReporter(
        config.get('bug_report_dir', 'bugs'),
        state_manager=state_manager,
        config=config,
    )
    game_conn = GameConnection(config.get('game_host'), config.get('game_port'), state_manager, bug_reporter)
    bandit_policy = BanditPolicy(epsilon=config.get("bandit_epsilon", 0.1), 
                                 q_table=state_manager.get("q_table"), 
                                 n_table=state_manager.get("n_table"))
    planner = FiniteStatePlanner(state_manager, game_conn, config, bug_reporter, bandit_policy)

    # --- Main Loop ---
    while not shutdown_flag:
        try:
            if not game_conn.sock:
                logger.warning("No game connection. Attempting to reconnect...")
                game_conn.connect()
                if not game_conn.sock:
                    time.sleep(1) # Wait a bit before retrying connection
                    continue

            # --- Process all incoming responses ---
            process_responses(state_manager, game_conn, bug_reporter)
            calculate_and_apply_reward(state_manager, bandit_policy)

            # --- Decision Making and Command Sending via Planner ---
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
                    logger.info(f"LLM suggested command: {llm_command.get('command')} with data: {llm_command.get('data')}")
                else:
                    logger.warning("LLM failed to generate a valid command.")

            next_command_dict = planner.get_next_command(llm_command)

            if next_command_dict:
                next_command_id = str(uuid.uuid4())
                idempotency_key = str(uuid.uuid4())
                game_command = {
                    "id": next_command_id,
                    "ts": datetime.datetime.now().isoformat() + "Z",
                    "command": next_command_dict["command"],
                    "auth": {"session": state_manager.get("session_token")},
                    "data": {**next_command_dict.get("data", {}), "idempotency_key": idempotency_key}, # Add idempotency_key to data
                    "meta": {"client_version": config.get("client_version")}, # Remove idempotency_key from meta as it's in data
                    "idempotency_key": idempotency_key # Keep at top level for consistency, though server doesn't use it here
                }

                game_conn.send_command(game_command)
                state_manager.set("last_command_sent_id", next_command_id)

                history = state_manager.get("last_commands_history")
                history.append(next_command_dict["command"])
                state_manager.set("last_commands_history", history[-5:]) # Keep only the last 5 commands
            else:
                logger.warning("Planner returned no command. Waiting...")
            
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
