import socket
import json
import time
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
from bandit_policy import BanditPolicy
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
        return {
            "player_username": self.config.get("player_username"), # Add player_username
            "client_version": self.config.get("client_version"), # Add client_version
            "last_server_response": {},
            "current_credits": 0.0,
            "previous_credits": 0.0, # New: Stores credits from previous step for reward calculation
            "last_command_sent_id": "",
            "player_location_sector": 1,
            "session_token": None,
            "last_commands_history": [], # Initialize here
            "last_responses_history": [], # New: Stores history of recent responses
            "stage": "bootstrap", # New: Initial stage should be bootstrap
            "working_commands": [], # New: List of commands that work
            "broken_commands": [], # New: List of commands that consistently fail
            "commands_to_try": [], # New: Commands yet to be tried in discovery
            "pending_commands": {}, # New: Commands sent, awaiting response {id: command_dict}
            "received_responses": [], # New: Responses received in current tick
            "ship_info": None, # New: Stores the latest ship.status data
            "port_info": None, # New: Stores the latest trade.port_info data
            "sector_info_fetched_for": {}, # New: Tracks when sector info was last fetched for a sector
            "port_info_failures_per_sector": {}, # New: Tracks persistent failures for port info per sector
            "last_ship_info_request_time": 0, # New: Timestamp of last ship.info request
            "last_sector_info_request_time": 0, # New: Timestamp of last sector.info request
            "last_bank_info_request_time": 0, # New: Timestamp of last bank.info request
            "cached_schemas": {}, # New: Stores fetched command schemas
            "price_cache": {}, # New: Stores cached price data from trade.quote
            "next_allowed": {}, # New: Stores next allowed timestamp for each command
            "server_capabilities": {}, # New: Stores server capabilities
            "normalized_commands": {}, # New: Stores normalized command names
            "q_table": {}, # New: Stores Q-values for bandit policy (now contextual)
            "n_table": {}, # New: Stores N-values for bandit policy (now contextual)
            "last_processed_command_name": None, # New: Stores the name of the last command for which a response was processed
            "schemas_to_fetch": ["move.warp", "sector.info", "ship.info", "trade.quote"], # New: List of commands whose schemas need to be fetched
            "sector_data": {}, # New: Stores detailed sector information
            "new_sector_discovered": False, # New: Intrinsic reward flag
            "trade_successful": False, # New: Intrinsic reward flag
            "new_port_discovered": False, # New: Intrinsic reward flag
            "rate_limit_info": {}, # New: Stores the latest rate limit information from server responses
            "command_retry_info": {}, # New: Stores retry information for commands
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
                    self._merge_state(self.state, loaded_data) # Merge loaded data into current state
                logger.info(f"Successfully loaded state from {self.path}")
            except json.JSONDecodeError:
                logger.error(f"Could not decode state file {self.path}. Starting with fresh state.")
        else:
            logger.info("No state file found. Starting with fresh state.")
        
        # Post-load reconciliation: Force bootstrap if session token is missing or authentication failed
        session_token_present = self.state.get("session_token") is not None and self.state.get("session_token") != ""
        authenticated_status = self.state.get("last_server_response", {}).get("data", {}).get("authenticated", False)

        if not session_token_present or not authenticated_status:
            if self.state["stage"] != "bootstrap":
                logger.warning(f"Session not active. Forcing stage from '{self.state['stage']}' to 'bootstrap'.")
            self.state["stage"] = "bootstrap"

    def save(self):
        try:
            with open(self.path, 'w') as f:
                json.dump(self.state, f, indent=4)
            logger.info(f"Successfully saved state to {self.path}")
        except IOError as e:
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
        if "meta" not in command_json:
            command_json["meta"] = {}
        if "idempotency_key" not in command_json["meta"]:
            command_json["meta"]["idempotency_key"] = str(uuid.uuid4())

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
        
        # Add response to history
        history = state_manager.get("last_responses_history")
        history.append(response)
        state_manager.set("last_responses_history", history[-5:]) # Keep only the last 5 responses
        
        # Extract and store rate limit information
        rate_limit_data = response.get("meta", {}).get("rate_limit")
        if rate_limit_data:
            # Assuming 'reset' is a window length in seconds
            reset_window_length = rate_limit_data.get("reset", 0)
            if reset_window_length > 0:
                rate_limit_data["reset_at"] = time.time() + reset_window_length
            else:
                rate_limit_data["reset_at"] = 0 # No reset time if window length is 0
            state_manager.set("rate_limit_info", rate_limit_data)

        reply_to_id = response.get("reply_to")
        if reply_to_id:
            pending_cmds = state_manager.get("pending_commands", {})
            sent_command = pending_cmds.pop(reply_to_id, None) # Remove from pending
            state_manager.set("pending_commands", pending_cmds) # Update state manager

            if sent_command:
                command_name = sent_command.get("command")
                state_manager.set("last_processed_command_name", command_name) # Store the command name that just received a response

                # --- Envelope Invariant Checks ---
                # 1. Check if reply_to matches a pending command (already done by pop)
                # 2. Check status validity
                if response.get("status") not in ["ok", "error", "refused", "granted", "partial"]:
                    bug_reporter.triage_protocol_error(
                        sent_command, response, state_manager.get_all(),
                        f"Invalid status '{response.get('status')}' received for command '{command_name}'",
                        state_manager.get("last_commands_history", []),
                        state_manager.get("last_responses_history", []),
                        sent_schema=state_manager.get("cached_schemas", {}).get(command_name, {}).get("request_schema"),
                        validated=True # Assuming the sent command was validated before sending
                    )
                    continue # Skip further processing for this invalid response

                # 3. If status is "ok", then error should be null
                if response.get("status") == "ok" and response.get("error") is not None:
                    bug_reporter.triage_protocol_error(
                        sent_command, response, state_manager.get_all(),
                        f"Status 'ok' received for command '{command_name}' but 'error' field is not null.",
                        state_manager.get("last_commands_history", []),
                        state_manager.get("last_responses_history", []),
                        sent_schema=state_manager.get("cached_schemas", {}).get(command_name, {}).get("request_schema"),
                        validated=True # Assuming the sent command was validated before sending
                    )
                    # Continue processing, but log as a potential issue
                
                # 4. If status is not "ok", then error should not be null
                if response.get("status") != "ok" and response.get("error") is None:
                    bug_reporter.triage_protocol_error(
                        sent_command, response, state_manager.get_all(),
                        f"Status '{response.get('status')}' received for command '{command_name}' but 'error' field is null.",
                        state_manager.get("last_commands_history", []),
                        state_manager.get("last_responses_history", []),
                        sent_schema=state_manager.get("cached_schemas", {}).get(command_name, {}).get("request_schema"),
                        validated=True # Assuming the sent command was validated before sending
                    )
                    # Continue processing, but log as a potential issue

                # Update working/broken commands and retry info
                command_retry_info = state_manager.get("command_retry_info", {})
                
                if response.get("status") == "ok":
                    working_cmds = state_manager.get("working_commands")
                    if command_name not in working_cmds:
                        working_cmds.append(command_name)
                        state_manager.set("working_commands", working_cmds)
                        logger.info(f"Command '{command_name}' categorized as WORKING.")
                    
                    # Reset retry info on success
                    if command_name in command_retry_info:
                        command_retry_info[command_name] = {"failures": 0, "next_retry_time": 0}
                        state_manager.set("command_retry_info", command_retry_info)
                        logger.info(f"Retry info for '{command_name}' reset due to success.")
                else: # Error or refused
                    broken_cmds = state_manager.get("broken_commands")
                    error_msg = response.get('error', {}).get('message', 'Unknown error')
                    
                    # Check for SQL errors specifically for bug reporting
                    if "no such column" in error_msg.lower() or "sql" in error_msg.lower():
                        bug_reporter.triage_protocol_error(
                            sent_command, response, state_manager.get_all(),
                            f"SQL error detected: {error_msg}",
                            state_manager.get("last_commands_history", []),
                            state_manager.get("last_responses_history", []),
                            sent_schema=state_manager.get("cached_schemas", {}).get(command_name, {}).get("request_schema"),
                            validated=True # Assuming the sent command was validated before sending
                        )

                    if command_name not in [item["command"] for item in broken_cmds]:
                        broken_cmds.append({"command": command_name, "error": error_msg})
                        state_manager.set("broken_commands", broken_cmds)
                        logger.error(f"Command '{command_name}' categorized as BROKEN. Error: {error_msg}")
                    else:
                        for item in broken_cmds:
                            if item["command"] == command_name:
                                item["error"] = error_msg
                                break
                        state_manager.set("broken_commands", broken_cmds)
                    logger.error(f"FAULT LOGGED - Command: {command_name} Error: {error_msg}")

                    # Update retry info on failure
                    BASE_RETRY_DELAY = 5 # seconds
                    retry_data = command_retry_info.get(command_name, {"failures": 0, "next_retry_time": 0})
                    retry_data["failures"] += 1
                    retry_data["next_retry_time"] = time.time() + BASE_RETRY_DELAY * (2 ** (retry_data["failures"] - 1))
                    command_retry_info[command_name] = retry_data
                    state_manager.set("command_retry_info", command_retry_info)
                    logger.warning(f"Command '{command_name}' failed. Next retry in {retry_data['next_retry_time'] - time.time():.2f}s (failures: {retry_data['failures']}).")

                # Update session token if login was successful
                if command_name == "auth.login" and response.get("status") == "ok" and response.get("data", {}).get("session"):
                    state_manager.set("session_token", response["data"]["session"])
                    logger.info("Login successful. Session token obtained and saved.")
                
                # Update current_credits
                if response.get("type") in ["trade.buy_receipt_v1", "trade.sell_receipt_v1"]:
                    credits_remaining = response.get("data", {}).get("credits_remaining")
                    if credits_remaining is not None:
                        state_manager.set("current_credits", float(credits_remaining))
                        logger.info(f"Player credits updated to: {credits_remaining}")
                        state_manager.set("trade_successful", True) # Set trade successful flag
                    else: # For sell_receipt, total_credits might be the relevant field
                        total_credits = response.get("data", {}).get("total_credits")
                        if total_credits is not None:
                            state_manager.set("current_credits", float(total_credits))
                            logger.info(f"Player credits updated to: {total_credits}")
                            state_manager.set("trade_successful", True) # Set trade successful flag
                elif response.get("type") in ["bank.info", "bank.balance"]:
                    balance = response.get("data", {}).get("balance")
                    if balance is not None: 
                        state_manager.set("current_credits", float(balance))
                        logger.info(f"Player bank balance updated to: {balance}")

                # Update player_location_sector
                if response.get("type") == "sector.info":
                    current_sector_id = state_manager.get("player_location_sector") # Get current before update
                    sector_id = response.get("data", {}).get("sector_id")
                    if sector_id is not None:
                        state_manager.set("player_location_sector", sector_id)
                        
                        # Check for new port discovered
                        new_ports = response.get("data", {}).get("ports", [])
                        old_sector_data = state_manager.get("sector_data", {}).get(str(sector_id), {}) # Get old sector data for this sector
                        old_ports = old_sector_data.get("ports", [])
                        if new_ports and not old_ports:
                            state_manager.set("new_port_discovered", True)

                        # Store the full sector data
                        all_sector_data = state_manager.get("sector_data", {})
                        all_sector_data[str(sector_id)] = response.get("data", {})
                        state_manager.set("sector_data", all_sector_data)
                        logger.info(f"Player location sector updated to: {sector_id}")
                        # Also update state for sector_info_fetched_for
                        fetched_for = state_manager.get("sector_info_fetched_for", {})
                        fetched_for[sector_id] = time.time()
                        state_manager.set("sector_info_fetched_for", fetched_for)
                elif response.get("type") == "move.warp":
                    previous_sector_id = state_manager.get("player_location_sector")
                    new_sector_id = response.get("data", {}).get("sector_id")
                    if new_sector_id is not None:
                        state_manager.set("player_location_sector", new_sector_id)
                        if new_sector_id != previous_sector_id:
                            state_manager.set("new_sector_discovered", True) # Set new sector discovered flag
                        logger.info(f"Player warped to sector: {new_sector_id}")

                # Update cached_schemas
                if response.get("status") == "ok" and response.get("type") in ["system.describe_schema","system.schema"]:
                    command_name_for_schema = response.get("data", {}).get("name") # Get the command name from the response data
                    request_schema = response.get("data", {}).get("schema") # Get the actual schema object
                    
                    # WORKAROUND: If the server doesn't provide the 'schema' field, assume an empty schema
                    # This allows validation to proceed without error, effectively disabling it for this command.
                    # The PROTOCOL.v2.md states the schema should be in the 'schema' field.
                    if request_schema is None:
                        logger.warning(f"Server did not provide schema for '{command_name_for_schema}'. Using empty schema as workaround.")
                        request_schema = {}

                    if command_name_for_schema and request_schema is not None: # Check for not None after workaround
                        cached_schemas = state_manager.get("cached_schemas", {})
                        # Store the schema under the command name, with "request_schema" as the key for the schema object
                        cached_schemas[command_name_for_schema] = {"request_schema": request_schema}
                        state_manager.set("cached_schemas", cached_schemas)
                        logger.info(f"Schema for '{command_name_for_schema}' cached.")
                # Update server_capabilities
                if response.get("type") == "system.capabilities" and response.get("status") == "ok":
                    state_manager.set("server_capabilities", response.get("data", {}))
                    logger.info("Server capabilities cached.")
                # Update port_info (or price cache)
                if response.get("type") == "trade.quote":
                    quote_data = response.get("data", {})
                    if quote_data:
                        # Cache this price data based on sector/port/commodity
                        price_cache = state_manager.get("price_cache", {})
                        sector_id = state_manager.get("player_location_sector")
                        port_id = quote_data.get("port_id")
                        
                        if sector_id not in price_cache:
                            price_cache[sector_id] = {}
                        if port_id not in price_cache[sector_id]:
                            price_cache[sector_id][port_id] = {}
                        
                        # Store prices by commodity
                        commodity = quote_data.get("commodity")
                        price_cache[sector_id][port_id][commodity] = {
                            "buy": quote_data.get("total_buy_price"), 
                            "sell": quote_data.get("total_sell_price"),
                            "timestamp": time.time() # Add timestamp
                        }
                        state_manager.set("price_cache", price_cache)
                        logger.info(f"Price for {commodity} in sector {sector_id}, port {port_id} cached.")
                if response.get("type") == "ship.status":
                    ship_data = response.get("data", {}).get("ship")
                    if ship_data:
                        state_manager.set("ship_info", ship_data)
                        logger.info("Ship info updated.")
                
                # Update normalized_commands from system.cmd_list
                if response.get("type") == "system.cmd_list" and response.get("status") == "ok":
                    commands_data = response.get("data", {}).get("commands", [])
                    normalized_commands = {}
                    schemas_to_fetch = state_manager.get("schemas_to_fetch", [])
                    cached_schemas = state_manager.get("cached_schemas", {})

                    for cmd_info in commands_data:
                        canonical_name = cmd_info.get("cmd")
                        if canonical_name:
                            normalized_commands[canonical_name] = canonical_name
                            for alias in cmd_info.get("aliases", []):
                                normalized_commands[alias] = canonical_name
                            
                            # Add to schemas_to_fetch if not already cached or in list
                            if canonical_name not in cached_schemas and canonical_name not in schemas_to_fetch:
                                schemas_to_fetch.append(canonical_name)

                    state_manager.set("normalized_commands", normalized_commands)
                    state_manager.set("schemas_to_fetch", schemas_to_fetch)
                    logger.info("Normalized command vocabulary updated and schemas to fetch queued.")
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
    last_command_name = state_manager.get("last_processed_command_name")
    if not last_command_name:
        return

    # Define intrinsic reward values
    NEW_SECTOR_REWARD = 10.0
    TRADE_SUCCESS_REWARD = 5.0
    NEW_PORT_REWARD = 15.0

    # Reward based on credit change
    previous_credits = state_manager.get("previous_credits", state_manager.get("current_credits", 0.0))
    current_credits = state_manager.get("current_credits", 0.0)
    
    reward = current_credits - previous_credits
    state_manager.set("previous_credits", current_credits) # Update previous credits for next iteration

    # Generate context key for bandit policy
    context_key = _generate_context_key(state_manager.get_all())

    # Add intrinsic rewards
    if state_manager.get("new_sector_discovered"):
        reward += NEW_SECTOR_REWARD
        state_manager.set("new_sector_discovered", False) # Reset flag
        logger.info(f"Intrinsic reward: New sector discovered (+{NEW_SECTOR_REWARD})")

    if state_manager.get("trade_successful"):
        reward += TRADE_SUCCESS_REWARD
        state_manager.set("trade_successful", False) # Reset flag
        logger.info(f"Intrinsic reward: Trade successful (+{TRADE_SUCCESS_REWARD})")

    if state_manager.get("new_port_discovered"):
        reward += NEW_PORT_REWARD
        state_manager.set("new_port_discovered", False) # Reset flag
        logger.info(f"Intrinsic reward: New port discovered (+{NEW_PORT_REWARD})")

    ECON = {"trade.buy","trade.sell","move.warp","bank.deposit","bank.withdraw"}
    META_PENALTY = -0.1
    if last_command_name in ECON:
        bandit_policy.update_q_value(last_command_name, reward, context_key)
    else:
        bandit_policy.update_q_value(last_command_name, META_PENALTY, context_key)
    logger.info(f"Applied reward {reward} to command '{last_command_name}'. New Q-value: {bandit_policy.q_table.get(context_key, {}).get(last_command_name)}")

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

def _generate_context_key(state: dict) -> str:
    """Generates a context key for the bandit policy based on the current state."""
    # Example context: current stage and player's sector
    # This can be expanded to include more relevant state variables
    current_stage = state.get("stage", "bootstrap")
    player_sector = state.get("player_location_sector", 1)
    return f"{current_stage}-{player_sector}"

def get_ollama_response(game_state_json: dict, model: str, current_stage: str):
    logger.debug(f"Type of game_state_json in get_ollama_response: {type(game_state_json)}")
    logger.debug(f"Content of game_state_json in get_ollama_response: {json.dumps(game_state_json, indent=2)}")
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
Your ship cargo: {game_state_json.get("ship_info", {}).get("cargo", {})}
Your ship holds free: {game_state_json.get("ship_info", {}).get("holds", 0) - sum(game_state_json.get("ship_info", {}).get("cargo", {}).values())}

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
    bug_reporter = BugReporter(config.get('bug_report_dir', 'bugs'))
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
                game_command = {
                    "id": next_command_id,
                    "ts": datetime.datetime.now().isoformat() + "Z",
                    "command": next_command_dict["command"],
                    "auth": {"session": state_manager.get("session_token")},
                    "data": next_command_dict.get("data", {}),
                    "meta": {"client_version": config.get("client_version")}
                }

                game_conn.send_command(game_command)
                state_manager.set("last_command_sent_id", next_command_id)

                # Update request timestamps immediately after sending
                if next_command_dict["command"] == "ship.info":
                    state_manager.set("last_ship_info_request_time", time.time())
                elif next_command_dict["command"] == "sector.info":
                    state_manager.set("last_sector_info_request_time", time.time())
                elif next_command_dict["command"] == "bank.info":
                    state_manager.set("last_bank_info_request_time", time.time())

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
