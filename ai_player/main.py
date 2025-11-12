import socket
import json
import time
import uuid
import datetime
import ollama
import logging
import os
import signal
import sys
import argparse
from planner import FiniteStatePlanner

MAX_PORT_INFO_FAILURES = 3 # Define a threshold for port info failures
COOLDOWN_SECONDS = 5 # Cooldown for repeated info requests

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
        try:
            with open(path, 'r') as f:
                self.settings = json.load(f)
        except FileNotFoundError:
            print(f"FATAL: Configuration file not found at {path}")
            sys.exit(1)
        except json.JSONDecodeError:
            print(f"FATAL: Could not decode JSON from {path}")
            sys.exit(1)

    def get(self, key, default=None):
        return self.settings.get(key, default)

class StateManager:
    """Handles loading, saving, and accessing the AI's state."""
    def __init__(self, state_file_path):
        self.path = state_file_path
        self.state = {
            "last_server_response": {},
            "current_credits": "0.00",
            "last_command_sent_id": "",
            "player_location_sector": 1,
            "session_token": None,
            "last_commands_history": [], # Initialize here
            "stage": "discovery", # New: Initial stage
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
            "cached_schemas": {} # New: Stores fetched command schemas
        }
        self.load()

    def load(self):
        if os.path.exists(self.path):
            try:
                with open(self.path, 'r') as f:
                    loaded_state = json.load(f)
                    # Update existing state with loaded values, providing defaults for new keys
                    self.state["last_server_response"] = loaded_state.get("last_server_response", {})
                    self.state["current_credits"] = loaded_state.get("current_credits", "0.00")
                    self.state["last_command_sent_id"] = loaded_state.get("last_command_sent_id", "")
                    self.state["player_location_sector"] = loaded_state.get("player_location_sector", 1)
                    self.state["session_token"] = loaded_state.get("session_token")
                    self.state["last_commands_history"] = loaded_state.get("last_commands_history", [])
                    self.state["stage"] = loaded_state.get("stage", "discovery")
                    self.state["working_commands"] = loaded_state.get("working_commands", [])
                    self.state["broken_commands"] = loaded_state.get("broken_commands", [])
                    self.state["commands_to_try"] = loaded_state.get("commands_to_try", [])
                    self.state["pending_commands"] = loaded_state.get("pending_commands", {})
                    self.state["received_responses"] = loaded_state.get("received_responses", [])
                    self.state["ship_info"] = loaded_state.get("ship_info", None)
                    self.state["port_info"] = loaded_state.get("port_info", None)
                    self.state["sector_info_fetched_for"] = loaded_state.get("sector_info_fetched_for", {})
                    self.state["port_info_failures_per_sector"] = loaded_state.get("port_info_failures_per_sector", {})
                    self.state["last_ship_info_request_time"] = loaded_state.get("last_ship_info_request_time", 0)
                    self.state["last_sector_info_request_time"] = loaded_state.get("last_sector_info_request_time", 0)
                    self.state["cached_schemas"] = loaded_state.get("cached_schemas", {})
                logger.info(f"Successfully loaded state from {self.path}")
            except json.JSONDecodeError:
                logger.error(f"Could not decode state file {self.path}. Starting with fresh state.")
        else:
            logger.info("No state file found. Starting with fresh state.")

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
    def __init__(self, host, port, state_manager):
        self.host = host
        self.port = port
        self.sock = None
        self.buffer = ""
        self.state_manager = state_manager # Store state_manager
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
                return True
            except socket.error as e:
                self.sock = None
                wait_time = min(2 ** attempt, 60)
                logger.error(f"Connection failed: {e}. Retrying in {wait_time} seconds...")
                time.sleep(wait_time)
                attempt += 1
        return False

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

        # Add idempotency key to meta if not already present
        if "meta" not in command_json:
            command_json["meta"] = {}
        if "idempotency_key" not in command_json["meta"]:
            command_json["meta"]["idempotency_key"] = str(uuid.uuid4())

        full_command = json.dumps(command_json) + "\n"
        try:
            self.sock.sendall(full_command.encode('utf-8'))
            logger.info(f"Sent command: {full_command.strip()}")
            
            pending_cmds = self.state_manager.get("pending_commands", {})
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
                        logger.error(f"JSON DECODE ERROR: {e} - Raw line: {line}")
        except socket.timeout:
            pass # Normal for idle state
        except socket.error as e:
            logger.error(f"Socket error during read: {e}. Closing socket.")
            self.close()
        
        return responses

