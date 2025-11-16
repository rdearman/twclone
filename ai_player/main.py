import sys
import time
import socket
import json
import logging
import uuid
import re
from datetime import datetime

from bug_reporter import BugReporter
from planner import Planner
from state_manager import StateManager
from llm_client import get_ollama_response
from bandit_policy import BanditPolicy, make_context_key 

# --- Standard Logging Setup ---
logger = logging.getLogger()
logger.setLevel(logging.DEBUG)

# Console Handler
c_handler = logging.StreamHandler(sys.stdout)
c_handler.setLevel(logging.DEBUG)  # Set to DEBUG to see all logs
c_formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
c_handler.setFormatter(c_formatter)
logger.addHandler(c_handler)

# --- FIX: File Handler setup ---
def setup_logging(config):
    """Configures file logging based on config."""
    log_file = config.get("log_file", "ai_player.log")
    f_handler = logging.FileHandler(log_file, mode='w')
    f_handler.setLevel(logging.INFO)  # Keep file log cleaner at INFO level
    f_formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    f_handler.setFormatter(f_formatter)
    logger.addHandler(f_handler)

# Suppress noisy libraries
logging.getLogger("urllib3").setLevel(logging.WARNING)
logging.getLogger("httpcore").setLevel(logging.WARNING)
logging.getLogger("httpx").setLevel(logging.WARNING)

# --- Global Shutdown Flag ---
shutdown_flag = False

# --- Game Connection Class ---
class GameConnection:
    """Handles the low-level socket connection to the game server."""
    
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None
        self.buffer = b""

    def connect(self):
        """Establishes connection to the server."""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((self.host, self.port))
            self.sock.setblocking(False)  # Use non-blocking sockets
            logger.info("Successfully connected to game server at %s:%s", self.host, self.port)
            return True
        except socket.error as e:
            logger.critical("Failed to connect to game server: %s", e)
            self.sock = None
            return False

    def disconnect(self):
        """Closes the socket connection."""
        if self.sock:
            try:
                self.sock.close()
                logger.info("Disconnected from game server.")
            except socket.error as e:
                logger.error("Error while disconnecting: %s", e)
        self.sock = None

    def send_command(self, command_dict):
        """Sends a JSON command to the server, suffixed with a newline."""
        if not self.sock:
            logger.warning("Not connected. Cannot send command.")
            return False
        
        try:
            message = json.dumps(command_dict) + "\n"
            self.sock.sendall(message.encode('utf-8'))
            return True
        except socket.error as e:
            logger.error("Error sending command: %s. Reconnecting.", e)
            self.disconnect()
            return False
        except Exception as e:
            logger.error("Unexpected error in send_command: %s", e)
            return False

    def receive_responses(self):
        """Receives data, handles buffering, and yields complete JSON responses."""
        if not self.sock:
            return []

        try:
            data = self.sock.recv(4096 * 8)
            if not data:
                # Server disconnected gracefully
                logger.warning("Server closed the connection.")
                self.disconnect()
                return []
            
            self.buffer += data
        except socket.error as e:
            if e.errno == socket.errno.EWOULDBLOCK or e.errno == socket.errno.EAGAIN:
                # No data available right now, this is normal for non-blocking
                return []
            else:
                # A real socket error occurred
                logger.error("Socket error on receive: %s. Disconnecting.", e)
                self.disconnect()
                return []

        # Process the buffer and extract complete JSON messages
        responses = []
        while b'\n' in self.buffer:
            response_str, self.buffer = self.buffer.split(b'\n', 1)
            response_str = response_str.strip()
            if response_str:
                try:
                    response_json = json.loads(response_str)
                    responses.append(response_json)
                except json.JSONDecodeError as e:
                    logger.error("Failed to decode JSON response: %s", e)
                    logger.debug("Invalid JSON string was: %s", response_str)
        
        return responses

# --- Strategy & Goal Functions ---

def get_strategy_from_llm(game_state, model):
    """
    Asks the LLM for a high-level strategic plan.
    """
    PROMPT_STRATEGY = """
    You are a master space trader. Your goal is to make profit.
    Based on the current game state, provide a high-level strategic plan.
    Return your plan as a JSON list of simple goal strings.
    The goals should be: "goto: <sector_id>", "sell: <commodity_code>", "buy: <commodity_code>".
    
    Example response:
    ["goto: 127", "sell: ORGANICS", "buy: ORE", "goto: 271", "sell: ORE"]

    Current state:
    {game_state}
    
    What is your plan?
    """
    
    try:
        # Re-use your existing get_ollama_response function, but with the new prompt
        response_json = get_ollama_response(
            game_state, 
            model, 
            "PROMPT_STRATEGY", # This is a placeholder key
            override_prompt=PROMPT_STRATEGY # Pass the full prompt
        )
        
        if response_json:
            import re
            import json
            
            # --- FIX: Added re.DOTALL to find JSON that spans multiple lines ---
            match = re.search(r'\[.*\]', response_json, re.DOTALL)
            if match:
                plan_str = match.group(0)
                plan = json.loads(plan_str)
                if isinstance(plan, list):
                    logger.info(f"LLM provided new strategy: {plan}")
                    return plan
        
        logger.warning("LLM failed to provide a valid strategy plan from response.")
        return None

    except Exception as e:
        logger.error(f"Error parsing strategy from LLM response: {e}")
        return None

