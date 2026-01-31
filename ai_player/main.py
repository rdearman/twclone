import sys
import time
import socket
import json
import logging
import uuid
import re
import random # Add this import
from datetime import datetime

from bug_reporter import BugReporter
from planner import Planner
from state_manager import StateManager
from llm_client import get_ollama_response, parse_llm_json
from bandit_policy import BanditPolicy, make_context_key 
from typing import Optional
from helpers import canon_commodity

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
        "cargo": cargo_summary
    }

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
    filtered_state["adjacent_sectors"] = [s["to_sector"] if isinstance(s, dict) else s for s in adj]
    
    # Global Market Intelligence (Top 3 profitable routes only)
    market_intel = game_state.get("market_intelligence", {})
    # (Simplified pass-through of pre-calculated intel)
    filtered_state["market_top_prices"] = market_intel

    filtered_state["last_action_result"] = game_state.get("last_action_result")
    filtered_state["valid_comms"] = state_manager.get("valid_commodity_names")

    return filtered_state

PROMPT_CONTRACT_BLOCK = """
State: {game_state}

Goal: Maximize profit by flipping commodities.
Rules:
- NO "scan: density" if scanned_current_sector is true.
- Use ONLY commodities in valid_comms.
- "goto: <id>" to travel.
- Only "buy:" or "sell:" if at a port.

Output JSON: {{"plan": ["goto: 5", "buy: ORE", "goto: 10", "sell: ORE"]}}
"""

PROMPT_EXPLORE = """Explorer goal: Find new sectors and ports. """ + PROMPT_CONTRACT_BLOCK

PROMPT_STRATEGY = """Trader goal: Identify low-price buy and high-price sell sectors. Flip between them. """ + PROMPT_CONTRACT_BLOCK

PROMPT_QA_OBJECTIVE = """You are a QA testing bot for a space game. Your current high-level objective is to "{qa_objective}".
Based on this objective, provide a detailed strategic plan.
"""

QA_OBJECTIVES = [
    "test basic trading",
    "test combat system",
    "test planet landing",
    "test ship upgrades",
    "test mining operations",
    "test bounty hunting",
    "test illegal trading"
]

