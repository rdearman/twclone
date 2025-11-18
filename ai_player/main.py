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
logger.setLevel(logging.INFO)
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

def _get_filtered_game_state_for_llm(full_game_state):
    """
    Creates a filtered version of the game state for the LLM,
    including only essential information to reduce token count and provide explicit guidance.
    """
    filtered_state = {}

    # Essential top-level info
    filtered_state["session_id"] = full_game_state.get("session_id")
    current_sector_id = full_game_state.get("player_location_sector")
    filtered_state["player_location_sector"] = current_sector_id
    filtered_state["bank_balance"] = full_game_state.get("bank_balance")

    # Player Info (trimmed)
    player_info_data = full_game_state.get("player_info", {})
    if player_info_data and "player" in player_info_data:
        player_obj = player_info_data.get("player", {})
        filtered_state["player_info"] = {
            "username": player_obj.get("username"),
            "credits": player_obj.get("credits"),
            "faction": player_obj.get("faction"),
            "is_online": player_obj.get("is_online"),
        }

    # Ship Info (trimmed)
    ship_info = full_game_state.get("ship_info", {})
    if ship_info:
        filtered_state["ship_info"] = {
            "type": ship_info.get("type"),
            "max_cargo": ship_info.get("max_cargo"),
            "current_cargo_volume": ship_info.get("current_cargo_volume"),
            "cargo": ship_info.get("cargo", {}), # Keep full cargo details
            "location": ship_info.get("location"), # Keep location details
            "modules": {
                "warp_drive": ship_info.get("modules", {}).get("warp_drive")
            }
        }
        filtered_state["ship_cargo_capacity"] = ship_info.get("max_cargo", 0)
        filtered_state["ship_current_cargo_volume"] = ship_info.get("current_cargo_volume", 0)
        # NEW: Density scanner info
        filtered_state["has_density_scanner"] = bool(ship_info.get("modules", {}).get("density_scanner"))
        
    # Sector Data (current and adjacent only)
    all_sector_data = full_game_state.get("sector_data", {})
    all_port_info = full_game_state.get("port_info_by_sector", {})

    relevant_sector_ids = set()
    current_sector_details = None
    if current_sector_id is not None and str(current_sector_id) in all_sector_data:
        relevant_sector_ids.add(str(current_sector_id))
        current_sector_details = all_sector_data[str(current_sector_id)]
        if "adjacent" in current_sector_details:
            for adj_sector_id in current_sector_details["adjacent"]:
                relevant_sector_ids.add(str(adj_sector_id))
    
    filtered_state["current_sector_info"] = {}
    filtered_state["adjacent_sectors_info"] = []
    filtered_state["has_port_in_current_sector"] = False
    filtered_state["current_port_commodities"] = []
    filtered_state["valid_goto_sectors"] = []
    filtered_state["valid_commodity_names"] = ["ORE", "ORGANICS", "EQUIPMENT", "COLONISTS"] # Explicitly list valid commodities

    if current_sector_details:
        filtered_state["current_sector_info"] = {
            "sector_id": current_sector_details.get("sector_id"),
            "name": current_sector_details.get("name"),
            "has_port": current_sector_details.get("has_port", False)
        }
        if current_sector_details.get("has_port"):
            filtered_state["has_port_in_current_sector"] = True
            current_port_data = all_port_info.get(str(current_sector_id))
            if current_port_data:
                filtered_state["current_port_commodities"] = [
                    {"commodity": c["commodity"], "buy_price": c["buy_price"], "sell_price": c["sell_price"]}
                    for c in current_port_data.get("commodities", [])
                ]
        # NEW: Explicit flag for trading capability
        filtered_state["can_trade_at_current_location"] = filtered_state["has_port_in_current_sector"]
    
    if current_sector_details and "adjacent" in current_sector_details:
        for adj_sector_id in current_sector_details["adjacent"]:
            filtered_state["valid_goto_sectors"].append(adj_sector_id)
            if str(adj_sector_id) in all_sector_data:
                sector_details = all_sector_data[str(adj_sector_id)]
                adj_sector_info = {
                    "sector_id": sector_details.get("sector_id"),
                    "name": sector_details.get("name"),
                    "has_port": sector_details.get("has_port", False)
                }
                # NEW: Add density info if available
                if "density" in sector_details:
                    adj_sector_info["density"] = sector_details["density"]
                filtered_state["adjacent_sectors_info"].append(adj_sector_info)

    return filtered_state