def is_goal_complete(goal_str, game_state, response_data, response_type):
    """
    Checks if the last server response or current state fulfills the goal.
    """
    try:
        goal_type, _, goal_target = goal_str.partition(":")
        goal_target = goal_target.strip().lower()

        if goal_type == "goto":
            target_sector_id = int(goal_target)
            
            # --- FIX: Check current state first! ---
            # This handles the case where the goal is "goto" a sector we are already in.
            if game_state.get("player_location_sector") == target_sector_id:
                return True

            # Check if we just arrived from a warp
            if response_type == "ship.info" or response_type == "ship.status":
                new_sector = response_data.get("ship", {}).get("location", {}).get("sector_id")
                if new_sector == target_sector_id:
                    return True

        elif goal_type == "sell":
            if response_type != "trade.sell_receipt_v1": return False
            # Check if what we just sold matches the goal
            return any(l.get("commodity", "").lower() == goal_target for l in response_data.get("lines", []))
        
        elif goal_type == "buy":
            if response_type != "trade.buy_receipt_v1": return False
            # Check if what we just bought matches the goal
            return any(l.get("commodity", "").lower() == goal_target for l in response_data.get("lines", []))
    
    except Exception as e:
        logger.warning(f"Error checking goal completion for '{goal_str}': {e}")
    
    return False

# --- Main Game Loop ---