def _get_fallback_plan(game_state, state_manager, planner):
    """
    Provides a simple fallback plan when the LLM fails to respond.
    Prioritizes surveying, then selling cargo, then buying if empty, then exploration.
    """
    import random
    
    logger.info("Generating fallback plan (LLM unavailable)")

    current_sector = game_state.get("player_location_sector")
    sector_data = game_state.get("sector_data", {}).get(str(current_sector), {})
    
    # 0. Survey Logic: If we are at a port and haven't surveyed it, DO IT.
    if sector_data.get("has_port"):
        port_info = game_state.get("port_info_by_sector", {}).get(str(current_sector))
        if not port_info or not port_info.get("commodities"):
            logger.info("Fallback: At an unsurveyed port. Setting goal to SURVEY.")
            return ["survey: port"]

    ship_info = game_state.get("ship_info", {})
    cargo = ship_info.get("cargo", [])
    
    # Filter for sellable items (quantity > 0)
    sellable_items = [item for item in cargo if item.get("quantity", 0) > 0]
    
    # 1. Sell Logic: If we have cargo, find where to sell it
    if sellable_items:
        # Check if current port buys it PROFITABLY
        if sector_data.get("has_port"):
            port_id = str(planner._find_port_in_sector(game_state, current_sector))
            port_sell_prices = game_state.get("price_cache", {}).get(port_id, {}).get("sell", {})
            
            for item in sellable_items:
                commodity = canon_commodity(item.get("commodity"))
                purchase_price = item.get("purchase_price")
                sell_price = port_sell_prices.get(commodity)
                
                if sell_price is not None and purchase_price is not None and sell_price > purchase_price:
                    logger.info(f"Fallback: Profitable sell opportunity for {commodity} at current port ({sell_price} > {purchase_price}).")
                    return [f"sell: {commodity}"]
        
        # Find nearest port that DOES buy it profitably
        for item in sellable_items:
            commodity = canon_commodity(item.get("commodity"))
            purchase_price = item.get("purchase_price")
            if purchase_price is None: continue

            for s_id, p_info in game_state.get("port_info_by_sector", {}).items():
                p_id_str = str(p_info.get("port_id"))
                sell_price = game_state.get("price_cache", {}).get(p_id_str, {}).get("sell", {}).get(commodity)
                
                if sell_price is not None and sell_price > purchase_price:
                    target_sector = int(s_id)
                    logger.info(f"Fallback: Found profitable sell for {commodity} in sector {target_sector} ({sell_price} > {purchase_price}). Going there.")
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
        if not prices: # Simplified: if no buy prices, it's unsurveyed
            unsurveyed_ports.append(int(s_id))
    
    if unsurveyed_ports:
        target = random.choice(unsurveyed_ports)
        logger.info(f"Fallback: Moving to unsurveyed port in sector {target}")
        return [f"goto: {target}"]

    # 4. Exploration Logic (Existing)
    adj = sector_data.get("adjacent_sectors") or sector_data.get("adjacent") or []
    
    # Extract adjacent sector IDs
    adjacent_ids = []
    for item in adj:
        if isinstance(item, dict) and "to_sector" in item:
            adjacent_ids.append(item["to_sector"])
        elif isinstance(item, int):
            adjacent_ids.append(item)
    
    # Prioritize unexplored adjacent sectors
    universe_map = game_state.get("universe_map", {})
    unexplored = [s for s in adjacent_ids if not universe_map.get(str(s), {}).get('is_explored')]
    
    if unexplored:
        target = random.choice(unexplored)
        logger.info(f"Fallback: Exploring unexplored sector {target}")
        return [f"goto: {target}"]
    elif adjacent_ids:
        # All adjacent explored, pick random one
        target = random.choice(adjacent_ids)
        logger.info(f"Fallback: Moving to sector {target}")
        return [f"goto: {target}"]
    else:
        # No adjacent data, explore blindly
        valid_gotos = state_manager.get_valid_goto_sectors()
        if valid_gotos:
            target = random.choice(valid_gotos)
            logger.info(f"Fallback: Trying known sector {target}")
            return [f"goto: {target}"]
        else:
            logger.warning("Fallback: No navigation options, scanning")
            return ["scan: density"]