PROMPT_EXPLORE = """
You are a master space explorer. Your goal is to find new sectors and ports.
Based on this filtered summary of the current game state, provide a high-level strategic plan.
Return your plan as a sequence of simple goal strings, one goal per line.
The goals MUST be in the format: "goto: <sector_id>" or "scan: density".

IMPORTANT RULES FOR GENERATING GOALS:
1.  For "goto" goals:
    -   The <sector_id> MUST be one of the sector IDs listed in `valid_goto_sectors`.
    -   Prioritize sectors that have not been explored yet (i.e., not in `adjacent_sectors_info`).
2.  For "scan: density" goals:
    -   You should only use this goal if `has_density_scanner` is `true`.
    -   Use this to gather information about adjacent sectors, especially to find ports.

Do not include any other text, commentary, or explanation.

Current state:
{game_state}

What is your plan?
"""

PROMPT_STRATEGY = """
    You are a master space trader. Your goal is to make profit.
    Based on this filtered summary of the current game state, provide a high-level strategic plan.
    Return your plan as a sequence of simple goal strings, one goal per line.
    The goals MUST be in the format: "goto: <sector_id>", "sell: <commodity_code>", "buy: <commodity_code>", or "scan: density".
    
    IMPORTANT RULES FOR GENERATING GOALS:
    1.  For "goto" goals:
        -   The <sector_id> MUST be one of the sector IDs listed in `valid_goto_sectors`.
        -   Do NOT attempt to 'goto' a sector that is not directly adjacent.
    
    2.  For "buy" or "sell" goals:
        -   You MUST be at a port. `can_trade_at_current_location` must be `true`. If it is `false`, you CANNOT buy or sell.
        -   When selling, you MUST have something in your `ship_info.cargo`. If `ship_info.cargo` is empty, you CANNOT sell.
        -   When buying, you MUST have space in your cargo hold. `ship_current_cargo_volume` must be less than `ship_cargo_capacity`.
        -   The <commodity_code> MUST be one of the valid commodity names listed in `valid_commodity_names`.
        -   Refer to `current_port_commodities` for available items and their prices at the current port.
            -   Example: `current_port_commodities: [{"commodity": "ORE", "buy_price": 100, "sell_price": 80}]` means you can buy ORE for 100 and sell for 80.

    3. For "scan: density" goals:
        - You should only use this goal if `has_density_scanner` is `true`.
        - Use this to gather information about adjacent sectors, especially to find ports.
        - A `density` value of approximately 100 in `adjacent_sectors_info` often indicates the presence of a port. Prioritize `goto` goals to sectors with high density after a scan.
    
    Do not include any other text, commentary, or explanation.
    
    Current state:
    {game_state}
    
    Example of a valid plan:
    scan: density
    goto: 123
    buy: ORE
    goto: 456
    sell: ORE
    
    What is your plan?
    """