def main():
    global shutdown_flag
    
    # Load configuration
    try:
        with open('config.json', 'r') as f:
            config = json.load(f)
    except FileNotFoundError:
        logger.critical("config.json not found. Exiting.")
        return
    except json.JSONDecodeError:
        logger.critical("config.json is not valid JSON. Exiting.")
        return
    
    # --- Setup Logging from Config ---
    setup_logging(config)

    # --- Initialize Core Components ---
    # --- FIX: Use YOUR config keys ---
    game_host = config.get("game_host", "localhost")
    game_port = config.get("game_port", 9000)
    state_file = config.get("state_file", "state.json")
    bug_path = config.get("bug_report_path", "bug_reports")
    
    game_conn = GameConnection(game_host, game_port)
    state_manager = StateManager(state_file, config)
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
    
    state_manager.set_default("strategy_plan", [])
    state_manager.set_default("session_id", None)
    # We must reset session_id on start, as it's not persistent
    state_manager.set("session_id", None) 

    last_heartbeat = time.time()
    
    # --- Main Loop ---
    while not shutdown_flag:
        try:
            # 1. Handle Connection
            if not game_conn.sock:
                if not game_conn.connect():
                    logger.info("Attempting to reconnect in 5 seconds...")
                    time.sleep(5)
                    continue
                else:
                    # Connection successful, send login
                    idempotency_key = str(uuid.uuid4())
                    
                    # --- THE REAL FIX: Use 'password' not 'token' ---
                    login_cmd = {
                        "id": f"c-{idempotency_key[:8]}",
                        "command": "auth.login",
                        "data": {
                            "password": config.get("player_password"),
                            "username": config.get("player_username") 
                        },
                        "meta": {
                            "client_version": config.get("client_version"),
                            "idempotency_key": idempotency_key
                        }
                    }
                    
                    if not config.get("player_username") or not config.get("player_password"):
                        logger.critical("player_username or player_password not found in config.json. Exiting.")
                        shutdown_flag = True
                        continue

                    logger.info("Sending command: %s", login_cmd)
                    game_conn.send_command(login_cmd)

            # 2. Process Server Responses
            responses = game_conn.receive_responses()
            if responses:
                # --- FIX: Pass config object to fix NameError ---
                process_responses(responses, state_manager, bug_reporter, config)
                last_heartbeat = time.time()
            
            # 3. Handle Heartbeat (Ping)
            if time.time() - last_heartbeat > 20:
                logger.debug("Sending ping to keep connection alive.")
                game_conn.send_command({"command": "player.ping"})
                last_heartbeat = time.time()

            # --- Check if we are authenticated BEFORE planning ---
            game_state = state_manager.get_all()
            if not game_state.get("session_id"):
                logger.debug("Not authenticated yet, waiting for login response...")
                time.sleep(0.5) # Wait a bit
                continue # Skip the rest of the loop
            
            # --- If we are here, we are authenticated ---

            # 4. Get Current State & Strategy
            strategy_plan = game_state.get("strategy_plan", [])
            current_goal = None

            # 5. Check if we need a new strategy
            if not strategy_plan:
                if game_state.get("player_info") and game_state.get("ship_info"): # Only ask if we are 'in-game'
                    logger.info("Strategy plan is empty. Requesting a new one from LLM.")
                    new_plan = get_strategy_from_llm(game_state, config.get("ollama_model")) 
                    if new_plan:
                        state_manager.set("strategy_plan", new_plan)
                        strategy_plan = new_plan
            
            # 6. Get the current goal
            if strategy_plan:
                current_goal = strategy_plan[0] # Get the top goal
                logger.info(f"Current strategic goal: {current_goal}")

            # 7. Get Next Command from Planner
            next_command_dict = planner.get_next_command(game_state, current_goal)
            
            if next_command_dict:
                command_name = next_command_dict.get("command")
                idempotency_key = str(uuid.uuid4())
                
                # Build the full command packet
                full_command = {
                    "id": f"c-{idempotency_key[:8]}",
                    "command": command_name,
                    "data": next_command_dict.get("data", {}),
                    "meta": {
                        "client_version": config.get("client_version")
                    }
                }
                
                # --- THIS IS THE FIX for 1306 ERROR ---
                # Only add idempotency_key where it is allowed
                if command_name in ["trade.buy", "trade.sell"]:
                    # Add to data payload
                    full_command["data"]["idempotency_key"] = idempotency_key
                elif command_name not in ["move.warp"]: 
                    # Add to meta payload for most other commands
                    full_command["meta"]["idempotency_key"] = idempotency_key
                # For move.warp, we add NO key.
                # --- END FIX ---

                logger.info("Sending command: %s", full_command)
                
                if game_conn.send_command(full_command):
                    state_manager.record_command_sent(command_name)
                    if config.get("qa_mode"):
                        bug_reporter.log_command(full_command)
                else:
                    logger.warning("Failed to send command (connection issue).")

            else:
                # --- THIS IS THE FIX for the "stuck goal" loop ---
                if current_goal:
                    # If we had a goal and the planner failed, the goal is impossible.
                    # Clear the plan to force a new one.
                    logger.warning(f"Planner failed to execute goal '{current_goal}'. Clearing strategy to get a new one.")
                    state_manager.set("strategy_plan", []) # Clear the plan
                else:
                    # If there was no goal, just wait for cooldowns.
                    logger.debug("Planner returned no command (no goal). Waiting for cooldowns.")
            
            # Prevent busy-looping
            time.sleep(0.5)

        except KeyboardInterrupt:
            logger.info("Shutdown signal received. Exiting.")
            shutdown_flag = True
        except Exception as e:
            logger.critical("Unhandled exception in main loop: %s", e, exc_info=True)
            time.sleep(5) # Avoid rapid crash-loop
            
    # --- Cleanup ---
    if game_conn:
        game_conn.disconnect()
    state_manager.save_state()
    logger.info("Bot has shut down.")

# --- Response Processing Function ---