def get_strategy_from_llm(game_state, model, stage, state_manager, qa_objective: Optional[str] = None):
    """
    Asks the LLM for a high-level strategic plan.
    """
    # Filter the game state to reduce token count for the LLM
    filtered_game_state = _get_filtered_game_state_for_llm(game_state, state_manager)
    logger.debug(f"DEBUG: Known sectors passed to LLM: {filtered_game_state.get('all_known_sectors')}")

    # --- FIX 3.1 & 3.2 ---
    # If we are not at a port, we must be exploring.
    if not filtered_game_state.get("can_trade_at_current_location", False) and not qa_objective:
        stage = "explore"
    
    if qa_objective:
        # Pre-format the QA objective into the specific prompt section
        qa_intro = PROMPT_QA_OBJECTIVE.format(qa_objective=qa_objective)
        prompt = qa_intro + PROMPT_CONTRACT_BLOCK.format(game_state=json.dumps(filtered_game_state, indent=2))
        logger.info(f"Using QA Objective prompt for LLM: {qa_objective}")
    elif stage == "explore":
        prompt = "You are a master space explorer. Your goal is to find new sectors and ports.\n" + PROMPT_CONTRACT_BLOCK.format(game_state=json.dumps(filtered_game_state, indent=2))
    else:
        prompt = "You are a master space trader. Your goal is to make profit.\n" + PROMPT_CONTRACT_BLOCK.format(game_state=json.dumps(filtered_game_state, indent=2))

    try:
        response_text = get_ollama_response(
            filtered_game_state,
            model,
            "PROMPT_STRATEGY",
            override_prompt=prompt
        )
        
        if not response_text:
            return None

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
        valid_comms = set(c.upper() for c in filtered_game_state.get("valid_commodity_names", []))
        can_trade = filtered_game_state.get("can_trade_at_current_location", False)
        
        goal_pattern = re.compile(r'^(goto|buy|sell|scan|planet|combat)\s*:\s*(.+)$', re.IGNORECASE)

        for goal in raw_plan:
            if not isinstance(goal, str): continue
            
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
                    # Relaxed validation: Allow any integer target.
                    # The Planner will handle pathfinding or rejection if truly unreachable.
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
                if arg.lower() in ("land", "info"):
                    validated_plan.append(f"planet: {arg.lower()}")
            
            elif verb == "combat":
                if arg.lower() == "attack":
                     validated_plan.append("combat: attack")
            
            else:
                logger.debug(f"Skipping unknown verb: {verb}")

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
                
                # 3. Pick an UNEXPLORED neighbor to warp to (prioritize exploration)
                current_sector_id = game_state.get("player_location_sector")
                sector_data = game_state.get("sector_data", {}).get(str(current_sector_id), {})
                adjacent = sector_data.get("adjacent", [])
                
                if adjacent:
                    # Extract sector IDs from adjacent list
                    adjacent_ids = []
                    for adj in adjacent:
                        if isinstance(adj, dict) and "to_sector" in adj:
                            adjacent_ids.append(adj["to_sector"])
                        elif isinstance(adj, int):
                            adjacent_ids.append(adj)
                    
                    if adjacent_ids:
                        # Prioritize unexplored sectors
                        universe_map = game_state.get("universe_map", {})
                        unexplored = [s for s in adjacent_ids if not universe_map.get(str(s), {}).get('is_explored')]
                        
                        if unexplored:
                            random_dest = random.choice(unexplored)
                            logger.info(f"Boredom filter: Moving to unexplored sector {random_dest}")
                        else:
                            random_dest = random.choice(adjacent_ids)
                            logger.info(f"Boredom filter: All adjacent explored, picking random {random_dest}")
                        
                        return [f"goto: {random_dest}"]
                
                # If no map data, warp blindly to a random sector (Emergency Jump)
                random_dest = random.randint(1, 1000) # Assuming 1000 sectors
                logger.warning(f"Boredom filter: Emergency jump to sector {random_dest}")
                return [f"goto: {random_dest}"]
            # ---------------------------------------------------------------

            logger.info(f"LLM provided valid plan: {final_plan}")
            return final_plan
        
        logger.warning(f"LLM response yielded no valid goals after validation. Raw goals: {raw_plan}, response_text: {response_text}")
        return None

    except Exception as e:
        logger.error(f"Error parsing strategy from LLM response: {e}", exc_info=True)
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
            return any(canon_commodity(l.get("commodity", "")).lower() == goal_target for l in response_data.get("lines", []))
        
        elif goal_type == "buy":
            if response_type != "trade.buy_receipt_v1": return False
            # Check if what we just bought matches the goal
            return any(canon_commodity(l.get("commodity", "")).lower() == goal_target for l in response_data.get("lines", []))

        elif goal_type == "survey" and goal_target == "port":
            # Check if survey is STRICTLY complete (all commodities quoted)
            # We mirror the planner's strict logic here to avoid premature popping.
            current_sector_id = str(game_state.get("player_location_sector"))
            port_info = game_state.get("port_info_by_sector", {}).get(current_sector_id)
            
            if not port_info or not port_info.get("commodities"):
                return False # Don't even have the list yet
            
            port_id_str = str(port_info.get("port_id"))
            price_cache = game_state.get("price_cache", {}).get(port_id_str, {})
            
            for c in port_info.get("commodities", []):
                c_code = canon_commodity(c.get("commodity"))
                if not c_code: continue
                
                has_buy = price_cache.get("buy", {}).get(c_code) is not None
                has_sell = price_cache.get("sell", {}).get(c_code) is not None
                
                if not (has_buy or has_sell):
                    return False # Found a commodity with no price data
            
            return True # All commodities have at least one price

        elif goal_type == "scan" and goal_target == "density":
            return response_type == "sector.density.scan"
        
        elif goal_type == "planet.land":
             # Assume success if we got a planet.info or planet.land response, or just OK
             # This is a bit loose but sufficient for now
             return response_type in ["planet.info", "planet.land"]

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
            "idempotency_key": idempotency_key,
            "session_token": state_manager.get("session_id")
        }
    }

    logger.info("Requesting command list from server...")
    game_conn.send_command(schema_request)

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
    
    # --- Setup Logging from Config ---
    setup_logging(config)

    delay = random.uniform(0, 180)
    print(f"Staggering start: waiting {delay:.2f}s...")
    time.sleep(delay)    
    
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
    state_manager.set_default("current_qa_objective", None) # NEW: For QA testing mode
    # We must reset session_id on start, as it's not persistent
    state_manager.set("session_id", None) 

    last_heartbeat = time.time()
    schemas_bootstrapped = False
    
    # --- Main Loop ---
    while not shutdown_flag:
        try:
            # 0. Always process server responses first
            responses = game_conn.receive_responses()
            if responses:
                process_responses(responses, game_conn, state_manager, bug_reporter, bandit_policy, config, planner)
                last_heartbeat = time.time()

            game_state = state_manager.get_all() # Always get the freshest state here

            # --- NEW: Check if a deterministic rule already sent a command ---
            if state_manager.get("command_sent_in_response_loop"):
                state_manager.set("command_sent_in_response_loop", False)
                logger.info("Skipping planner turn because a deterministic rule already sent a command.")
                time.sleep(0.5)
                continue
            # -----------------------------------------------------------------

            # 3. Handle Heartbeat (Ping)
            if time.time() - last_heartbeat > 20:
                logger.debug("Sending ping to keep connection alive.")
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
                game_conn.send_command(ping_cmd)
                last_heartbeat = time.time()

            # 1. Handle Connection & Authentication
            if not game_conn.sock:
                if not game_conn.connect():
                    logger.info("Attempting to reconnect in 5 seconds...")
                    time.sleep(5)
                    continue
            
            # --- Check if we are authenticated BEFORE planning ---
            game_state = state_manager.get_all()
            if not game_state.get("session_id"):
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
                
                if not config.get("player_username") or not config.get("player_password"):
                    logger.critical("player_username or player_password not found in config.json. Exiting.")
                    shutdown_flag = True
                    continue

                logger.info("No session detected. Sending login command: %s", login_cmd)
                game_conn.send_command(login_cmd)
                
                # Wait for login response to avoid spamming login requests
                # We'll re-loop and process responses in the next iteration
                time.sleep(1) 
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
                logger.info("Bootstrap command full: %s", full_command)
                if game_conn.send_command(full_command):
                    state_manager.record_command_sent(bootstrap_cmd["command"])
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
                survey_needed = at_port and (not port_info or not port_info.get("commodities"))

                if config.get("qa_mode"):
                    current_qa_objective = state_manager.get("current_qa_objective")
                    if current_qa_objective is None:
                        current_qa_objective = random.choice(QA_OBJECTIVES)
                        state_manager.set("current_qa_objective", current_qa_objective)
                        logger.info(f"New QA Objective selected: {current_qa_objective}")
                    
                    logger.info(f"Requesting new plan for QA Objective: {current_qa_objective}")
                    new_plan = get_strategy_from_llm(game_state, config.get("ollama_model"), planner.current_stage, state_manager, qa_objective=current_qa_objective)
                    
                    if new_plan and isinstance(new_plan, list) and all(isinstance(g, str) and ":" in g for g in new_plan):
                        state_manager.set("strategy_plan", new_plan)
                        strategy_plan = new_plan
                    else:
                        logger.warning(f"LLM failed to provide a valid plan for QA Objective '{current_qa_objective}'. Using fallback plan.")
                        fallback_plan = _get_fallback_plan(game_state, state_manager, planner)
                        state_manager.set("strategy_plan", fallback_plan)
                        strategy_plan = fallback_plan
                        # Do not clear the objective yet, let the fallback help achieve it or progress
                        time.sleep(2) 
                        # Removed 'continue' so we proceed to execute the fallback plan immediately
                else: # Not in QA mode, revert to old strategy logic
                    if survey_needed:
                        logger.info("At a port, but survey is incomplete. Setting goal to survey.")
                        state_manager.set("strategy_plan", ["survey: port"])
                    else:
                        if game_state.get("player_info") and game_state.get("ship_info"): # Only ask if we are 'in-game'
                            logger.info("Strategy plan is empty. Requesting a new one from LLM.")
                            logger.debug(f"Calling LLM with model: {config.get('ollama_model')}")
                            new_plan = get_strategy_from_llm(game_state, config.get("ollama_model"), planner.current_stage, state_manager) 
                            if new_plan and isinstance(new_plan, list) and all(isinstance(g, str) and ":" in g for g in new_plan):
                                state_manager.set("strategy_plan", new_plan)
                                strategy_plan = new_plan
                            else:
                                logger.warning(f"LLM provided an invalid plan: {new_plan}. Using fallback plan.")
                                # Fallback: Create a simple exploration plan
                                fallback_plan = _get_fallback_plan(game_state, state_manager, planner)
                                state_manager.set("strategy_plan", fallback_plan)
                                strategy_plan = fallback_plan
                                time.sleep(2) # Wait a moment before continuing
            
            # 6. Get the current goal
            if strategy_plan:
                current_goal = strategy_plan[0] # Get the top goal
                logger.info(f"Current strategic goal: {current_goal}")

            # 7. Get Next Command from Planner
            next_command_dict = planner.get_next_command(game_state, current_goal)
            
            if next_command_dict:
                command_name = next_command_dict.get("command")
                idempotency_key = str(uuid.uuid4())
                
                # IMPORTANT: trade.buy/trade.sell require idempotency_key INSIDE data (server handler reads data.idempotency_key)
                # planner.py now adds this, but we ensure it here just in case logic was bypassed
                data_payload = dict(next_command_dict.get("data", {}) or {})
                if command_name in ("trade.buy", "trade.sell") and "idempotency_key" not in data_payload:
                     # This should be redundant if planner is working, but safe
                     data_payload["idempotency_key"] = idempotency_key

                
                # Build the full command packet
                full_command = {
                    "id": f"c-{idempotency_key[:8]}",
                    "command": command_name,
                    "data": data_payload,
                    "meta": {
                        "client_version": config.get("client_version"),
                        # --- THIS IS THE FIX for 1306 ERROR ---
                        # Always include the idempotency_key in meta
                        "idempotency_key": idempotency_key,
                        "session_token": game_state.get("session_id")
                    }
                }

                logger.info("Sending command: %s", full_command)
                
                if game_conn.send_command(full_command):
                    state_manager.add_pending_command(full_command['id'], full_command)
                    state_manager.record_command_sent(command_name)
                    if config.get("qa_mode"):
                        bug_reporter.log_command(full_command)
                    
                    # If an invariant command was sent, prioritize waiting for its response
                    if next_command_dict.get("is_invariant"):
                        logger.info(f"Sent invariant command '{command_name}'. Pausing to await response.")
                        time.sleep(1) # Wait a bit longer for crucial state updates
                        continue # Immediately re-loop to process responses
                else:
                    logger.warning("Failed to send command (connection issue).")

            else:
                # --- THIS IS THE FIX for the "stuck goal" loop ---
                if current_goal:
                    # If we had a goal and the planner failed, the goal is impossible.
                    # Clear the plan to force a new one.
                    logger.warning(f"Planner failed to execute goal '{current_goal}'. Clearing strategy.")
                    state_manager.set("strategy_plan", []) # Clear the plan
                    
                    # FORCE A RANDOM MOVE to unstick the bot
                    valid_gotos = game_state.get("valid_goto_sectors", [])
                    if valid_gotos:
                        random_sector = random.choice(valid_gotos)
                        logger.info(f"FALLBACK: Attempting random warp to sector {random_sector} to break loop.")
                        fallback_cmd = {
                            "id": f"c-fallback-{uuid.uuid4().hex[:8]}",
                            "command": "move.warp",
                            "data": {"to_sector_id": random_sector},
                            "meta": {
                                "client_version": config.get("client_version"), 
                                "idempotency_key": str(uuid.uuid4()),
                                "session_token": game_state.get("session_id")
                            }
                        }
                        game_conn.send_command(fallback_cmd)
                        state_manager.add_pending_command(fallback_cmd['id'], fallback_cmd)
                    
                    # If in QA mode, also clear the current objective to force a new one
                    if config.get("qa_mode"):
                        state_manager.set("current_qa_objective", None)
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