def get_strategy_from_llm(game_state, model, stage):
    """
    Asks the LLM for a high-level strategic plan.
    """
    # Filter the game state to reduce token count for the LLM
    filtered_game_state = _get_filtered_game_state_for_llm(game_state)

    # --- FIX 3.1 & 3.2 ---
    # If we are not at a port, we must be exploring.
    if not filtered_game_state.get("can_trade_at_current_location", False):
        stage = "explore"
    
    if stage == "explore":
        prompt = PROMPT_EXPLORE
    else:
        prompt = PROMPT_STRATEGY

    try:
        response_text = get_ollama_response(
            filtered_game_state, 
            model, 
            "PROMPT_STRATEGY",
            override_prompt=prompt
        )
        
        if response_text:
            import json, re

            plan = []

            # 1) Try JSON first
            try:
                json_response = json.loads(response_text)

                # Case A: {"goto": 94, "sell": "ORE"}
                if isinstance(json_response, dict):
                    tmp = []
                    for key, value in json_response.items():
                        verb = str(key).strip().lower()
                        if verb not in ("goto", "buy", "sell", "scan"):
                            continue
                        if verb == "goto":
                            try:
                                value = int(value)
                            except (TypeError, ValueError):
                                continue
                            tmp.append(f"goto: {value}")
                        elif verb == "scan":
                            tmp.append("scan: density")
                        else:
                            # buy/sell commodities as upper-case strings
                            commodity = str(value).strip().upper()
                            if not commodity:
                                continue
                            tmp.append(f"{verb}: {commodity}")
                    plan.extend(tmp)

                # Case B: ["goto: 94", "sell": "ORE"]
                elif isinstance(json_response, list):
                    tmp = []
                    for item in json_response:
                        if isinstance(item, str):
                            tmp.append(item.strip())
                        elif isinstance(item, dict):
                            # Support [{"goto": 94}, {"sell": "ORE"}]
                            for key, value in item.items():
                                verb = str(key).strip().lower()
                                if verb not in ("goto", "buy", "sell", "scan"):
                                    continue
                                if verb == "goto":
                                    try:
                                        value = int(value)
                                    except (TypeError, ValueError):
                                        continue
                                    tmp.append(f"goto: {value}")
                                elif verb == "scan":
                                    tmp.append("scan: density")
                                else:
                                    commodity = str(value).strip().upper()
                                    if not commodity:
                                        continue
                                    tmp.append(f"{verb}: {commodity}")
                    plan.extend(tmp)

            except Exception:
                # JSON parse failed – fall through to text parsing
                pass

            # 2) Fallback: extract "goto: X"/"buy: Y"/"sell: Z" from free-text
            if not plan:
                goal_pattern = re.compile(r'(goto:\s*\d+|sell:\s*\w+|buy:\s*\w+|scan:\s*density)', re.IGNORECASE)
                raw_goals = goal_pattern.findall(response_text)
                plan.extend(g.strip() for g in raw_goals)

            # 3) Validate against filtered_game_state
            if plan:
                validated = []
                valid_gotos = set(filtered_game_state.get("valid_goto_sectors", []))
                valid_comms = set(c.upper() for c in filtered_game_state.get("valid_commodity_names", []))
                can_trade_here = filtered_game_state.get("can_trade_at_current_location", False)

                for goal in plan:
                    if not isinstance(goal, str) or ":" not in goal:
                        continue
                    verb, _, arg = goal.partition(":")
                    verb = verb.strip().lower()
                    arg = arg.strip()

                    if verb == "goto":
                        try:
                            sid = int(arg)
                        except ValueError:
                            continue
                        if sid not in valid_gotos:
                            continue
                        validated.append(f"goto: {sid}")
                    elif verb in ("buy", "sell"):
                        if not can_trade_here:
                            logger.warning(f"LLM proposed '{goal}' but not at a port. Ignoring.")
                            continue
                        comm = arg.upper()
                        if comm not in valid_comms:
                            continue
                        validated.append(f"{verb}: {comm}")
                    elif verb == "scan":
                        validated.append("scan: density")

                # Optional: de-duplicate consecutive identical goals
                deduped = []
                for g in validated:
                    if not deduped or g != deduped[-1]:
                        deduped.append(g)

                if deduped:
                    logger.info(f"LLM provided new strategy: {deduped}")
                    return deduped
        
        logger.warning(f"LLM failed to provide a valid strategy plan from response: {response_text}")
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
            
            # This handles the case where the goal is "goto" a sector we are already in.
            if game_state.get("player_location_sector") == target_sector_id:
                return True

            # Check if we just arrived from a warp (move.result)
            if response_type == "move.result":
                new_sector = response_data.get("to_sector_id")
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

        elif goal_type == "survey" and goal_target == "port":
            if response_type == "port.info":
                # Goal is complete if we have received port info with commodities.
                return bool(response_data.get("port", {}).get("commodities"))
            # Fallback check on the state, in case the response was processed before the goal check.
            current_sector_id = str(game_state.get("player_location_sector"))
            port_info = game_state.get("port_info_by_sector", {}).get(current_sector_id)
            if port_info and port_info.get("commodities"):
                return True
            return False

        elif goal_type == "scan" and goal_target == "density":
            return response_type == "sector.density.scan"
    except Exception as e:
        logger.warning(f"Error checking goal completion for '{goal_str}': {e}")
    
    return False

