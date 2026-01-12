import logging
import time
import random
import json
import uuid
from jsonschema import validate, ValidationError
from bandit_policy import BanditPolicy, make_context_key # Import the unified context key function
from typing import Optional, Any, Dict
from helpers import canon_commodity, generate_idempotency_key

logger = logging.getLogger(__name__)

# This is no longer used for the action catalogue, but kept for reference
GAMEPLAY_COMMANDS = {
    "move.warp",
    "sector.info",
    "ship.info",
    "player.my_info",
    "trade.port_info",
    "trade.quote",
    "trade.buy",
    "trade.sell",
#    "bank.balance",
}

class Planner:
    def __init__(self, state_manager, bug_reporter, bandit_policy, config):
        self.state_manager = state_manager
        self.bug_reporter = bug_reporter
        self.bandit_policy = bandit_policy
        self.config = config
        self.player_name = self.config.get("player_username", "unknown")
        self.current_stage = self.state_manager.get("stage", "start") # start, explore, survey, exploit

    def handle_trade_response(self, cmd, resp):
        """Temporary diagnostic for trade results."""
        error_data = resp.get("error") or {}
        logger.warning(
            "TRADE RESULT bot=%s cmd=%s status=%s code=%s msg=%s",
            self.player_name,
            cmd,
            resp.get("status"),
            error_data.get("code"),
            error_data.get("message"),
        )

    # --- Deterministic Trade Rules (NEW) ---

    # --- Tactical (Goal) Layer (NEW/FIXED) ---

    def _find_port_in_sector(self, current_state, sector_id):
        """Helper to get the port_id if we are at a port."""
        port_info = current_state.get("port_info_by_sector", {}).get(str(sector_id))
        if port_info:
            # Handle the nested structure e.g. {"121": {"port": {...}}} or {"port_id": 121, ...}
            if "port" in port_info and "id" in port_info["port"]:
                 return port_info["port"]["id"]
            if "port_id" in port_info:
                return port_info.get("port_id")
        
        # Fallback: Check sector_data
        sector_data = current_state.get("sector_data", {}).get(str(sector_id), {})
        ports = sector_data.get("ports", [])
        if ports:
            return ports[0].get("port_id") or ports[0].get("id")

        logger.warning(f"Could not find port_id in sector {sector_id}")
        return None

    def _achieve_goal(self, current_state, goal_str):
        """
        Returns the specific command dictionary needed to work towards the goal.
        This is the "tactical" part.
        """
        try:
            if not isinstance(goal_str, str) or ":" not in goal_str:
                logger.error(f"Invalid goal format: {goal_str}. Expected a string like 'goto: 1'.")
                return None

            goal_type, _, goal_target = goal_str.partition(":")
            goal_target = goal_target.strip() # Keep case for commodities


            
            ship_id = current_state.get("ship_info", {}).get("id") 
            current_sector = current_state.get("player_location_sector")

            if goal_type == "goto":
                try:
                    target_sector = int(goal_target)
                except ValueError:
                    logger.error(f"Invalid target sector ID in goal '{goal_str}'.")
                    return None
                
                if current_sector == target_sector:
                    logger.debug("Goal 'goto' complete (at target).")
                    return None 

                sector_data = current_state.get("sector_data", {}).get(str(current_sector), {})
                adjacent_list = sector_data.get("adjacent", [])
                
                # Extract sector numbers from adjacent warps
                # adjacent could be a list of dicts like [{'to_sector': 7}, ...] or just [7, ...]
                adjacent_sectors = []
                for item in adjacent_list:
                    if isinstance(item, dict):
                        adjacent_sectors.append(item.get("to_sector"))
                    else:
                        adjacent_sectors.append(item)

                # 0. Missing Adjacency Data -> Fetch it
                # Note: "adjacent" not in sector_data means we lack data; empty [] means no warp links exist
                if "adjacent" not in sector_data:
                    logger.info(f"Goto goal but no adjacency data for sector {current_sector}. Requesting sector.info.")
                    return {"command": "sector.info", "data": {"sector_id": int(current_sector)}}

                # 1. If adjacent, just warp
                if target_sector in adjacent_sectors:
                    if self._is_command_ready("move.warp", current_state.get("command_retry_info", {})):
                        logger.info(f"Target sector {target_sector} is adjacent. Warping.")
                        return {"command": "move.warp", "data": {"to_sector_id": target_sector}}
                    else:
                        logger.warning(f"Goal 'goto: {target_sector}' wants move.warp, but it's on cooldown/blacklisted.")
                        return None

                # 2. Try Local Pathfinding (BFS on known sectors)
                local_path = self.state_manager.find_path(current_sector, target_sector)
                if local_path and len(local_path) > 1:
                    next_hop = local_path[1]
                    # DOUBLE CHECK: Is the next hop actually adjacent? 
                    # BFS should guarantee this, but if sector_data is corrupt, we verify.
                    if next_hop in adjacent_sectors:
                        logger.info(f"Local path found to {target_sector}: {local_path}. Next hop: {next_hop}")
                        if self._is_command_ready("move.warp", current_state.get("command_retry_info", {})):
                            return {"command": "move.warp", "data": {"to_sector_id": next_hop}}
                        else:
                            logger.warning(f"Warp to {next_hop} on cooldown.")
                            return None
                    else:
                        logger.warning(f"Local path next hop {next_hop} is NOT adjacent to {current_sector}! Stale data?")

                # 3. Try Server Pathfinding (Cached)
                server_path = current_state.get("current_path", [])
                if (server_path and len(server_path) > 1 and 
                    server_path[0] == current_sector and server_path[-1] == target_sector):
                    
                    next_hop = server_path[1]
                    if next_hop in adjacent_sectors:
                        logger.info(f"Using cached server path to {target_sector}. Next hop: {next_hop}")
                        if self._is_command_ready("move.warp", current_state.get("command_retry_info", {})):
                            return {"command": "move.warp", "data": {"to_sector_id": next_hop}}
                        else:
                            return None
                    else:
                        logger.warning(f"Cached server path next hop {next_hop} is NOT adjacent to {current_sector}!")

                # 4. Request Server Path (Authoritative)
                # If we are here, we don't have a valid adjacent next hop. Force move.pathfind.
                if self._is_command_ready("move.pathfind", current_state.get("command_retry_info", {})):
                    logger.info(f"Target {target_sector} is distant or path unknown. Requesting authoritative pathfind.")
                    return {
                        "command": "move.pathfind", 
                        "data": {
                            "from_sector_id": current_sector,
                            "to_sector_id": target_sector
                        }
                    }

                # 5. Fallback: Explore Random Adjacent (Step toward unknown)
                # If we can't pathfind, just move somewhere to expand the map
                warp_blacklist = self.state_manager.get("warp_blacklist", [])
                candidates = [s for s in adjacent_sectors if s not in warp_blacklist and s != current_sector]
                
                if candidates and self._is_command_ready("move.warp", current_state.get("command_retry_info", {})):
                    chosen = random.choice(candidates)
                    logger.info(f"Goto {target_sector} path unknown; exploring via warp to {chosen}.")
                    return {"command": "move.warp", "data": {"to_sector_id": chosen}}
                
                return None

            elif goal_type == "sell":
                # FIX 1: Sanitize the commodity name (e.g., "OREx" -> "ORE")
                clean_commodity = canon_commodity(goal_target)
                if not clean_commodity:
                    logger.warning(f"Ignoring invalid commodity '{goal_target}' hallucinated by LLM.")
                    return None
                goal_target = clean_commodity

                # FIX 2: If we are blind, fetch info instead of giving up
                current_sector = current_state.get("player_location_sector")
                sector_data = current_state.get("sector_data", {}).get(str(current_sector))
                
                if not sector_data:
                    logger.info(f"Goal is 'sell' but we have no sector data. Auto-switching to 'sector.info'.")
                    return {"command": "sector.info", "data": {"sector_id": int(current_sector)}}

                if not self._at_port(current_state):
                    logger.warning(f"Goal 'sell' active but not at a port. Searching for nearest known port.")
                    # Find any known port from state
                    known_ports = current_state.get("port_info_by_sector", {}).keys()
                    if known_ports:
                        # Simple logic: pick the first one. TODO: Nearest.
                        target_sector = list(known_ports)[0]
                        logger.info(f"Redirecting to known port at sector {target_sector} to sell.")
                        return self._achieve_goal(current_state, f"goto: {target_sector}")
                    else:
                        logger.warning(f"Goal 'sell' failed: no port detected and no known ports in memory.")
                        return None 

                port_id = self._find_port_in_sector(current_state, current_sector)
                if port_id in current_state.get("port_trade_blacklist", []):
                    logger.warning(f"Goal 'sell' failed: port {port_id} is in trade blacklist.")
                    return None
                
                commodity_to_sell = goal_target

                # --- NEW: Filter illegal commodities early ---
                LEGAL_COMMODITIES = {"ORE", "ORG", "EQU", "COLONISTS"} # Define legal commodities
                if commodity_to_sell not in LEGAL_COMMODITIES:
                    logger.warning(f"Rejecting sell goal for illegal commodity '{commodity_to_sell}'.")
                    return None
                # ---------------------------------------------
                
                # Validation: Check if port actually trades this commodity
                port_info = current_state.get("port_info_by_sector", {}).get(str(current_sector), {})
                commodities_info = port_info.get("commodities", [])
                traded_commodities = [canon_commodity(c.get("commodity")) for c in commodities_info]

                if traded_commodities and commodity_to_sell not in traded_commodities:
                     logger.warning(f"Goal 'sell' failed: Port {port_id} does not trade {commodity_to_sell}.")
                     return None
                
                # Check if port is full
                target_comm_info = next((c for c in commodities_info if canon_commodity(c.get("commodity")) == commodity_to_sell), None)
                if target_comm_info:
                    current_qty = target_comm_info.get("quantity", 0)
                    max_qty = target_comm_info.get("max_quantity", 0)
                    if current_qty >= max_qty and max_qty > 0:
                        logger.warning(f"Goal 'sell' failed: Port {port_id} is FULL of {commodity_to_sell} ({current_qty}/{max_qty}).")
                        return None

                port_id_str = str(port_id)
                sell_price = current_state.get("price_cache", {}).get(port_id_str, {}).get("sell", {}).get(commodity_to_sell)

                if sell_price is None:
                    logger.info(f"Sell price for {commodity_to_sell} missing. Requesting trade.quote first.")
                    return {"command": "trade.quote", "data": {"port_id": port_id, "commodity": commodity_to_sell, "quantity": 1}}

                cargo = current_state.get("ship_info", {}).get("cargo", {})
                # Cargo is a list of dicts: [{"commodity": "ORE", "quantity": 10, "purchase_price": 50}]
                # Need to iterate to find quantity
                quantity = sum(item.get("quantity", 0) for item in cargo if canon_commodity(item.get("commodity")) == commodity_to_sell)
                
                if not quantity:
                    logger.warning(f"Goal 'sell' failed: no '{commodity_to_sell}' in cargo.")
                    return None 

                return {"command": "trade.sell", "data": {
                    "port_id": port_id,
                    "items": [{"commodity": commodity_to_sell, "quantity": int(quantity)}]
                }}

            elif goal_type == "buy":
                # FIX 1: Sanitize the commodity name (e.g., "OREx" -> "ORE")
                clean_commodity = canon_commodity(goal_target)
                if not clean_commodity:
                    logger.warning(f"Ignoring invalid commodity '{goal_target}' hallucinated by LLM.")
                    return None
                goal_target = clean_commodity

                # FIX 2: If we are blind, fetch info instead of giving up
                current_sector = current_state.get("player_location_sector")
                sector_data = current_state.get("sector_data", {}).get(str(current_sector))
                
                if not sector_data:
                    logger.info(f"Goal is 'buy' but we have no sector data. Auto-switching to 'sector.info'.")
                    return {"command": "sector.info", "data": {"sector_id": int(current_sector)}}

                if not self._at_port(current_state):
                    logger.warning(f"Goal 'buy' failed: no port detected in sector {current_sector}.")
                    return None 

                port_id = self._find_port_in_sector(current_state, current_sector)
                if port_id in current_state.get("port_trade_blacklist", []):
                    logger.warning(f"Goal 'buy' failed: port {port_id} is in trade blacklist.")
                    return None
                
                commodity_to_buy = goal_target

                # --- NEW: Filter illegal commodities early ---
                LEGAL_COMMODITIES = {"ORE", "ORG", "EQU", "COLONISTS"} # Define legal commodities
                if commodity_to_buy not in LEGAL_COMMODITIES:
                    logger.warning(f"Rejecting buy goal for illegal commodity '{commodity_to_buy}'.")
                    return None
                # ---------------------------------------------
                
                # Validation: Check if port actually trades this commodity
                port_info = current_state.get("port_info_by_sector", {}).get(str(current_sector), {})
                traded_commodities = [canon_commodity(c.get("commodity")) for c in port_info.get("commodities", [])]
                
                if traded_commodities and commodity_to_buy not in traded_commodities:
                     logger.warning(f"Goal 'buy' failed: Port {port_id} does not trade {commodity_to_buy}.")
                     return None

                port_id_str = str(port_id)
                buy_price = current_state.get("price_cache", {}).get(port_id_str, {}).get("buy", {}).get(commodity_to_buy)
                
                if buy_price is None:
                    # If price is missing, issue a trade.quote first
                    logger.info(f"Buy price for {commodity_to_buy} missing. Requesting trade.quote first.")
                    return {"command": "trade.quote", "data": {"port_id": port_id, "commodity": commodity_to_buy, "quantity": 1}}

                free_holds = self._get_free_holds(current_state)
                if free_holds <= 0:
                    logger.warning(f"Goal 'buy' failed: no free holds to buy '{commodity_to_buy}'.")
                    return None

                player_credits_str = current_state.get("player_info", {}).get("player", {}).get("credits", "0")
                try:
                    player_credits = float(player_credits_str)
                except (ValueError, TypeError):
                    player_credits = 0.0

                max_affordable_quantity = int(player_credits / buy_price) if buy_price > 0 else 0
                
                max_quantity = min(free_holds, max_affordable_quantity)

                # FUZZING LOGIC
                quantity = self._get_fuzzed_buy_quantity(max_quantity)

                if quantity == 0 and max_quantity > 0:
                    logger.info("Fuzzer decided to buy 0, but max is > 0. Skipping to avoid wasting a turn.")
                    return None # Don't issue a command that will definitely do nothing

                if quantity > 0:
                    logger.info(f"Preparing to BUY {quantity} of {commodity_to_buy} at port {port_id}. Price: {buy_price}.")
                    return {"command": "trade.buy", "data": {
                        "port_id": port_id,
                        "items": [{"commodity": commodity_to_buy, "quantity": quantity}]
                    }}

            elif goal_type == "survey" and goal_target == "port":
                if not self._at_port(current_state):
                    logger.warning("Goal 'survey: port' failed: not at a port. Falling back.")
                    return None
                
                if self._survey_complete(current_state):
                    logger.debug("Goal 'survey: port' already complete.")
                    return None

                logger.info("Achieving 'survey: port'.")
                port_id = self._find_port_in_sector(current_state, current_sector)
                if not port_id:
                    logger.warning("Goal 'survey: port' failed: could not find port ID.")
                    return None
                
                port_info = current_state.get("port_info_by_sector", {}).get(str(current_sector), {})
                
                price_cache = current_state.get("price_cache", {}).get(str(port_id), {"buy": {}, "sell": {}})

                for c in port_info.get("commodities", []):
                    c_code = canon_commodity(c.get("commodity"))
                    if not c_code: continue

                    # Smart selection: quote if missing EITHER buy OR sell price
                    if price_cache["buy"].get(c_code) is None or \
                       price_cache["sell"].get(c_code) is None:
                        logger.info(f"Calling trade.quote for unquoted commodity: {c_code}")
                        return {"command": "trade.quote", "data": {"port_id": port_id, "commodity": c_code, "quantity": 1}}
                
                # If we reach here, all commodities are quoted, but _survey_complete returned False
                # This should ideally not happen if _survey_complete is correct.
                logger.warning("Survey: port logic reached end without quoting all commodities, but none found unquoted.")
                return {"command": "trade.port_info", "data": {}} # Fallback to ensure port info is fresh
            
            elif goal_type == "scan" and goal_target == "density":
                return {"command": "sector.scan.density", "data": {}}

            elif goal_type == "combat" and goal_target == "attack":
                # Check if there are targets
                sector_id = str(current_state.get("player_location_sector"))
                sector_data = current_state.get("sector_data", {}).get(sector_id, {})
                ships = sector_data.get("ships_present") or sector_data.get("ships", [])
                
                # Filter out our own ship
                my_ship_id = current_state.get("ship_info", {}).get("id")
                targets = [s for s in ships if s.get("id") != my_ship_id]
                
                if targets:
                    logger.info("Executing goal 'combat: attack'")
                    # Planner's _build_payload will handle picking the specific target_id
                    return {"command": "combat.attack", "data": {}}
                else:
                    logger.warning("Goal 'combat: attack' failed: No targets in sector.")
                    return None

            elif goal_type == "planet" and goal_target == "land":
                # Check for planet
                sector_id = str(current_state.get("player_location_sector"))
                sector_data = current_state.get("sector_data", {}).get(sector_id, {})
                
                if sector_data.get("has_planet"):
                     logger.info("Executing goal 'planet: land'")
                     return {"command": "planet.land", "data": {}}
                else:
                     logger.warning("Goal 'planet: land' failed: No planet in sector.")
                     return None

            elif goal_type == "planet" and goal_target == "info":
                 # Check for planet
                sector_id = str(current_state.get("player_location_sector"))
                sector_data = current_state.get("sector_data", {}).get(sector_id, {})
                
                if sector_data.get("has_planet"):
                     logger.info("Executing goal 'planet: info'")
                     return {"command": "planet.info", "data": {}}
                else:
                     logger.warning("Goal 'planet: info' failed: No planet in sector.")
                     return None

        except Exception as e:
            logger.error(f"Error in _achieve_goal for '{goal_str}': {e}", exc_info=True)
        
        return None # Fallback

    # --- Bootstrap Logic (Priority 0) ---
    def ensure_minimum_world_state(self, current_state):
        """
        Ensures we have the basic info (Player, Ship, Current Sector) 
        BEFORE asking the LLM for a plan.
        Also refreshes sector data periodically to detect enemy ships.
        """
        
        # 1. Check Player Info - CRITICAL: Must fetch before anything else
        player_info = current_state.get("player_info")
        player_data = player_info.get("player", {}) if player_info else {}
        
        # If player info is missing or incomplete (missing ship_id), fetch it.
        if not player_info or "player" not in player_info or "ship_id" not in player_data:
            logger.info("Bootstrap: Fetching player info (missing or incomplete).")
            return {"command": "player.my_info", "data": {}}
        
        # 1b. Check if player has a ship - if not, let LLM handle acquiring one
        ship_id = player_data.get("ship_id", 0)
        if ship_id > 0:
            # 2. Check Ship Info - FIX: Check for 'id' to ensure it's not missing or placeholder
            ship_info = current_state.get("ship_info")
            if not ship_info or "id" not in ship_info:
                logger.info("Bootstrap: Fetching ship info (missing ID).")
                return {"command": "ship.info", "data": {}}
        else:
            # Player has no ship - this is OK, let the LLM ask for one as its first action
            logger.info("Bootstrap: Player has no ship - LLM will handle ship acquisition.")
        
        # 3. Check Current Sector Data (CRITICAL FIX)
        # We perform this check EVERY turn, not just at startup.
        current_sector = current_state.get("player_location_sector")
        
        # Fallback if location is missing in root but exists in player info
        if current_sector is None:
            p_info = current_state.get("player_info", {}).get("player", {})
            current_sector = p_info.get("sector")

        if current_sector:
            sector_data = current_state.get("sector_data", {}).get(str(current_sector))
            
            # If we are blind (no data), we MUST look.
            if not sector_data:
                logger.info(f"Bootstrap: Blind in sector {current_sector}. Fetching sector info.")
                return {"command": "sector.info", "data": {"sector_id": int(current_sector)}}

            # But if adjacent is an empty list [], that's valid data meaning no warp links exist.
            # FIX: Check for 'adjacent_sectors' (v2 protocol) OR 'adjacent' (internal/v1)
            has_adjacency = "adjacent" in sector_data or "adjacent_sectors" in sector_data
            
            if not has_adjacency:
                logger.info(f"Bootstrap: Adjacency missing for sector {current_sector}. Fetching sector info.")
                return {"command": "sector.info", "data": {"sector_id": int(current_sector)}}
            
            # NEW: If we have no port info for this sector, fetch it
            # This ensures we know if there's a trading opportunity
            if "has_port" not in sector_data:
                logger.info(f"Bootstrap: Missing port info for sector {current_sector}. Fetching sector.info.")
                return {"command": "sector.info", "data": {"sector_id": int(current_sector)}}
            
            # NEW: Periodic refresh (every 60 seconds) to detect enemy ships
            # Get the last refresh timestamp for this sector
            last_refresh = sector_data.get("_last_refreshed", 0)
            current_time = time.time()
            if current_time - last_refresh > 60:
                logger.debug(f"Sector {current_sector} data is {current_time - last_refresh:.0f}s old. Refreshing.")
                return {"command": "sector.info", "data": {"sector_id": int(current_sector)}}

        # If we have everything, we don't need to bootstrap.
        self.state_manager.set("needs_bootstrap", False)
        return None

    # --- Main Planner Entry Point ---

    def get_next_command(self, current_state, current_goal=None):
        """
        Selects the next command, prioritizing the strategic goal.
        """
        
        # --- 0. Bootstrap Check ---
        bootstrap_cmd = self.ensure_minimum_world_state(current_state)
        if bootstrap_cmd:
            if self._is_command_ready(bootstrap_cmd["command"], current_state.get("command_retry_info", {})):
                return bootstrap_cmd
            else:
                logger.debug(f"Bootstrap command '{bootstrap_cmd['command']}' is on cooldown. Waiting.")
                return None
        
        # --- Check for turns remaining ---
        player_turns_remaining = (current_state.get("player_info") or {}).get("player", {}).get("turns_remaining")
        if player_turns_remaining is not None and player_turns_remaining <= 0:
            logger.warning("No turns remaining. Skipping command execution.")
            return None

        # --- 1. Check Invariants (Must-Do Actions) ---

        invariant_command = self._check_invariants(current_state)
        if invariant_command:
            if self._is_command_ready(invariant_command["command"], current_state.get("command_retry_info", {})):
                # Invariant commands are simple and don't need the full payload builder
                return invariant_command
            else:
                logger.debug(f"Invariant command '{invariant_command['command']}' is on cooldown. Waiting.")
                return None

        # --- 1.6 Tactical Override: Mandatory Survey & Sell ---
        # Prioritize local port interactions over long-distance travel goals.
        if self._at_port(current_state):
            # A. Ensure Survey is Complete
            if not self._survey_complete(current_state):
                if self._is_command_ready("trade.quote", current_state.get("command_retry_info", {})):
                    logger.info("Tactical Override: Survey incomplete at port. Overriding goal to quote.")
                    payload = self._build_payload("trade.quote", current_state)
                    if payload:
                         return {"command": "trade.quote", "data": payload}
            
            # B. Sell if possible (Opportunistic Trade)
            if self._can_sell(current_state):
                if self._is_command_ready("trade.sell", current_state.get("command_retry_info", {})):
                    logger.info("Tactical Override: Sell opportunity detected. Overriding goal.")
                    payload = self._build_payload("trade.sell", current_state)
                    if payload:
                        return {"command": "trade.sell", "data": payload}

        # --- 1.5. Stress-safe micro-trade loop (forces progress) ---
        # If we're at a port and have completed survey, always try to trade 1 unit.
        # This prevents the bandit picking non-gameplay cmds (e.g. bank.deposit) and stalling.
        if (not current_goal) and self._at_port(current_state) and self._survey_complete(current_state):
            current_sector = current_state.get("player_location_sector")
            port_id = self._find_port_in_sector(current_state, current_sector)
            if port_id and port_id not in current_state.get("port_trade_blacklist", []):
                cargo = current_state.get("ship_info", {}).get("cargo", []) or []
                used = sum(int(i.get("quantity", 0)) for i in cargo)
                free = max(0, int(current_state.get("ship_info", {}).get("holds", 0)) - used)
                port_info = current_state.get("port_info_by_sector", {}).get(str(current_sector), {})
                comms = [canon_commodity(c.get("commodity")) for c in port_info.get("commodities", []) if c.get("commodity")]
                pc = current_state.get("price_cache", {}).get(str(port_id), {"buy": {}, "sell": {}})

                # Prefer SELL if we have cargo, else BUY if we have space.
                if cargo:
                    item = next((i for i in cargo if int(i.get("quantity", 0)) > 0), None)
                    if item:
                        comm_code = canon_commodity(item["commodity"])
                        if comm_code and pc.get("sell", {}).get(comm_code) is None:
                            return {"command": "trade.quote", "data": {"port_id": port_id, "commodity": comm_code, "quantity": 1}}
                        if comm_code and self._is_command_ready("trade.sell", current_state.get("command_retry_info", {})):
                            payload = self._build_payload("trade.sell", {"port_id": port_id, "items": [{"commodity": comm_code, "quantity": 1}]})
                            if payload is not None: return {"command": "trade.sell", "data": payload}
                if free > 0 and comms:
                    c = min(comms, key=lambda k: pc.get("buy", {}).get(k, float("inf")))
                    if c and pc.get("buy", {}).get(c) is None:
                        return {"command": "trade.quote", "data": {"port_id": port_id, "commodity": c, "quantity": 1}}
                    if c:
                        w = self._ensure_sufficient_credits(current_state, pc["buy"][c])
                        if w: return w
                        if self._is_command_ready("trade.buy", current_state.get("command_retry_info", {})):
                            payload = self._build_payload("trade.buy", {"port_id": port_id, "items": [{"commodity": c, "quantity": 1}]})
                            if payload is not None: return {"command": "trade.buy", "data": payload}


        

        # --- 2. Try to Achieve Strategic Goal (NEW) ---
        if current_goal:
            goal_type, _, goal_target = current_goal.partition(":")
            goal_type = goal_type.strip()

            # Pre-check for credits if the goal is to buy
            if goal_type == "buy":
                port_id = self._find_port_in_sector(current_state, current_state.get("player_location_sector"))
                commodity_to_buy = goal_target.upper()
                buy_price = current_state.get("price_cache", {}).get(str(port_id), {}).get("buy", {}).get(commodity_to_buy)
                
                if buy_price is not None:
                    # Assume we want to buy at least 1 unit to trigger the check
                    required_credits = buy_price 
                    withdraw_command = self._ensure_sufficient_credits(current_state, required_credits)
                    if withdraw_command:
                        logger.info(f"Prioritizing bank withdrawal for buy goal: {current_goal}")
                        return withdraw_command

            goal_command_dict = self._achieve_goal(current_state, current_goal)
            
            if goal_command_dict:
                command_name = goal_command_dict.get("command")

                schema_blacklist = set(self.state_manager.get("schema_blacklist", []))
                if command_name in schema_blacklist:
                    logger.error(f"Goal command '{command_name}' is in the schema_blacklist! Clearing plan.")
                    self.state_manager.set("strategy_plan", [])  # Clear plan
                    return None

                if not self._is_command_ready(command_name, current_state.get("command_retry_info", {})):
                    logger.warning(f"Goal logic wants {command_name}, but it's on cooldown/blacklisted. Returning None and waiting for cooldown.")
                    return None # Do not clear plan, just wait.
                else:
                    logger.info(f"Planner is executing goal: {current_goal} -> {command_name}")
                    final_payload = self._build_payload(command_name, goal_command_dict.get("data", {}))
                    if final_payload is not None:
                        logger.debug(f"Final payload for {command_name}: {json.dumps(final_payload)}") # ADDED THIS LINE
                        return {"command": command_name, "data": final_payload}
                    else:
                        logger.error(f"Goal command {command_name} failed schema validation! Clearing plan.")
                        self.state_manager.set("strategy_plan", []) # Clear plan
                        return None
            else:
                logger.debug(f"Could not find a valid command for goal: {current_goal}. Falling back to stage logic.")
        
        # --- 3. Fallback to Stage-Based Logic ---
        
        self.current_stage = self._select_stage(current_state)
        logger.debug(f"Planner stage: {self.current_stage} (Fallback)")

        action_catalogue = self._build_action_catalogue_by_stage(current_state, self.current_stage)
        
        ready_actions = self._filter_ready_actions(
            action_catalogue,
            current_state.get("command_retry_info", {})
        )

        # Hard stop: never select commands we know are missing schemas / not implemented
        schema_blacklist = set(self.state_manager.get("schema_blacklist", []))
        if schema_blacklist:
            ready_actions = [cmd for cmd in ready_actions if cmd not in schema_blacklist]

        
        if not ready_actions:
            logger.debug("No ready actions for stage '%s'. Waiting for cooldowns.", self.current_stage)
            return None

        # --- 4. Select Action (Bandit) ---
        
        context_key = make_context_key(current_state, self.config) # Pass config dict
        
        command_name = self.bandit_policy.choose_action(ready_actions, context_key)
        logger.info(f"Bandit selected action: {command_name} for stage {self.current_stage}")
        
        # --- 5. Build and Return Payload ---
        data_payload = self._build_payload(command_name, current_state)
        
        if data_payload is not None:
            self.state_manager.set("last_action", command_name)
            self.state_manager.set("last_context_key", context_key)
            self.state_manager.set("last_stage", self.current_stage)
            return {"command": command_name, "data": data_payload}

        logger.error(f"All candidate actions for stage '{self.current_stage}' failed payload generation or validation.")
        return None

    # --- Invariant Checks (Priority 0) ---
    def _check_invariants(self, current_state):
        """Checks for critical missing state and returns a command to fix it."""
        if not current_state.get("player_info"):
            return {"command": "player.my_info", "data": {}}
        if not current_state.get("ship_info"):
            return {"command": "ship.info", "data": {}}
        
        current_sector_id = current_state.get("player_location_sector")
        if current_sector_id is not None and str(current_sector_id) != "None":
            current_sector_data = current_state.get("sector_data", {}).get(str(current_sector_id))
            
            # Check for missing or incomplete current sector data
            # We need to know adjacency AND if there is a port
            if not current_sector_data or "adjacent" not in current_sector_data or "has_port" not in current_sector_data:
                logger.info(f"Invariant check: Incomplete sector_data (adjacent/has_port) for current sector {current_sector_id}. Fetching.")
                return {"command": "sector.info", "data": {"sector_id": int(current_sector_id)}, "is_invariant": True}
            
            # If we are at a port, ensure we have port details (commodities, etc.)
            if current_sector_data.get("has_port"):
                port_info = current_state.get("port_info_by_sector", {}).get(str(current_sector_id))
                if not port_info:
                    retry_info = current_state.get("command_retry_info", {})
                    if self._is_command_ready("trade.port_info", retry_info):
                        logger.info(f"Invariant check: At port in sector {current_sector_id} but missing port info. Fetching.")
                        return {"command": "trade.port_info", "data": {}, "is_invariant": True}
                    else:
                        logger.debug("trade.port_info is on cooldown, skipping invariant.")

            # Check for missing schema for sector.scan.density if we have the scanner
            if (current_state.get("has_density_scanner") and 
                "sector.scan.density" not in self.state_manager.get("schema_blacklist", []) and # Check blacklist
                not self.state_manager.get_schema("sector.scan.density")):
                logger.info("Invariant check: Missing schema for sector.scan.density. Fetching.")
                return {"command": "system.describe_schema", "data": {"name": "sector.scan.density"}, "is_invariant": True}
            
            # Check for density scan if we have the module and adjacent sectors are not fully known
            if current_state.get("has_density_scanner"):
                adjacent_sectors_info = current_state.get("adjacent_sectors_info", [])
                # Check if any adjacent sector is missing density info or has_port info
                needs_scan = False
                for adj_info in adjacent_sectors_info:
                    if "density" not in adj_info or "has_port" not in adj_info:
                        needs_scan = True
                        break
                
                if needs_scan:
                    logger.info("Invariant check: Density scanner available and adjacent sectors need more info. Performing scan.")
                    return {"command": "sector.scan.density", "data": {}, "is_invariant": True}
            
        return None
        
    def _ensure_sufficient_credits(self, current_state, required_amount):
        """
        Checks if player has enough credits. If not, and bank balance is sufficient,
        returns a bank.withdraw command.
        """
        player_credits_str = current_state.get("player_info", {}).get("player", {}).get("credits", "0")
        try:
            player_credits = float(player_credits_str)
        except (ValueError, TypeError):
            player_credits = 0.0

        if player_credits >= required_amount:
            return None # Already have enough credits

        bank_balance = current_state.get("bank_balance", 0)
        needed_from_bank = required_amount - player_credits
        
        if bank_balance >= needed_from_bank:
            logger.info(f"Player needs {needed_from_bank} credits. Withdrawing from bank.")
            # Withdraw a bit more than needed, up to a max, to avoid frequent small withdrawals
            withdraw_amount = min(bank_balance, needed_from_bank + 500, 5000) # Withdraw at least needed, up to 5000
            return {"command": "bank.withdraw", "data": {"amount": int(withdraw_amount)}}
        else:
            logger.warning(f"Player needs {needed_from_bank} credits but only has {bank_balance} in bank. Cannot withdraw.")
            return None

    # --- Stage Selection (Priority 1) ---
    def _select_stage(self, current_state):
        """Determines the bot's current high-level 'stage'."""
        
        current_sector_id = str(current_state.get("player_location_sector"))
        if not current_state.get("sector_data", {}).get(current_sector_id):
            return "explore"

        if self._is_in_dead_end(current_state):
            return "explore"
        
        if self._at_port(current_state) and not self._survey_complete(current_state):
            return "survey"
            
        if self._get_free_holds(current_state) == 0 or self._survey_complete(current_state):
            return "exploit"
        
        return "explore"

    # --- Action Catalogue (Priority 2) ---
    def _build_action_catalogue_by_stage(self, current_state, stage):
        """Returns a prioritized list of actions based on the current stage."""
        
        if stage == "explore":
            return ["sector.info", "move.warp", "bank.balance"]
            
        elif stage == "survey":
            actions = []
            if self._at_port(current_state):
                actions.append("trade.port_info")
            actions.append("trade.quote")
            return actions
            
        elif stage == "exploit":
            actions = []
            if self._at_port(current_state):
                if self._can_sell(current_state):
                    actions.append("trade.sell")
                if self._can_buy(current_state):
                    actions.append("trade.buy")

            # New: Add combat actions if other ships are present
            sector_id = str(current_state.get("player_location_sector"))
            sector_data = current_state.get("sector_data", {}).get(sector_id, {})
            ship_info = current_state.get("ship_info") or {}
            ships = sector_data.get("ships_present") or sector_data.get("ships")
            if ships and len(ships) > 1: # More than just our own ship
                actions.append("combat.attack")
                if ship_info.get("fighters", 0) > 0:
                    actions.append("combat.deploy_fighters")
                if ship_info.get("mines", 0) > 0:
                    actions.append("combat.lay_mines")

            # New: Add planet actions if a planet is present
            if sector_data.get("has_planet"):
                actions.append("planet.info")
                # a 10% chance to try landing on a planet
                if random.random() < 0.1:
                    actions.append("planet.land")

            actions.append("move.warp")
            player_credits_str = current_state.get("player_info", {}).get("player", {}).get("credits", "0")
            # try:
            #     if float(player_credits_str) > 1000:
            #         actions.append("bank.deposit")
            # except (ValueError, TypeError):
            #     pass # Ignore if credits is not a valid number
            actions.append("bank.balance")
            # Allow withdrawing if player credits are low and bank balance is available
            player_credits_str = current_state.get("player_info", {}).get("player", {}).get("credits", "0")
            try:
                player_credits = float(player_credits_str)
            except (ValueError, TypeError):
                player_credits = 0.0
            
            bank_balance = current_state.get("bank_balance", 0)

            if player_credits < 500 and bank_balance > 0: # If player credits are low, consider withdrawing
                actions.append("bank.withdraw")
            
            return actions
        
        return ["bank.balance"] # Default

    # --- Action Filtering ---
    
    def _is_command_ready(self, command_name, retry_info):
        """Checks if a single command is on cooldown or has failed too many times."""
        # TEMP FIX: Always allow trade.quote (needed for surveys)
        if command_name in ["trade.quote"]:
            return True

        info = retry_info.get(command_name, {})
        
        next_retry_time = info.get("next_retry_time", 0)
        if time.time() < next_retry_time:
            logger.debug("Command '%s' is on cooldown.", command_name)
            return False
            
        return True

    def _filter_ready_actions(self, actions, retry_info):
        """Filters the action list, removing un-ready commands."""
        return [cmd for cmd in actions if self._is_command_ready(cmd, retry_info)]

    # --- State Helper Functions ---

    def _is_in_dead_end(self, current_state):
        """Checks if the bot is in a sector with no port and no known adjacent ports."""
        if self._at_port(current_state):
            return False
        
        adjacent_sectors_info = current_state.get("adjacent_sectors_info", [])
        for adj_info in adjacent_sectors_info:
            if adj_info.get("has_port"):
                return False
        
        return True

    def _get_free_holds(self, current_state):
        """Calculates remaining free cargo space robustly."""
        ship_info = current_state.get("ship_info")
        if not ship_info:
            return 0
            
        # FIX: Check multiple keys for capacity
        max_holds = (ship_info.get("holds") or 
                     ship_info.get("hold_capacity") or 
                     ship_info.get("capacity") or 
                     ship_info.get("current_hold_capacity") or 0)
                     
        cargo_list = ship_info.get("cargo", [])
        if isinstance(cargo_list, list):
            current_cargo = sum(item.get('quantity', 0) for item in cargo_list)
        else:
            current_cargo = 0 # Fallback for old dict format or malformed cargo
        logger.debug(f"Free holds calculation: max_holds={max_holds}, cargo_list={cargo_list}, current_cargo={current_cargo}, free_holds={max_holds - current_cargo}")
        return max(0, max_holds - current_cargo)


    def _at_port(self, current_state):
        """Checks if the ship is currently at a port."""
        sector_id = str(current_state.get("player_location_sector"))
        sector_data = current_state.get("sector_data", {}).get(sector_id, {})
        return sector_data.get("has_port", False)

    def _survey_complete(self, current_state):
        """Checks if we have price data for ALL commodities at the current port (Strict)."""
        sector_id = str(current_state.get("player_location_sector"))
        port_id = self._find_port_in_sector(current_state, sector_id)
        if not port_id:
            return False  # No port here; nothing to survey

        port_id_str = str(port_id)
        price_cache = current_state.get("price_cache", {}).get(port_id_str, {})
        
        port_info = current_state.get("port_info_by_sector", {}).get(sector_id, {})
        commodities = port_info.get("commodities", [])
        
        if not commodities:
            return False # We haven't even seen the commodities list yet

        for c in commodities:
            c_code = canon_commodity(c.get("commodity"))
            if not c_code: continue
            
            # Check if we have EITHER a buy OR sell price (quotes usually give both)
            # Use 'is not None' because 0.0 is a valid (though unlikely) float price.
            has_buy = price_cache.get("buy", {}).get(c_code) is not None
            has_sell = price_cache.get("sell", {}).get(c_code) is not None
            
            if not (has_buy and has_sell): # Requirement: MUST have both prices for a complete survey
                return False # Found a commodity with incomplete price data

        return True # All commodities have both prices

    def _can_sell(self, current_state):
        """Checks if there is any profitable commodity to sell."""
        return self._get_best_commodity_to_sell(current_state) is not None

    def _can_buy(self, current_state):
        """Checks if we have free holds and are at a port that sells something."""
        if not self._at_port(current_state):
            logger.debug("Cannot buy: Not at a port.")
            return False

        if self._get_free_holds(current_state) <= 0:
            logger.debug("Cannot buy: No free holds.")
            return False

        port_id = self._find_port_in_sector(current_state, current_state.get("player_location_sector"))
        if not port_id:
            logger.debug("Cannot buy: No port ID found for current sector.")
            return False

        port_id_str = str(port_id)
        port_buy_prices = current_state.get("price_cache", {}).get(port_id_str, {}).get("buy", {})

        # New rule: can buy if we have any non-None price entries at all
        can_buy_any = any(price is not None for price in port_buy_prices.values())
        if not can_buy_any:
            logger.debug("Cannot buy: No known buy prices at this port.")
        return can_buy_any

    def _build_payload(self, command_name: str, context: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        logger.debug(f"[_build_payload] command_name: {command_name}, context: {context}")
        """
        Dynamically builds and validates a payload for a given command using its JSON schema.
        'context' is current_state for fallback logic, or goal data for goal logic.
        """
        if command_name == "bank.balance":
            return {}
        if command_name == "sector.scan.density":
            return {}
        if command_name == "move.pathfind":
            return context

        schema = self.state_manager.get_schema(command_name)
        if not schema:
            # For commands that are known to work with empty payloads without a schema
            if command_name in ["bank.balance", "sector.scan.density"]:
                logger.debug(f"No schema found for command '{command_name}'. Assuming empty payload.")
                return {}
            if command_name == "move.pathfind":
                return context
            
            logger.warning(f"No schema found for command '{command_name}'. Adding to schema blacklist and returning None.")
            self.state_manager.add_to_schema_blacklist(command_name)
            return None

        logger.debug(f"Using schema for {command_name}: {json.dumps(schema)}")

        properties = schema.get("properties", {})
        required_fields = schema.get("required", [])
        logger.debug(f"[_build_payload] properties: {properties}")
        logger.debug(f"[_build_payload] required_fields: {required_fields}")
        payload = {}

        # --- FIX: Use a consistent 'current_state' context ---
        # The 'context' passed from goal logic is just the 'data' part.
        # We need the full state for most payload decisions.
        current_state = self.state_manager.get_all()

        try:
            # Special handling for schemas where the payload is nested under 'data'
            if 'data' in properties and isinstance(properties['data'], dict):
                nested_properties = properties['data'].get('properties', {})
                nested_required = properties['data'].get('required', [])
                nested_payload = {}
                
                for field in nested_properties.keys():
                    logger.debug(f"[_build_payload] processing nested field: {field}")
                    if field in context:
                        nested_payload[field] = context[field]
                        logger.debug(f"[_build_payload] got nested field {field} from context")
                        continue
                    
                    if field in nested_required:
                        value = self._generate_required_field(field, command_name, current_state)
                        if value is not None:
                            nested_payload[field] = value
                            logger.debug(f"[_build_payload] generated nested field {field}")
                        else:
                            logger.error(f"Could not generate required field '{field}' for command '{command_name}'.")
                            return None
                payload['data'] = nested_payload
            else: # Flat payload
                for field in properties.keys():
                    logger.debug(f"[_build_payload] processing flat field: {field}")
                    if field in context:
                        payload[field] = context[field]
                        logger.debug(f"[_build_payload] got flat field {field} from context")
                        continue

                    if field in required_fields:
                        value = self._generate_required_field(field, command_name, current_state)
                        if value is not None:
                            payload[field] = value
                            logger.debug(f"[_build_payload] generated flat field {field}")
                        else:
                            logger.error(f"Could not generate required field '{field}' for command '{command_name}'.")
                            return None
            
            logger.debug(f"[_build_payload] final payload before validation: {payload}")
            
            # --- MANDATORY IDEMPOTENCY KEY ---
            if command_name in ("trade.buy", "trade.sell"):
                if "data" in properties: # Nested payload
                    if "data" not in payload: payload["data"] = {}
                    if "idempotency_key" not in payload["data"]:
                        payload["data"]["idempotency_key"] = generate_idempotency_key()
                else: # Flat payload
                     if "idempotency_key" not in payload:
                        payload["idempotency_key"] = generate_idempotency_key()

            logger.debug(f"Validating command: {command_name}")
            logger.debug(f"Payload for validation: {json.dumps(payload)}")

            # Validate the final payload against the schema
            validate(instance=payload, schema=schema)

            logger.debug(f"Successfully built and validated payload for {command_name}: {payload}")
            
            # --- FIX: Return the correct part of the payload ---
            if 'data' in payload:
                return payload['data']
            return payload

        except ValidationError as e:
            logger.error(f"Payload validation failed for {command_name}: {e.message}", exc_info=True)
            # --- Bug Reporting ---
            self.bug_reporter.triage_schema_validation_error(
                command_name,
                payload,
                schema,
                e,
                current_state
            )
            return None
        except Exception as e:
            logger.error(f"Unexpected error building payload for {command_name}: {e}", exc_info=True)
            return None

    def _generate_required_field(self, field_name: str, command_name: str, current_state: Dict[str, Any]) -> Any:
        ship_id = (current_state.get("ship_info") or {}).get("id")
        current_sector = current_state.get("player_location_sector")

        if field_name == "to_sector_id":
            target = self._get_next_warp_target(current_state)
            if target is None:
                logger.error(f"Could not determine valid to_sector_id for command {command_name}.")
                return None
            return target
        if field_name == "ship_id":
            return ship_id
        if field_name == "sector_id":
            return current_sector
        if field_name == "port_id":
            return self._find_port_in_sector(current_state, current_sector)
        if field_name == "idempotency_key":
            # Just generate it; reuse logic is handled if the command is persisted (which we don't do for generated fields, 
            # but we force it in _build_payload for trade commands anyway)
            return generate_idempotency_key()

        if command_name == "combat.deploy_fighters":
            if field_name == "fighters":
                available_fighters = (current_state.get("ship_info") or {}).get("fighters", 0)
                if available_fighters > 0:
                    return random.randint(1, available_fighters)
                else:
                    return 0
            if field_name == "offense":
                # TODO: get this from ship info
                return 50 # Default value as per schema
        
        if command_name == "combat.lay_mines":
            if field_name == "quantity":
                available_mines = (current_state.get("ship_info") or {}).get("mines", 0)
                if available_mines > 0:
                    return random.randint(1, available_mines)
                else:
                    return 0
            if field_name == "type":
                return "ARMID" # TODO: Fuzz this later



        
        if command_name == "combat.attack":
            if field_name == "target_type":
                return "ship"
            if field_name == "stance":
                return "aggressive" # TODO: Fuzz this later
            if field_name == "target_id":
                sector_data = current_state.get("sector_data", {}).get(str(current_sector), {})
                ships = sector_data.get("ships_present") or sector_data.get("ships", [])
                other_ships = [s for s in ships if s.get("id") != ship_id]
                if other_ships:
                    target_ship = random.choice(other_ships)
                    logger.info(f"Selected target ship: {target_ship.get('id')}")
                    return target_ship.get("id")
                else:
                    logger.warning("No other ships in sector to attack.")
                    return None

                
        if field_name == "commodity":
            if command_name == "trade.quote":
                # Smart selection: pick a commodity we don't have a price for yet
                current_sector = current_state.get("player_location_sector")
                port_id = self._find_port_in_sector(current_state, current_sector)
                if port_id:
                    port_id_str = str(port_id)
                    price_cache = current_state.get("price_cache", {}).get(port_id_str, {})
                    buy_cache = price_cache.get("buy", {})
                    sell_cache = price_cache.get("sell", {})
                    
                    # Try to get actual port commodities, fallback to defaults
                    port_info = current_state.get("port_info_by_sector", {}).get(str(current_sector), {})
                    if port_info and port_info.get("commodities"):
                         candidates = [canon_commodity(c.get("commodity")) for c in port_info.get("commodities", [])]
                         candidates = [c for c in candidates if c] # filter Nones
                    else:
                         candidates = ["ORE", "ORG", "EQU"]
                    
                    for c in candidates:
                        # Smart selection: quote if missing EITHER buy OR sell price
                        # Use 'is None' to correctly handle float prices
                        if buy_cache.get(c) is None or sell_cache.get(c) is None:
                            logger.info(f"Generated quote commodity candidate: {c}")
                            return c
                
                # Fallback: Random
                all_commodities = ["ORE", "EQU", "ORG"]
                return random.choice(all_commodities)
        
        if field_name == "quantity":
            if command_name == "trade.quote":
                return 1
        
        if field_name == "account":
            # For now, always use account 0 (player's personal account)
            return 0

        if field_name == "amount":
            if command_name == "bank.deposit":
                player_credits_str = current_state.get("player_info", {}).get("player", {}).get("credits", "0")
                try:
                    credits = float(player_credits_str)
                    # Only attempt to deposit if credits are strictly greater than 1000
                    if credits > 1000:
                        return int(credits - 1000)
                    else:
                        return None # Not enough credits to make a meaningful deposit
                except (ValueError, TypeError):
                    return None


        if field_name == "items":
            if command_name == "trade.buy":
                commodity = canon_commodity(self._get_cheapest_commodity_to_buy(current_state))
                if not commodity: return None
                
                free_holds = self._get_free_holds(current_state)
                if free_holds <= 0:
                    logger.warning("No free holds to buy. Cannot generate buy command.")
                    return None

                port_id = str(self._find_port_in_sector(current_state, current_sector))
                buy_price = current_state.get("price_cache", {}).get(port_id, {}).get("buy", {}).get(commodity)
                
                if buy_price is None:
                    logger.warning(f"No buy price found for {commodity}. Cannot generate buy command.")
                    return None

                player_credits_str = current_state.get("player_info", {}).get("player", {}).get("credits", "0")
                try:
                    player_credits = float(player_credits_str)
                except (ValueError, TypeError):
                    player_credits = 0.0
                
                max_affordable_quantity = int(player_credits / buy_price) if buy_price > 0 else 0
                
                max_quantity = min(free_holds, max_affordable_quantity)

                # FUZZING LOGIC
                quantity = self._get_fuzzed_buy_quantity(max_quantity)

                if quantity > 0:
                    return [{"commodity": commodity, "quantity": quantity}]
                elif max_quantity > 0:
                     logger.info(f"Fuzzer decided to buy {quantity}, but max is {max_quantity}. Returning a non-zero quantity to avoid wasting a turn.")
                     return [{"commodity": commodity, "quantity": 1}] # Buy at least one
                else: 
                    logger.warning(f"Not enough credits ({player_credits}) or free holds ({free_holds}) to buy {commodity} at {buy_price}. Cannot generate buy command.")
                    return None 
            if command_name == "trade.sell":
                commodity_to_sell = canon_commodity(self._get_best_commodity_to_sell(current_state))
                if not commodity_to_sell: return None

                cargo_list = (current_state.get("ship_info") or {}).get("cargo", [])
                total_quantity = 0
                for item in cargo_list:
                    if canon_commodity(item.get("commodity")) == commodity_to_sell:
                        total_quantity += item.get("quantity", 0)

                if total_quantity > 0:
                    return [{"commodity": commodity_to_sell, "quantity": int(total_quantity)}]
                else:
                    return None

        logger.warning(f"No generation logic for required field '{field_name}' in command '{command_name}'.")
        return None

    # --- Payload Helper Functions ---

    def _get_fuzzed_buy_quantity(self, max_quantity: int) -> int:
        """
        Generates a valid fuzzed quantity for buying/selling within reasonable bounds.
        No longer generates quantities > max_quantity or negative for trade.buy/sell to avoid
        immediate refusal due to basic game rules (e.g., insufficient credits/holds).
        """
        if max_quantity <= 0:
            return 0
        
        roll = random.random()
        if roll < 0.10: # 10% chance to try to buy 1 unit
            return 1
        elif roll < 0.20: # 10% chance to try to buy exactly max_quantity
            return max_quantity
        else: # 80% chance to buy a random amount between 1 and max_quantity
            return random.randint(1, max_quantity)

    def _get_next_warp_target(self, current_state):
        current_sector = current_state.get("player_location_sector")
        sector_data_map = current_state.get("sector_data", {})
        current_sector_data = sector_data_map.get(str(current_sector))
        if not current_sector_data: return None
            
        adjacent_sectors = current_sector_data.get("adjacent", [])
        if not adjacent_sectors: return None

        warp_blacklist = self.state_manager.get("warp_blacklist", [])
        
        # Extract sector IDs from adjacent list (server returns [{"to_sector": N}, ...])
        adjacent_ids = []
        for adj in adjacent_sectors:
            if isinstance(adj, dict) and "to_sector" in adj:
                adjacent_ids.append(adj["to_sector"])
            elif isinstance(adj, int):
                adjacent_ids.append(adj)
        
        # Strictly exclude current_sector to prevent self-warps
        possible_targets = [s for s in adjacent_ids if s != current_sector and s not in warp_blacklist]
        
        logger.debug(f"Warp candidates from {current_sector}: {possible_targets} (Raw adj: {adjacent_sectors})")

        if not possible_targets:
            logger.warning(f"No valid warp targets available from {current_sector}. Candidates empty.")
            return None

        # New exploration logic using universe_map
        universe_map = current_state.get("universe_map", {})
        unexplored_adjacent = [s for s in possible_targets if not universe_map.get(str(s), {}).get('is_explored')]

        if unexplored_adjacent:
            logger.info(f"Prioritizing unexplored adjacent sectors. Choosing from: {unexplored_adjacent}")
            return random.choice(unexplored_adjacent)

        # If no adjacent sectors are unexplored, find the nearest unexplored sector in the KNOWN universe
        # ... (keep existing Expanding Wave logic) ...
        
        # Fallback to original logic if all adjacent sectors are explored
        recent = set(current_state.get("recent_sectors", [])[-5:])
        non_recent_targets = [s for s in possible_targets if s not in recent]
        
        if non_recent_targets:
            return random.choice(non_recent_targets)
        
        return random.choice(possible_targets)

    def _get_best_commodity_to_sell(self, current_state):
        """Finds the most profitable commodity to sell."""
        ship_info = current_state.get("ship_info")
        if not ship_info:
            return None
            
        cargo_list = ship_info.get("cargo", [])
        if not isinstance(cargo_list, list):
            return None

        port_id = str(self._find_port_in_sector(current_state, current_state.get("player_location_sector")))
        port_sell_prices = current_state.get("price_cache", {}).get(port_id, {}).get("sell", {})

        best_commodity = None
        highest_profit_margin = -999999 # Allow for negative profit (loss)
        best_sell_price = 0
        
        # Calculate holds full ratio
        capacity = max(1, current_state.get("ship_cargo_capacity", 1))
        current_vol = current_state.get("ship_current_cargo_volume", 0)
        holds_full_ratio = current_vol / capacity
        allow_loss = holds_full_ratio > 0.8

        for item in cargo_list:
            commodity_raw = item.get("commodity")
            commodity = canon_commodity(commodity_raw)
            if not commodity:
                continue

            quantity = item.get("quantity", 0)
            purchase_price = item.get("purchase_price")

            if quantity > 0:
                sell_price = port_sell_prices.get(commodity)
                if sell_price is not None and sell_price > 0:
                    # If we know the purchase price, calculate profit margin
                    if purchase_price is not None:
                        profit_margin = sell_price - purchase_price
                        
                        # Only allow loss if holds are full
                        if profit_margin < 0 and not allow_loss:
                            continue

                        if profit_margin >= highest_profit_margin:
                            highest_profit_margin = profit_margin
                            best_commodity = commodity
                            best_sell_price = sell_price
                    # If we don't know purchase price, just sell at the highest price
                    elif sell_price > best_sell_price:
                        best_sell_price = sell_price
                        best_commodity = commodity
                        highest_profit_margin = 0
        
        if best_commodity:
            if highest_profit_margin > 0:
                logger.info(f"Found profitable trade: sell {best_commodity} for {highest_profit_margin} profit per unit.")
            elif highest_profit_margin < 0 and highest_profit_margin > -999999:
                logger.warning(f"Holds full ({holds_full_ratio:.1%}). Selling {best_commodity} at a LOSS of {abs(highest_profit_margin)} per unit to clear space.")
            else:
                logger.info(f"Found commodity to sell: {best_commodity} at price {best_sell_price} (purchase price unknown).")
        
        return best_commodity

    def _get_cheapest_commodity_to_buy(self, current_state):
        port_id = str(self._find_port_in_sector(current_state, current_state.get("player_location_sector")))
        port_buy_prices = current_state.get("price_cache", {}).get(port_id, {}).get("buy", {})
        
        best_commodity = None
        lowest_price = float('inf')
        for commodity, price in port_buy_prices.items():
            if price and price < lowest_price: # Check for non-None price
                lowest_price = price
                best_commodity = commodity
        return best_commodity