def process_responses(state_manager: StateManager, game_conn: GameConnection):
    responses = game_conn.read_responses()
    for response in responses:
        state_manager.set("last_server_response", response)
        logger.info(f"Server response: {json.dumps(response)}")
        
        reply_to_id = response.get("reply_to")
        if reply_to_id:
            pending_cmds = state_manager.get("pending_commands", {})
            sent_command = pending_cmds.pop(reply_to_id, None) # Remove from pending
            state_manager.set("pending_commands", pending_cmds) # Update state manager

            if sent_command:
                command_name = sent_command.get("command")
                
                # Update working/broken commands
                if response.get("status") == "ok":
                    working_cmds = state_manager.get("working_commands")
                    if command_name not in working_cmds:
                        working_cmds.append(command_name)
                        state_manager.set("working_commands", working_cmds)
                        logger.info(f"Command '{command_name}' categorized as WORKING.")
                else: # Error or refused
                    broken_cmds = state_manager.get("broken_commands")
                    error_msg = response.get('error', {}).get('message', 'Unknown error')
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

                # Update session token if login was successful
                if command_name == "auth.login" and response.get("status") == "ok" and response.get("data", {}).get("session"):
                    state_manager.set("session_token", response["data"]["session"])
                    logger.info("Login successful. Session token obtained and saved.")
                
                # Update current_credits
                if response.get("type") in ["trade.buy_receipt_v1", "trade.sell_receipt_v1"]:
                    credits_remaining = response.get("data", {}).get("credits_remaining")
                    if credits_remaining is not None:
                        state_manager.set("current_credits", str(credits_remaining))
                        logger.info(f"Player credits updated to: {credits_remaining}")
                    else: # For sell_receipt, total_credits might be the relevant field
                        total_credits = response.get("data", {}).get("total_credits")
                        if total_credits is not None:
                            state_manager.set("current_credits", str(total_credits))
                            logger.info(f"Player credits updated to: {total_credits}")
                elif response.get("type") == "bank.info":
                    balance = response.get("data", {}).get("balance")
                    if balance is not None: 
                        state_manager.set("current_credits", str(balance))
                        logger.info(f"Player bank balance updated to: {balance}")

                # Update player_location_sector
                if response.get("type") == "sector.info":
                    sector_id = response.get("data", {}).get("sector_id")
                    if sector_id is not None:
                        state_manager.set("player_location_sector", sector_id)
                        logger.info(f"Player location sector updated to: {sector_id}")
                        # Also update state for sector_info_fetched_for
                        fetched_for = state_manager.get("sector_info_fetched_for", {})
                        fetched_for[sector_id] = time.time()
                        state_manager.set("sector_info_fetched_for", fetched_for)
                elif response.get("type") == "move.warp":
                    new_sector_id = response.get("data", {}).get("sector_id")
                    if new_sector_id is not None:
                        state_manager.set("player_location_sector", new_sector_id)
                        logger.info(f"Player warped to sector: {new_sector_id}")

                # Update cached_schemas
                if response.get("type") == "system.describe_schema" and response.get("status") == "ok":
                    schema_name = sent_command.get("data", {}).get("name")
                    if schema_name:
                        cached_schemas = state_manager.get("cached_schemas", {})
                        cached_schemas[schema_name] = response.get("data", {})
                        state_manager.set("cached_schemas", cached_schemas)
                        logger.info(f"Schema for '{schema_name}' cached.")
                if response.get("type") == "ship.status":
                    ship_data = response.get("data", {}).get("ship")
                    if ship_data:
                        state_manager.set("ship_info", ship_data)
                        logger.info("Ship info updated.")
                
                # Update port_info
                if response.get("type") == "trade.port_info":
                    port_data = response.get("data", {}).get("port")
                    if port_data:
                        state_manager.set("port_info", port_data)
                        state_manager.set("port_info_fetched_at", time.time())
                        logger.info("Port info updated.")
            else:
                logger.warning(f"Received response for unknown or already processed command ID: {reply_to_id}")
        else:
            logger.debug(f"Received unsolicited server message: {json.dumps(response)}")

def format_game_state_for_llm(game_state_json: dict) -> str:
    """
    Formats the game state into a human-readable string for the LLM.
    """
    formatted_state = []

    # Current Credits
    formatted_state.append(f"Credits: {game_state_json.get('current_credits', '0.00')}")

    # Player Location
    current_sector_id = game_state_json.get("player_location_sector", 1)
    last_server_response = game_state_json.get("last_server_response", {})
    sector_name = last_server_response.get("data", {}).get("name", f"Sector {current_sector_id}")
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

    return "\\n".join(formatted_state)

