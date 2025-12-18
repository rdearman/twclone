import logging
import time
import random
import json
import uuid
from jsonschema import validate, ValidationError
from bandit_policy import BanditPolicy, make_context_key # Import the unified context key function
from typing import Optional, Any, Dict

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
        self.current_stage = self.state_manager.get("stage", "start") # start, explore, survey, exploit

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
            return ports[0].get("id") # Return first port ID

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
                adjacent_sectors = sector_data.get("adjacent", [])

                # 0. Missing Adjacency Data -> Fetch it
                if not adjacent_sectors:
                    logger.info(f"Goto goal but no adjacency data for sector {current_sector}. Requesting sector.info.")
                    return {"command": "sector.info", "data": {"sector_id": int(current_sector)}}

                # 1. If adjacent, just warp
                if target_sector in adjacent_sectors:
                    if self._is_command_ready("move.warp", current_state.get("command_retry_info", {})):
                        return {"command": "move.warp", "data": {"to_sector_id": target_sector}}
                    else:
                        logger.warning(f"Goal 'goto: {target_sector}' wants move.warp, but it's on cooldown/blacklisted.")
                        return None

                # 2. Try Local Pathfinding (BFS on known sectors)
                local_path = self.state_manager.find_path(current_sector, target_sector)
                if local_path and len(local_path) > 1:
                    next_hop = local_path[1]
                    logger.info(f"Local path found to {target_sector}: {local_path}. Next hop: {next_hop}")
                    if self._is_command_ready("move.warp", current_state.get("command_retry_info", {})):
                        return {"command": "move.warp", "data": {"to_sector_id": next_hop}}
                    else:
                        logger.warning(f"Warp to {next_hop} on cooldown.")
                        return None

                # 3. Try Server Pathfinding (Cached)
                server_path = current_state.get("current_path", [])
                if (server_path and len(server_path) > 1 and 
                    server_path[0] == current_sector and server_path[-1] == target_sector):
                    
                    next_hop = server_path[1]
                    logger.info(f"Using cached server path to {target_sector}. Next hop: {next_hop}")
                    if self._is_command_ready("move.warp", current_state.get("command_retry_info", {})):
                        return {"command": "move.warp", "data": {"to_sector_id": next_hop}}
                    else:
                        return None

                # 4. Request Server Path (if acceptable)
                # Only ask if we haven't asked recently
                if self._is_command_ready("move.pathfind", current_state.get("command_retry_info", {})):
                    logger.info(f"Requesting server path to {target_sector}...")
                    return {"command": "move.pathfind", "data": {"to_sector_id": target_sector}}

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
                port_id = self._find_port_in_sector(current_state, current_sector)
                if port_id in current_state.get("port_trade_blacklist", []):
                    logger.warning(f"Goal 'sell' failed: port {port_id} is in trade blacklist.")
                    return None
                if not port_id:
                    logger.warning("Goal 'sell' failed: not at a port. Falling back.")
                    return None 
                
                commodity_to_sell = goal_target.upper() 

                # --- NEW: Filter illegal commodities early ---
                LEGAL_COMMODITIES = {"ORE", "ORG", "EQU", "COLONISTS"} # Define legal commodities
                if commodity_to_sell not in LEGAL_COMMODITIES:
                    logger.warning(f"Rejecting sell goal for illegal commodity '{commodity_to_sell}'.")
                    return None
                # ---------------------------------------------
                
                # Validation: Check if port actually trades this commodity
                #port_info = current_state.get("port_info_by_sector", {}).get(str(current_sector), {})
                #traded_commodities = [c.get("symbol") for c in port_info.get("commodities", [])]
                current_sector = current_state.get("player_location_sector")  # <-- add this line
                port_info = current_state.get("port_info_by_sector", {}).get(str(current_sector), {})
                traded_commodities = [c.get("commodity") for c in port_info.get("commodities", [])]  # <-- symbol -> commodity

                if traded_commodities and commodity_to_sell not in traded_commodities:
                     logger.warning(f"Goal 'sell' failed: Port {port_id} does not trade {commodity_to_sell}.")
                     return None

                port_id_str = str(port_id)
                sell_price = current_state.get("price_cache", {}).get(port_id_str, {}).get("sell", {}).get(commodity_to_sell)

                if sell_price is None:
                    logger.info(f"Sell price for {commodity_to_sell} missing. Requesting trade.quote first.")
                    return {"command": "trade.quote", "data": {"port_id": port_id, "commodity": commodity_to_sell, "quantity": 1}}

                cargo = current_state.get("ship_info", {}).get("cargo", {})
                # Cargo is a list of dicts: [{"commodity": "ORE", "quantity": 10, "purchase_price": 50}]
                # Need to iterate to find quantity
                quantity = sum(item.get("quantity", 0) for item in cargo if item.get("commodity") == commodity_to_sell)
                
                if not quantity:
                    logger.warning(f"Goal 'sell' failed: no '{commodity_to_sell}' in cargo.")
                    return None 

                return {"command": "trade.sell", "data": {
                    "port_id": port_id,
                    "items": [{"commodity": commodity_to_sell, "quantity": int(quantity)}]
                }}

            elif goal_type == "buy":
                port_id = self._find_port_in_sector(current_state, current_state.get("player_location_sector"))
                if port_id in current_state.get("port_trade_blacklist", []):
                    logger.warning(f"Goal 'buy' failed: port {port_id} is in trade blacklist.")
                    return None
                if not port_id:
                    logger.warning("Goal 'buy' failed: not at a port. Falling back.")
                    return None 
                
                commodity_to_buy = goal_target.upper() 

                # --- NEW: Filter illegal commodities early ---
                LEGAL_COMMODITIES = {"ORE", "ORG", "EQU", "COLONISTS"} # Define legal commodities
                if commodity_to_buy not in LEGAL_COMMODITIES:
                    logger.warning(f"Rejecting buy goal for illegal commodity '{commodity_to_buy}'.")
                    return None
                # ---------------------------------------------
                
                # Validation: Check if port actually trades this commodity
                #port_info = current_state.get("port_info_by_sector", {}).get(str(current_sector), {})
                #traded_commodities = [c.get("symbol") for c in port_info.get("commodities", [])]
                current_sector = current_state.get("player_location_sector")
                port_info = current_state.get("port_info_by_sector", {}).get(str(current_sector), {})
                traded_commodities = [c.get("commodity") for c in port_info.get("commodities", [])]
                
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
                # port_commodities = [c.get("symbol") for c in port_info.get("commodities", [])]
                port_commodities = [c.get("commodity") for c in port_info.get("commodities", [])]
                
                price_cache = current_state.get("price_cache", {}).get(str(port_id), {"buy": {}, "sell": {}})

                for commodity_symbol in port_commodities:
                    if not (price_cache["buy"].get(commodity_symbol) is not None and
                            price_cache["sell"].get(commodity_symbol) is not None):
                        logger.info(f"Calling trade.quote for unquoted commodity: {commodity_symbol}")
                        return {"command": "trade.quote", "data": {"port_id": port_id, "commodity": commodity_symbol, "quantity": 1}}
                
                # If we reach here, all commodities are quoted, but _survey_complete returned False
                # This should ideally not happen if _survey_complete is correct.
                logger.warning("Survey: port logic reached end without quoting all commodities, but none found unquoted.")
                return {"command": "trade.port_info", "data": {}} # Fallback to ensure port info is fresh
            
            elif goal_type == "scan" and goal_target == "density":
                return {"command": "sector.scan.density", "data": {}}

        except Exception as e:
            logger.error(f"Error in _achieve_goal for '{goal_str}': {e}", exc_info=True)
        
        return None # Fallback

    # --- Bootstrap Logic (Priority 0) ---
    def ensure_minimum_world_state(self, current_state):
        """
        If the bot has just started or world caches are empty,
        return a command to rebuild the minimal world state.
        """
        if not self.state_manager.get("needs_bootstrap"):
            return None

        # 1. Player Info
        if not current_state.get("player_info"):
            logger.info("Bootstrap: Fetching player info.")
            return {"command": "player.my_info", "data": {}}

        # 2. Ship Info
        ship_info = current_state.get("ship_info")
        if not ship_info or not ship_info.get("id"):
            logger.info("Bootstrap: Fetching ship info.")
            return {"command": "ship.info", "data": {}}

        # 3. Sector Info & Adjacency
        current_sector = current_state.get("player_location_sector")
        if current_sector is None:
            # Fallback to player info if location not yet set in root state
            p_info = current_state.get("player_info", {}).get("player", {})
            current_sector = p_info.get("sector")
        
        if current_sector:
            sector_data = current_state.get("sector_data", {}).get(str(current_sector))
            # If we don't have sector data, OR we don't have adjacency info
            if not sector_data or not sector_data.get("adjacent"):
                logger.info(f"Bootstrap: Fetching sector info for {current_sector} to build map.")
                return {"command": "sector.info", "data": {"sector_id": int(current_sector)}}
            
            # Optional: If we have sector info but no adjacency (rare but possible), maybe scan?
            # For now, sector.info should be enough for adjacency.

        # If we reached here, we have the basics.
        logger.info("Bootstrap complete: World state is ready.")
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
            return bootstrap_cmd
        
        # --- Check for turns remaining ---
        player_turns_remaining = (current_state.get("player_info") or {}).get("player", {}).get("turns_remaining")
        if player_turns_remaining is not None and player_turns_remaining <= 0:
            logger.warning("No turns remaining. Skipping command execution.")
            return None

        # --- 1. Check Invariants (Must-Do Actions) ---

        invariant_command = self._check_invariants(current_state)
        if invariant_command:
            # Invariant commands are simple and don't need the full payload builder
            return invariant_command

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
                comms = [c.get("commodity") for c in port_info.get("commodities", []) if c.get("commodity")]
                pc = current_state.get("price_cache", {}).get(str(port_id), {"buy": {}, "sell": {}})

                # Prefer SELL if we have cargo, else BUY if we have space.
                if cargo:
                    item = next((i for i in cargo if int(i.get("quantity", 0)) > 0), None)
                    if item and pc.get("sell", {}).get(item["commodity"]) is None:
                        return {"command": "trade.quote", "data": {"port_id": port_id, "commodity": item["commodity"], "quantity": 1}}
                    if item and self._is_command_ready("trade.sell", current_state.get("command_retry_info", {})):
                        payload = self._build_payload("trade.sell", {"port_id": port_id, "items": [{"commodity": item["commodity"], "quantity": 1}]})
                        if payload is not None: return {"command": "trade.sell", "data": payload}
                if free > 0 and comms:
                    c = min(comms, key=lambda k: pc.get("buy", {}).get(k, float("inf")))
                    if pc.get("buy", {}).get(c) is None:
                        return {"command": "trade.quote", "data": {"port_id": port_id, "commodity": c, "quantity": 1}}
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
                    logger.info(f"Invariant check: At port in sector {current_sector_id} but missing port info. Fetching.")
                    return {"command": "trade.port_info", "data": {}, "is_invariant": True}

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
            if sector_data.get("ships") and len(sector_data.get("ships")) > 1: # More than just our own ship
                actions.append("combat.attack")
                if current_state.get("ship_info", {}).get("fighters", 0) > 0:
                    actions.append("combat.deploy_fighters")
                if current_state.get("ship_info", {}).get("mines", 0) > 0:
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
        # TEMP FIX: Always allow move.warp
        if command_name == "move.warp":
            return True

        info = retry_info.get(command_name, {})
        failures = info.get("failures", 0)
        
        if failures >= self.state_manager.config.get("max_retries_per_command", 3):
            logger.debug("Command '%s' is blacklisted (failed %d times).", command_name, failures)
            return False
            
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
        """Calculates remaining free cargo space."""
        ship_info = current_state.get("ship_info", {})
        max_holds = ship_info.get("holds", 0)
        cargo_list = ship_info.get("cargo", [])
        if isinstance(cargo_list, list):
            current_cargo = sum(item.get('quantity', 0) for item in cargo_list)
        else:
            current_cargo = 0 # Fallback for old dict format or malformed cargo
        logger.debug(f"Free holds calculation: max_holds={max_holds}, cargo_list={cargo_list}, current_cargo={current_cargo}, free_holds={max_holds - current_cargo}")
        return max_holds - current_cargo


    def _at_port(self, current_state):
        """Checks if the ship is currently at a port."""
        sector_id = str(current_state.get("player_location_sector"))
        sector_data = current_state.get("sector_data", {}).get(sector_id, {})
        return sector_data.get("has_port", False)

    def _survey_complete(self, current_state):
        """Checks if we have price data for any commodity at the current port."""
        sector_id = str(current_state.get("player_location_sector"))
        port_id = self._find_port_in_sector(current_state, sector_id)
        if not port_id:
            return False  # No port here; nothing to survey

        port_id_str = str(port_id)
        price_cache = current_state.get("price_cache", {}).get(port_id_str, {})

        # New simple rule: if we know any buy OR sell prices for any commodity at this port,
        # consider the survey "good enough" to move to exploit.
        has_any_buy_price = bool(price_cache.get("buy") and any(v is not None for v in price_cache["buy"].values()))
        has_any_sell_price = bool(price_cache.get("sell") and any(v is not None for v in price_cache["sell"].values()))

        return has_any_buy_price or has_any_sell_price

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

        schema = self.state_manager.get_schema(command_name)
        if not schema:
            # For commands that are known to work with empty payloads without a schema
            if command_name in ["bank.balance", "sector.scan.density"]:
                logger.debug(f"No schema found for command '{command_name}'. Assuming empty payload.")
                return {}
            
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
            full_command_for_validation = {"command": command_name, **payload}

            logger.debug(f"Validating command: {command_name}")
            logger.debug(f"Payload for validation: {json.dumps(full_command_for_validation)}")

            # Validate the final payload against the full schema
            validate(instance=full_command_for_validation, schema=schema)
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
        ship_id = current_state.get("ship_info", {}).get("id")
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
            if command_name == "trade.buy" or command_name == "trade.sell": # Apply to both buy and sell
                return str(uuid.uuid4())
            # For other commands, we might have specific idempotency key generation or it might be handled elsewhere
            return str(uuid.uuid4()) # Fallback for any idempotency_key

        if command_name == "combat.deploy_fighters":
            if field_name == "fighters":
                available_fighters = current_state.get("ship_info", {}).get("fighters", 0)
                if available_fighters > 0:
                    return random.randint(1, available_fighters)
                else:
                    return 0
            if field_name == "offense":
                # TODO: get this from ship info
                return 50 # Default value as per schema
        
        if command_name == "combat.lay_mines":
            if field_name == "quantity":
                available_mines = current_state.get("ship_info", {}).get("mines", 0)
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
                other_ships = [s for s in sector_data.get("ships", []) if s.get("id") != ship_id]
                if other_ships:
                    target_ship = random.choice(other_ships)
                    logger.info(f"Selected target ship: {target_ship.get('id')}")
                    return target_ship.get("id")
                else:
                    logger.warning("No other ships in sector to attack.")
                    return None

                
        if field_name == "commodity":
            if command_name == "trade.quote":
                # Cycle through commodities to get quotes for all
                all_commodities = ["ORE", "EQU", "ORG"]
                # A simple way to cycle: use the current turn number
                turns = current_state.get("player_info", {}).get("player", {}).get("turns_remaining", 0)
                if turns is None:
                    turns = 0
                return all_commodities[turns % len(all_commodities)]
        
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
                commodity = self._get_cheapest_commodity_to_buy(current_state)
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
                commodity_to_sell = self._get_best_commodity_to_sell(current_state)
                if not commodity_to_sell: return None

                cargo_list = current_state.get("ship_info", {}).get("cargo", [])
                total_quantity = 0
                for item in cargo_list:
                    if item.get("commodity") == commodity_to_sell:
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
        
        # Strictly exclude current_sector to prevent self-warps
        possible_targets = [s for s in adjacent_sectors if s != current_sector and s not in warp_blacklist]
        
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
        cargo_list = current_state.get("ship_info", {}).get("cargo", [])
        if not isinstance(cargo_list, list):
            return None

        port_id = str(self._find_port_in_sector(current_state, current_state.get("player_location_sector")))
        port_sell_prices = current_state.get("price_cache", {}).get(port_id, {}).get("sell", {})

        best_commodity = None
        highest_profit_margin = 0

        for item in cargo_list:
            commodity = item.get("commodity")
            quantity = item.get("quantity", 0)
            purchase_price = item.get("purchase_price")

            if quantity > 0 and purchase_price is not None:
                sell_price = port_sell_prices.get(commodity)
                if sell_price is not None and sell_price > purchase_price:
                    profit_margin = sell_price - purchase_price
                    if profit_margin > highest_profit_margin:
                        highest_profit_margin = profit_margin
                        best_commodity = commodity
        
        if best_commodity:
            logger.info(f"Found profitable trade: sell {best_commodity} for {highest_profit_margin} profit per unit.")
        
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