def process_responses(responses, state_manager, bug_reporter, config):
    """
    Processes a list of responses from the server, updates state,
    and checks for goal completion.
    
    --- FIX: Added 'config' parameter ---
    """
    game_state_before = state_manager.get_all() # Get state before processing

    for response in responses:
        logger.info("Server response: %s", json.dumps(response, separators=(',', ':')))
        
        # This is a known weak point. A real fix would map sent client-side
        # IDs ('id') to the 'reply_to' field.
        command_name = "unknown"
        if response.get("meta", {}).get("command"):
             command_name = response.get("meta").get("command")

        response_type = response.get("type", "unknown")
        response_data = response.get("data", {}) or {}
        
        if config.get("qa_mode"):
            bug_reporter.log_response(response)

        try:
            if response.get("status") == "ok":
                
                # --- FIX: Detect successful login ---
                if response_type == "auth.session":
                    session_id = response_data.get("session")
                    state_manager.set("session_id", session_id)
                    # --- NEW: Set player_id from login ---
                    player_id = response_data.get("player_id")
                    if player_id:
                         # Store this, though it's mainly for logging
                        state_manager.set("player_id", player_id)
                    logger.info(f"Login successful (received auth.session). Session: {session_id}, PlayerID: {player_id}")
                    command_name = "auth.login" # Assume this response is for auth
                
                # 2) Player Info
                if response_type == "player.info" or response_type == "player.my_info":
                    state_manager.set("player_info", response_data)
                    if response_data.get("ship", {}).get("location"):
                        state_manager.set("player_location_sector", response_data["ship"]["location"]["sector_id"])
                
                # 3) Ship Info
                if response_type == "ship.info" or response_type == "ship.status":
                    ship_data = response_data.get("ship", response_data) # Handle both .info and .status
                    state_manager.set("ship_info", ship_data)
                    if ship_data.get("location"):
                        state_manager.set("player_location_sector", ship_data["location"]["sector_id"])

                # 4) Location Info
                if response_type == "sector.info":
                    # --- THIS IS THE FIX for the 'sector.info' loop ---
                    sector_id = response_data.get("sector_id") # <-- WAS .get("id")
                    if sector_id:
                        state_manager.update_sector_data(str(sector_id), response_data)
                        state_manager.set("player_location_sector", sector_id) 
                
                if response_type == "port.info":
                    port_id = response_data.get("port", {}).get("id")
                    sector_id = response_data.get("port", {}).get("sector_id")
                    if port_id and sector_id:
                        state_manager.update_port_info(str(sector_id), response_data.get("port"))
                
                # 5) Trade Info
                if response_type == "trade.quote":
                    state_manager.update_price_cache(response_data)
                
                # 6) Bank Info
                if response_type == "bank.balance":
                    state_manager.set("bank_balance", response_data.get("balance", 0))

                # 7) Trade Receipts (Update Cargo)
                if response_type == "trade.sell_receipt_v1":
                    sold_items = response_data.get("lines", [])
                    ship_info = state_manager.get("ship_info", {})
                    current_cargo = ship_info.get("cargo", {})
                    for item in sold_items:
                        commodity = item.get("commodity", "").lower()
                        if commodity:
                            current_cargo[commodity] = current_cargo.get(commodity, 0) - item.get("quantity", 0)
                            if current_cargo[commodity] <= 0:
                                del current_cargo[commodity]
                    ship_info["cargo"] = current_cargo
                    state_manager.set("ship_info", ship_info)
                    state_manager.set("trade_successful", True)
                    logger.info("Ship cargo updated after successful trade.sell.")
                
                if response_type == "trade.buy_receipt_v1":
                    bought_items = response_data.get("lines", [])
                    ship_info = state_manager.get("ship_info", {})
                    current_cargo = ship_info.get("cargo", {})
                    for item in bought_items:
                        commodity = item.get("commodity", "").lower()
                        if commodity:
                            current_cargo[commodity] = current_cargo.get(commodity, 0) + item.get("quantity", 0)
                    ship_info["cargo"] = current_cargo
                    state_manager.set("ship_info", ship_info)
                    state_manager.set("trade_successful", True)
                    logger.info("Ship cargo updated after successful trade.buy.")
                
                # --- GOAL COMPLETION CHECK ---
                game_state_after = state_manager.get_all() 
                strategy_plan = game_state_after.get("strategy_plan", [])
                
                if strategy_plan:
                    current_goal = strategy_plan[0]
                    if is_goal_complete(current_goal, game_state_after, response_data, response_type):
                        logger.info(f"Strategic goal '{current_goal}' is complete. Removing from plan.")
                        strategy_plan.pop(0) # Pop the completed goal
                        state_manager.set("strategy_plan", strategy_plan)
                
            else: # Error or refused
                error_data = response.get("error", {})
                error_msg = error_data.get("message", "Unknown error")
                error_code = error_data.get("code")
                
                log_command_name = "unknown"
                
                logger.warning("Server refused command '%s': %s (Code: %s)", log_command_name, error_msg, error_code)
                
                state_manager.record_command_failure(log_command_name)
                
                # --- GOAL INVALIDATION ---
                strategy_plan = state_manager.get("strategy_plan", [])
                if strategy_plan:
                    # Clear the plan on ANY error for now. This is safer.
                    logger.error(f"Goal-related error: {error_msg}. Clearing plan.")
                    state_manager.set("strategy_plan", []) # Clear the plan
                        
                # --- Auto-Recovery Logic ---
                if error_code == 1403: # "Insufficient cargo space"
                    logger.warning("Forcing ship.info refresh due to 'Insufficient cargo' error.")
                    state_manager.set("ship_info", None)
                if error_code == 1402: # "You do not carry enough..."
                    logger.warning("Forcing ship.info refresh due to 'Not enough commodity' error.")
                    state_manager.set("ship_info", None)

                # --- QA Mode: Triage Error ---
                if config.get("qa_mode"):
                    bug_reporter.triage_protocol_error(
                        command_name,
                        response,
                        game_state_before, 
                        error_code,
                        error_msg
                    )

        except Exception as e:
            logger.error("Critical error in process_responses: %s", e, exc_info=True)


if __name__ == "__main__":
    main()