def process_responses(responses, game_conn, state_manager, bug_reporter, bandit_policy, config, planner=None):
    """
    Processes a list of responses from the server, updates state,
    and checks for goal completion.
    """
    game_state_before = state_manager.get_all() # Get state before processing

    for response in responses:
        logger.info("Server response: %s", json.dumps(response, separators=(',', ':')))
        
        request_id = response.get("reply_to")
        sent_command = None
        if request_id:
            sent_command = state_manager.get_pending_command(request_id)

        command_name = "unknown"
        if sent_command:
            command_name = sent_command.get("command", "unknown")
        
        response_type = response.get("type", "unknown")
        response_data = response.get("data", {}) or {}
        
        if config.get("qa_mode"):
            bug_reporter.log_response(response)

        # Diagnostic log for trade results
        if planner and command_name in ("trade.buy", "trade.sell"):
             planner.handle_trade_response(sent_command, response) # Pass the full sent_command dict

        if request_id:
            state_manager.remove_pending_command(request_id)

        try:
            if response.get("status") == "ok":
                # --- Feedback Loop: Record Success ---
                state_manager.record_command_success(command_name)
                
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
                        profit = state_manager.update_cargo_after_sell(sold_items, port_id, credits_remaining=credits_remaining)
                        reward = profit / 1000.0 # Normalize profit for the reward
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
                        state_manager.update_cargo_after_buy(bought_items, port_id, credits_remaining=credits_remaining)
                    state_manager.set("trade_successful", True)
                    logger.info("Ship cargo state updated after successful trade.buy.")
                
                # Give feedback for the last action that led to this success
                if last_action and last_context:
                    bandit_policy.give_feedback(last_action, last_context, reward)

                # --- Handle Schema Responses ---
                if response_type == "system.schema":
                    schema_name = response_data.get("name")
                    if schema_name and 'schema' in response_data:
                        state_manager.add_schema(schema_name, response_data['schema'])
                        logger.debug(f"Cached schema for command: {schema_name}")
                    continue

                if response_type == "system.cmd_list":
                    commands_to_fetch = [cmd.get("cmd") for cmd in response_data.get("commands", [])]
                    logger.info(f"Received command list from server: {commands_to_fetch}")
                    
                    commands_to_ignore = [
                        "system.hello", "system.capabilities", "system.describe_schema", "system.cmd_list", "system.schema_list", "player.list_online"
                    ]

                    for command_name_to_fetch in commands_to_fetch:
                        if command_name_to_fetch in commands_to_ignore:
                            continue
                        if command_name_to_fetch in state_manager.get("schema_blacklist", []):
                            logger.debug(f"Skipping schema request for blacklisted command: {command_name_to_fetch}")
                            continue

                        idempotency_key = str(uuid.uuid4())
                        request_id_new = f"c-{idempotency_key[:8]}"
                        schema_request = {
                            "id": request_id_new, "command": "system.describe_schema",
                            "data": {"type": "command", "name": command_name_to_fetch},
                            "meta": {
                                "client_version": config.get("client_version"),
                                "idempotency_key": idempotency_key,
                                "session_token": state_manager.get("session_id")
                            }
                        }
                        logger.debug(f"Requesting schema for command: {command_name_to_fetch}")
                        game_conn.send_command(schema_request)
                        state_manager.state["pending_schema_requests"][command_name_to_fetch] = request_id_new
                        time.sleep(0.1)
                    continue

                if response_type == "auth.session":
                    # Fix: Use session_token, not session (server returns session_token)
                    session_token = response_data.get("session_token") or response_data.get("session")
                    state_manager.set("session_id", session_token)
                    
                    # Wrap in 'player' key if it looks like player info but is missing the wrapper
                    p_info = response_data if "player" in response_data else {"player": response_data}
                    
                    # Ensure player_id is present in the player object for downstream logic
                    if "player_id" in response_data and "id" not in p_info["player"]:
                        p_info["player"]["id"] = response_data["player_id"]

                    state_manager.update_player_info(p_info)
                
                elif response_type == "player.info" or response_type == "player.my_info":
                    if "player" not in response_data:
                        # Server might return flat data, wrap it if so
                        response_data = {"player": response_data}
                    state_manager.update_player_info(response_data)
                
                elif response_type in ("ship.info", "ship.status", "ship.claimed"):
                    ship_obj = response_data.get("ship", response_data)
                    # update_player_info expects {"ship": {...}} 
                    state_manager.update_player_info({"ship": ship_obj})
                    if response_type == "ship.claimed":
                        logger.info(f"Bootstrap: Successfully claimed ship {ship_obj.get('id', '?')}")
                
                if response_type == "move.result":
                    if response.get("status") == "ok":
                        new_sector = response_data.get("to_sector_id")
                        if new_sector:
                            state_manager.set("player_location_sector", new_sector)
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
                            game_conn.send_command(next_cmd)
                
                # --- NEW: Handle Pathfind Response ---
                sent_cmd = state_manager.get_pending_command(request_id)
                if sent_cmd and sent_cmd.get("command") == "move.pathfind":
                    if response.get("status") == "ok":
                        # FIX: Protocol returns 'steps', not 'path'
                        steps = response_data.get("steps", [])
                        if steps:
                            state_manager.set("current_path", steps)
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
                
                elif response_type == "shipyard.list":
                    # Store available ships for bootstrap ship claiming
                    ships = response_data.get("ships", [])
                    if ships:
                        state_manager.set("shipyard_list", ships)
                        logger.info(f"Bootstrap: Received shipyard list with {len(ships)} available ships.")
                
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
                
                elif response_type == "bank.balance":
                    state_manager.set("bank_balance", response_data.get("balance", 0))

                game_state_after = state_manager.get_all() 
                strategy_plan = game_state_after.get("strategy_plan", [])
                
                if not state_manager.validate_state():
                    bug_reporter.report_state_inconsistency(game_state_after, "State inconsistency detected after server response.")

                if strategy_plan:
                    current_goal = strategy_plan[0]
                    if is_goal_complete(current_goal, game_state_after, response_data, response_type):
                        logger.info(f"Strategic goal '{current_goal}' is complete. Removing from plan.")
                        strategy_plan.pop(0)
                        state_manager.set("strategy_plan", strategy_plan)
                
            else: # Error or refused
                error_data = response.get("error", {})
                error_msg = error_data.get("message", "Unknown error")
                error_code = error_data.get("code")
                
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

                last_action = game_state_before.get("last_action")
                last_context = game_state_before.get("last_context_key")
                if last_action and last_context:
                    bandit_policy.give_feedback(last_action, last_context, -1.0) # Negative reward for failure
                
                if response_type == "error" and error_code == 1104 and command_name != "unknown":
                    state_manager.add_to_schema_blacklist(command_name)


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
                        # state_manager.add_to_warp_blacklist(to_sector_id) # Don't blacklist yet, location might just be wrong
                        
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
                            game_conn.send_command(re_sync_cmd)

                if sent_command and command_name == "move.warp":
                    to_sector_id = sent_command.get("data", {}).get("to_sector_id")
                    if error_code == 1453 and to_sector_id is not None:
                        logger.warning(f"Move.warp to sector {to_sector_id} failed with 'No warp link'. Blacklisting for future attempts.")
                        state_manager.add_to_warp_blacklist(to_sector_id)
                
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
                    game_conn.send_command(resync_cmd)

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
                    game_conn.send_command(resync_cmd)
                # --------------------------------------------------------------------

                if config.get("qa_mode"):
                    bug_reporter.triage_protocol_error(command_name, response, game_state_before, error_code, error_msg)

        except Exception as e:
            logger.error("Critical error in process_responses: %s", e, exc_info=True)



if __name__ == "__main__":
    # Allow: python main.py config_bot_001.json
    cfg = sys.argv[1] if len(sys.argv) > 1 else "config.json"
    main(cfg)