def get_ollama_response(game_state_json: dict, model: str, current_stage: str):
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
    if current_stage == "discovery":
        system_prompt = f"""You are 'AI_QA_Bot'. Your SOLE task is to output a single, valid JSON object for the next game command. DO NOT include any prose or extra text. Your objective is to discover and categorize all available commands. Use the provided game state to formulate the next logical action.

Here is the list of available commands:
{command_list_str}
{last_commands_str}

You must choose a command from this list. Your response MUST be a JSON object with a "command" field containing the command name. If a command requires parameters, include them in a "data" field. For example: {{'command': 'system.describe_schema', 'data': {{'name': 'players'}}}}

CRITICAL: If the last server response contains an "Unknown command" error, you MUST choose a different command from the list. Do not repeat a command that has just resulted in an error.

Your primary goal in this stage is to systematically try every command in the `commands_to_try` list. Once a command is tried, it will be removed from `commands_to_try`. If a command requires parameters, try to guess reasonable default parameters (e.g., for `system.describe_schema`, try `{{ 'name': 'system' }}`). If a command consistently fails or requires complex parameters you cannot guess, categorize it as broken.
"""
    elif current_stage == "economic_optimization" or current_stage == "exploit": # Use for exploit stage as well
        working_commands_str = "\\n".join([f"- `{c}`" for c in game_state_json.get("working_commands", [])])
        system_prompt = f"""You are 'AI_QA_Bot'. Your SOLE task is to output a single, valid JSON object for the next game command. DO NOT include any prose or extra text. Your objective is to quadruple your current credits. Use the provided game state to formulate the next logical action.

Here is the list of commands that have been confirmed to be WORKING:
{working_commands_str}
{last_commands_str}

You must choose a command from this list of WORKING commands. Your response MUST be a JSON object with a "command" field containing the command name. If a command requires parameters, include them in a "data" field. For example: {{'command': 'trade.buy', 'data': {{'port_id': 1, 'commodity': 'ore', 'quantity': 10}}}}

Your current credits are: {game_state_json.get("current_credits", "0.00")}

To achieve your objective of quadrupling your credits, you **must calculate a price differential** before issuing a `trade.buy` or `trade.sell` command. **You cannot buy and sell at the same port.**

**THOUGHT REQUIREMENT:** Your `thought` field **MUST** include a comparison: `Profit Margin Check: Buy Price at Port X vs. Expected Sell Price at Port Y = [Value]`.

To achieve your objective of quadrupling your credits, follow these steps:
1.  **Gather Information:** Use `sector.info` and `trade.port_info` to understand the current sector and available trade opportunities.
2.  **Handle Missing Port Info:** If `port_info` is not available or `trade.port_info` commands are consistently failing, use `move.warp` to move to an adjacent sector to find new trade opportunities.
3.  **Identify Profitable Trades:** Look for commodities that can be bought cheaply and sold for a higher price. **Crucially, profitable trading involves buying at one port and selling at a different port. Do NOT buy and sell at the same port, as this will result in a loss.**
4.  **Execute Trades:** Use `trade.buy` and `trade.sell` with appropriate parameters (port_id, commodity, quantity) to make a profit.
5.  **Manage Funds:** Use `bank.deposit` and `bank.withdraw` to manage your credits.
6.  **Move to New Sectors:** If no profitable trades are available in the current sector, use `move.warp` to move to an adjacent sector (you can get adjacent sectors from `sector.info`).

You must choose a command from the list of WORKING economic commands. Your response MUST be a JSON object with a "command" field containing the command name. If a command requires parameters, include them in a "data" field. For example: {{'command': 'trade.buy', 'data': {{'port_id': 1, 'commodity': 'ore', 'quantity': 10}}}}

Prioritize actions that directly increase your credits. Avoid repeating commands that don't yield new information or contribute to your economic goal.
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
    
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s - %(levelname)s - %(message)s',
        handlers=[
            logging.FileHandler(config.get('log_file', 'ai_player.log')),
            logging.StreamHandler()
        ]
    )
    logger = logging.getLogger(__name__)
    
    signal.signal(signal.SIGINT, signal_handler)
    
    state_manager = StateManager(config.get('state_file', 'state.json'))
    game_conn = GameConnection(config.get('game_host'), config.get('game_port'), state_manager)
    planner = FiniteStatePlanner(state_manager, game_conn, config)

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
            process_responses(state_manager, game_conn)

            # --- Decision Making and Command Sending via Planner ---
            # If there are pending commands, don't send new ones yet
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