# --- Main Game Loop ---

def bootstrap_schemas(game_conn, state_manager, config):
    """
    Fetches all command schemas from the server on startup.
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
            "idempotency_key": idempotency_key
        }
    }
    logger.info("Requesting command list from server...")
    game_conn.send_command(schema_request)
    state_manager.state["pending_schema_requests"]["system.cmd_list"] = request_id
    time.sleep(0.1) # Avoid overwhelming the server


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
    schemas_bootstrapped = False
    
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
                process_responses(responses, game_conn, state_manager, bug_reporter, bandit_policy, config)
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

            # --- Bootstrap Schemas after login ---
            if not schemas_bootstrapped:
                bootstrap_schemas(game_conn, state_manager, config)
                schemas_bootstrapped = True
                # Give server time to respond to schema requests
                logger.info("Waiting for schema responses...")
                time.sleep(2) 
                continue

            # 4. Get Current State & Strategy
            game_state = state_manager.get_all()
            
            # --- FIX: Cooldown if out of turns ---
            if (game_state.get("player_info") or {}).get("player", {}).get("turns_remaining", 1) <= 0:
                logger.info("Out of turns. Cooling down for 1 hour.")
                time.sleep(3600)
                continue

            strategy_plan = game_state.get("strategy_plan", [])
            current_goal = None

            # 5. Check if we need a new strategy
            if not strategy_plan:
                game_state = state_manager.get_all() # get fresh state
                current_sector_id = str(game_state.get("player_location_sector"))
                current_sector_data = game_state.get("sector_data", {}).get(current_sector_id, {})
                at_port = current_sector_data.get("has_port", False)
                
                port_info = game_state.get("port_info_by_sector", {}).get(current_sector_id)
                survey_needed = at_port and (not port_info or not port_info.get("commodities"))

                if survey_needed:
                    logger.info("At a port, but survey is incomplete. Setting goal to survey.")
                    state_manager.set("strategy_plan", ["survey: port"])
                else:
                    if game_state.get("player_info") and game_state.get("ship_info"): # Only ask if we are 'in-game'
                        logger.info("Strategy plan is empty. Requesting a new one from LLM.")
                        logger.debug(f"Calling LLM with model: {config.get('ollama_model')}")
                        new_plan = get_strategy_from_llm(game_state, config.get("ollama_model"), planner.current_stage) 
                        if new_plan and isinstance(new_plan, list) and all(isinstance(g, str) and ":" in g for g in new_plan):
                            state_manager.set("strategy_plan", new_plan)
                            strategy_plan = new_plan
                        else:
                            logger.warning(f"LLM provided an invalid plan: {new_plan}. Retrying.")
                            time.sleep(2) # Wait a moment before retrying
                            continue
            
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
                        "client_version": config.get("client_version"),
                        # --- THIS IS THE FIX for 1306 ERROR ---
                        # Always include the idempotency_key in meta
                        "idempotency_key": idempotency_key 
                    }
                }

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
            logger.critical(f"Unhandled exception in main loop: {e}", exc_info=True)
            time.sleep(5) # Avoid rapid crash-loop
            
    # --- Cleanup ---
    if game_conn:
        game_conn.disconnect()
    state_manager.save_state()
    logger.info("Bot has shut down.")

# --- Response Processing Function ---

SHIP_TYPE_HOLDS_MAP = {
    "Scout Marauder": 25,
    # Add other ship types as needed if similar discrepancies are found
}

def process_responses(responses, game_conn, state_manager, bug_reporter, bandit_policy, config):
    """
    Processes a list of responses from the server, updates state,
    and checks for goal completion.
    
    --- FIX: Added 'config' and 'bandit_policy' parameters ---
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
                
                # --- Handle Schema Responses ---
                if response_type == "system.schema":
                    schema_name = response_data.get("name")
                    if schema_name and 'schema' in response_data:
                        state_manager.add_schema(schema_name, response_data['schema'])
                        logger.debug(f"Cached schema for command: {schema_name}")
                    continue # Don't process schema responses further

                if response_type == "system.cmd_list":
                    commands_to_fetch = [cmd.get("cmd") for cmd in response_data.get("commands", [])]
                    logger.info(f"Received command list from server: {commands_to_fetch}")
                    
                    commands_to_ignore = [
                        "system.hello", 
                        "system.capabilities", 
                        "system.describe_schema",
                        "system.cmd_list"
                    ]

                    for command_name in commands_to_fetch:
                        if command_name in commands_to_ignore:
                            continue
                        if command_name in state_manager.get("schema_blacklist", []):
                            logger.debug(f"Skipping schema request for blacklisted command: {command_name}")
                            continue

                        idempotency_key = str(uuid.uuid4())
                        request_id = f"c-{idempotency_key[:8]}"
                        schema_request = {
                            "id": request_id,
                            "command": "system.describe_schema",
                            "data": {
                                "type": "command",
                                "name": command_name
                            },
                            "meta": {
                                "client_version": config.get("client_version"),
                                "idempotency_key": idempotency_key
                            }
                        }
                        logger.debug(f"Requesting schema for command: {command_name}")
                        game_conn.send_command(schema_request)
                        state_manager.state["pending_schema_requests"][command_name] = request_id
                        time.sleep(0.1)
                    continue

                # --- FIX: Detect successful login ---
                if response_type == "auth.session":
                    session_id = response_data.get("session")
                    state_manager.set("session_id", session_id)
                    state_manager.update_player_info(response_data)
                
                # 2) Player Info
                if response_type == "player.info" or response_type == "player.my_info":
                    state_manager.update_player_info(response_data)
                    if response_data.get("ship", {}).get("location"):
                        state_manager.set("player_location_sector", response_data["ship"]["location"]["sector_id"])
                
                # 3) Ship Info
                if response_type == "ship.info" or response_type == "ship.status":
                    ship_data = response_data.get("ship", response_data) # Handle both .info and .status

                    # --- START FIX for incorrect ship.holds ---
                    ship_type_name = ship_data.get("type", {}).get("name")
                    if ship_type_name and ship_type_name in SHIP_TYPE_HOLDS_MAP:
                        corrected_holds = SHIP_TYPE_HOLDS_MAP[ship_type_name]
                        if ship_data.get("holds") != corrected_holds:
                            logger.warning(f"Overriding ship.holds for '{ship_type_name}'. Server reported {ship_data.get('holds')}, using corrected value {corrected_holds}.")
                            ship_data["holds"] = corrected_holds
                    # --- END FIX for incorrect ship.holds ---

                    state_manager.set("ship_info", ship_data)
                    if ship_data.get("location"):
                        state_manager.set("player_location_sector", ship_data["location"]["sector_id"])

                # 4) Location Info
                if response_type == "move.result":
                    # --- Update player location immediately ---
                    if response_data.get("current_sector"):
                        new_sector = response_data.get("current_sector")
                        state_manager.set("player_location_sector", new_sector)
                        # --- FIX: Track recent sectors ---
                        visits = state_manager.get("recent_sectors", [])
                        visits.append(new_sector)
                        state_manager.set("recent_sectors", visits[-10:])

                        # --- FIX: Refresh player info after move ---
                        idempotency_key = str(uuid.uuid4())
                        refresh_cmd = {
                            "id": f"c-{idempotency_key[:8]}",
                            "command": "player.my_info",
                            "data": {},
                            "meta": {
                                "client_version": config.get("client_version"),
                                "idempotency_key": idempotency_key
                            }
                        }
                        game_conn.send_command(refresh_cmd)

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
                # Try to get the command name from the reply_to ID
                if response.get("reply_to"):
                    # This is a hacky way to get the command name from the ID
                    # A better way would be to store a map of ID to command name
                    # when sending the command.
                    for cmd_name, cmd_id in state_manager.get("pending_schema_requests", {}).items():
                        if cmd_id == response["reply_to"]:
                            log_command_name = cmd_name
                            break

                logger.warning("Server refused command '%s': %s (Code: %s)", log_command_name, error_msg, error_code)
                
                state_manager.record_command_failure(log_command_name)
                
                # If schema request failed with 1104, add to blacklist
                if response_type == "error" and error_code == 1104 and log_command_name != "unknown":
                    state_manager.add_to_schema_blacklist(log_command_name)


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
