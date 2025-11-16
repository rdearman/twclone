import time
import logging
import random
import json
from bandit_policy import BanditPolicy, make_context_key # Import the unified context key function

logger = logging.getLogger(__name__)

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
            goal_type, _, goal_target = goal_str.partition(":")
            goal_target = goal_target.strip() # Keep case for commodities
            ship_id = current_state.get("ship_info", {}).get("id")
            current_sector = current_state.get("player_location_sector")

            if goal_type == "goto":
                target_sector = int(goal_target)
                
                # --- FIX: Check if we are already there. ---
                if current_sector == target_sector:
                    logger.debug("Goal 'goto' already complete (at target).")
                    return None # Let main.py's `is_goal_complete` handle popping this.
                
                # Simple 1-hop pathfinder.
                sector_data = current_state.get("sector_data", {}).get(str(current_sector), {})
                if target_sector in sector_data.get("adjacent", []):
                    return {"command": "move.warp", "data": self._warp_payload(current_state, target_sector)}
                else:
                    # Can't reach it. Fallback to stage-based explore logic.
                    logger.warning(f"Cannot 'goto' {target_sector}. Not adjacent. Falling back to explore.")
                    return None

            elif goal_type == "sell":
                port_id = self._find_port_in_sector(current_state, current_sector)
                if not port_id:
                    logger.warning("Goal 'sell' failed: not at a port. Falling back.")
                    return None # Fallback
                
                commodity_to_sell = goal_target.upper() # e.g. "ORGANICS"
                cargo = current_state.get("ship_info", {}).get("cargo", {})
                quantity = cargo.get(commodity_to_sell.lower()) # cargo keys are lowercase
                
                if not quantity:
                    logger.warning(f"Goal 'sell' failed: no '{commodity_to_sell}' in cargo.")
                    return None # Goal is "complete" (can't sell what we don't have)

                # --- CRITICAL FIX: Use the correct FLAT payload for trade.sell ---
                return {"command": "trade.sell", "data": {
                    "port_id": port_id,
                    "commodity": commodity_to_sell,
                    "quantity": int(quantity) # Ensure it's an int
                }}

            elif goal_type == "buy":
                port_id = self._find_port_in_sector(current_state, current_sector)
                if not port_id:
                    logger.warning("Goal 'buy' failed: not at a port. Falling back.")
                    return None # Fallback
                
                commodity_to_buy = goal_target.upper() # e.g. "ORE"
                
                # --- FIX: Check free holds ---
                free_holds = self._get_free_holds(current_state)
                if free_holds <= 0:
                    logger.warning(f"Goal 'buy' failed: no free holds to buy '{commodity_to_buy}'.")
                    return None
                
                # Buy 10 or max free, whichever is less
                quantity = min(10, free_holds) 

                # Build payload directly (this one *does* use an items array)
                return {"command": "trade.buy", "data": {
                    "port_id": port_id,
                    "items": [
                        {"commodity": commodity_to_buy, "quantity": quantity}
                    ]
                }}

        except Exception as e:
            logger.error(f"Error in _achieve_goal for '{goal_str}': {e}", exc_info=True)
        
        return None # Fallback

    # --- Main Planner Entry Point ---

    def get_next_command(self, current_state, current_goal=None):
        """
        Selects the next command, prioritizing the strategic goal.
        """
        
        # --- 1. Check Invariants (Must-Do Actions) ---
        invariant_command = self._check_invariants(current_state)
        if invariant_command:
            return invariant_command

        # --- 2. Try to Achieve Strategic Goal (NEW) ---
        if current_goal:
            goal_command_dict = self._achieve_goal(current_state, current_goal)
            
            if goal_command_dict:
                command_name = goal_command_dict.get("command")
                # Check for cooldowns/retries *before* returning
                if not self._is_command_ready(command_name, current_state.get("command_retry_info", {})):
                    logger.warning(f"Goal logic wants {command_name}, but it's on cooldown/blacklisted.")
                else:
                    logger.info(f"Planner is executing goal: {current_goal} -> {command_name}")
                    return goal_command_dict
            else:
                # Goal logic returned None (e.g., goal complete, or can't execute)
                # If the goal was already complete, main.py will pop it.
                # If it failed (e.g., "not at port"), we fall through to stage logic.
                logger.debug(f"Could not find a valid command for goal: {current_goal}. Falling back to stage logic.")
        
        # --- 3. Fallback to Stage-Based Logic ---
        # If no goal, or goal logic failed, fall back to existing stage-based logic
        
        self.current_stage = self._select_stage(current_state)
        logger.debug(f"Planner stage: {self.current_stage} (Fallback)")

        # --- FIX: Call the *stage-based* action catalogue builder ---
        action_catalogue = self._build_action_catalogue_by_stage(current_state, self.current_stage)
        
        # Filter out actions that are on cooldown or have failed too many times
        ready_actions = self._filter_ready_actions(
            action_catalogue,
            current_state.get("command_retry_info", {})
        )

        if not ready_actions:
            logger.debug("No ready actions for stage '%s'. Waiting for cooldowns.", self.current_stage)
            return None

        # --- 4. Select Action (Bandit or Simple) ---
        
        # --- THIS IS THE FIX for AttributeError ---
        # It should be self.config, not self.config.settings
        context_key = make_context_key(current_state, self.config) # Pass config dict
        # --- END FIX ---
        
        command_name = self.bandit_policy.choose_action(ready_actions, context_key)
        logger.info(f"Bandit selected action: {command_name} for stage {self.current_stage}")
        
        # --- 5. Build and Return Payload ---
        # --- FIX: Find the correct payload function ---
        payload_function = getattr(self, f"_payload_{command_name.replace('.', '_')}", None)
        if payload_function:
            data = payload_function(current_state)
            if data is not None:
                # Store context for reward
                self.state_manager.set("last_action", command_name)
                self.state_manager.set("last_context_key", context_key)
                self.state_manager.set("last_stage", self.current_stage)
                return {"command": command_name, "data": data}
        
        logger.error(f"No payload function found for action: {command_name} (tried _payload_{command_name.replace('.', '_')})")
        return None

    # --- Invariant Checks (Priority 0) ---
    def _check_invariants(self, current_state):
        """Checks for critical missing state and returns a command to fix it."""
        if not current_state.get("player_info"):
            return {"command": "player.my_info", "data": {}}
        if not current_state.get("ship_info"):
            return {"command": "ship.info", "data": {}}
        
        # --- THIS IS THE NEW FIX for "No sector_data" ---
        current_sector = str(current_state.get("player_location_sector"))
        if current_sector != "None" and not current_state.get("sector_data", {}).get(current_sector):
            logger.info(f"Invariant check: Missing sector_data for current sector {current_sector}. Fetching.")
            return {"command": "sector.info", "data": {"sector_id": int(current_sector)}}
        # --- END NEW FIX ---
            
        return None
        
    # --- Stage Selection (Priority 1) ---
    def _select_stage(self, current_state):
        """Determines the bot's current high-level 'stage'."""
        
        # These checks are sequential and act as a priority list.
        current_sector_id = str(current_state.get("player_location_sector"))
        if not current_state.get("sector_data", {}).get(current_sector_id):
            return "explore"
        
        if self._at_port(current_state) and not self._survey_complete(current_state):
            return "survey"
            
        if self._get_free_holds(current_state) == 0 or self._survey_complete(current_state):
            return "exploit"
        
        return "explore"

    # --- Action Catalogue (Priority 2) ---
    # --- FIX: Renamed to match original logic ---
    def _build_action_catalogue_by_stage(self, current_state, stage):
        """Returns a prioritized list of actions based on the current stage."""
        
        # --- FIX: This is the simple, correct logic ---
        if stage == "explore":
            return ["sector.info", "move.warp", "bank.balance"]
            
        elif stage == "survey":
            return ["trade.port_info", "trade.quote"]
            
        elif stage == "exploit":
            actions = []
            if self._can_sell(current_state):
                actions.append("trade.sell")
            if self._can_buy(current_state):
                actions.append("trade.buy")
            actions.append("move.warp")
            actions.append("bank.deposit")
            actions.append("bank.balance")
            return actions
        
        return ["bank.balance"] # Default

    # --- Action Filtering ---
    
    def _is_command_ready(self, command_name, retry_info):
        """Checks if a single command is on cooldown or has failed too many times."""
        info = retry_info.get(command_name, {})
        failures = info.get("failures", 0)
        
        # Check for permanent failure (for this session)
        if failures >= self.state_manager.config.get("max_retries_per_command", 3):
            logger.debug("Command '%s' is blacklisted (failed %d times).", command_name, failures)
            return False
            
        # Check for time-based cooldown
        next_retry_time = info.get("next_retry_time", 0)
        if time.time() < next_retry_time:
            logger.debug("Command '%s' is on cooldown.", command_name)
            return False
            
        return True

    def _filter_ready_actions(self, actions, retry_info):
        """Filters the action list, removing un-ready commands."""
        return [cmd for cmd in actions if self._is_command_ready(cmd, retry_info)]

    # --- State Helper Functions ---

    def _get_free_holds(self, current_state):
        """Calculates remaining free cargo space."""
        ship_info = current_state.get("ship_info", {})
        max_holds = ship_info.get("holds", 0)
        current_cargo_data = ship_info.get("cargo", {})
        if isinstance(current_cargo_data, dict):
            current_cargo = sum(current_cargo_data.values())
        else:
            current_cargo = 0 # Fallback for empty/malformed cargo
        return max_holds - current_cargo


    def _at_port(self, current_state):
        """Checks if the ship is currently at a port."""
        sector_id = str(current_state.get("player_location_sector"))
        sector_data = current_state.get("sector_data", {}).get(sector_id, {})
        return sector_data.get("has_port", False)

    def _survey_complete(self, current_state):
        """Checks if we have price data for the current port."""
        sector_id = str(current_state.get("player_location_sector"))
        port_id = self._find_port_in_sector(current_state, sector_id)
        if not port_id:
            return True # No port to survey
        
        port_id = str(port_id)
        price_cache = current_state.get("price_cache", {}).get(port_id, {})
        
        # "Complete" means we have at least one buy price and one sell price
        return bool(price_cache.get("buy", {})) and bool(price_cache.get("sell", {}))

    def _can_sell(self, current_state):
        """Checks if we have cargo and are at a port that will buy it."""
        if not self._at_port(current_state) or not self._survey_complete(current_state):
            return False
        
        cargo = current_state.get("ship_info", {}).get("cargo", {})
        if not cargo:
            return False # Nothing to sell
        
        port_id = str(self._find_port_in_sector(current_state, current_state.get("player_location_sector")))
        port_sell_prices = current_state.get("price_cache", {}).get(port_id, {}).get("sell", {})

        # Check if we have any cargo that this port is buying
        for commodity, quantity in cargo.items():
            if quantity > 0 and commodity.upper() in port_sell_prices:
                return True
        return False

    def _can_buy(self, current_state):
        """Checks if we have free holds and are at a port that sells something."""
        if not self._at_port(current_state) or not self._survey_complete(current_state):
            return False
            
        if self._get_free_holds(current_state) <= 0:
            return False # No space
            
        port_id = str(self._find_port_in_sector(current_state, current_state.get("player_location_sector")))
        port_buy_prices = current_state.get("price_cache", {}).get(port_id, {}).get("buy", {})
        
        # Check if this port sells *anything*
        return bool(port_buy_prices)

    # --- START: Payload Generation Functions ---
    # --- FIX: Restored all payload functions ---
    
    def _payload_sector_info(self, current_state):
        return {"sector_id": current_state.get("player_location_sector")}
        
    def _payload_player_my_info(self, current_state):
        return {}
        
    def _payload_ship_info(self, current_state):
        return {}

    def _payload_trade_port_info(self, current_state):
        port_id = self._find_port_in_sector(current_state, current_state.get("player_location_sector"))
        if port_id:
            return {"port_id": port_id}
        return None

    def _payload_trade_quote(self, current_state):
        port_id = self._find_port_in_sector(current_state, current_state.get("player_location_sector"))
        if not port_id:
            return None
        
        # --- FIX: This logic was broken and causing 400 errors ---
        # Get commodities from port.info
        current_sector = str(current_state.get("player_location_sector"))
        port_info = current_state.get("port_info_by_sector", {}).get(current_sector)
        if not port_info:
             # Fallback if port_info is missing
             return {"port_id": port_id, "commodity": "ORE", "quantity": 1}

        # Find a commodity to quote
        available_commodities = []
        # Handle nested structure
        if "port" in port_info and "commodities" in port_info["port"]:
            available_commodities = port_info["port"]["commodities"]
        elif "commodities" in port_info:
             available_commodities = port_info.get("commodities", [])
        
        if not available_commodities:
            available_commodities = ["ORE", "ORGANICS", "EQUIPMENT"] # Last resort

        price_cache_for_port = current_state.get("price_cache", {}).get(str(port_id), {})

        # Try to find one we haven't quoted yet
        for commodity in available_commodities:
            # Check if commodity is a string before calling upper()
            if isinstance(commodity, str) and commodity.upper() not in price_cache_for_port.get("buy", {}) and commodity.upper() not in price_cache_for_port.get("sell", {}):
                return {"port_id": port_id, "commodity": commodity.upper(), "quantity": 1}

        # If all are quoted, just quote the first one again
        if available_commodities:
            return {"port_id": port_id, "commodity": available_commodities[0].upper(), "quantity": 1}
        
        # Final fallback
        return {"port_id": port_id, "commodity": "ORE", "quantity": 1}


    def _payload_bank_balance(self, current_state):
        return {}
        
    def _payload_bank_deposit(self, current_state):
        credits = current_state.get("player_info", {}).get("credits", 0)
        if credits > 1000:
            # Deposit all but 1000
            return {"amount": credits - 1000}
        return None 

    def _payload_move_warp(self, current_state):
        current_sector = current_state.get("player_location_sector")
        
        # --- FIX: This is the ship_id bug ---
        ship_id = current_state.get("ship_info", {}).get("id") # <-- WAS "ship_id"
        
        if not ship_id:
            logger.error("Cannot warp: ship_id is missing from state.")
            return None
            
        sector_data_map = current_state.get("sector_data", {})
        current_sector_data = sector_data_map.get(str(current_sector))
        if not current_sector_data:
            logger.error(f"Cannot warp: No sector_data for current sector {current_sector}.")
            return None
            
        adjacent_sectors = current_sector_data.get("adjacent", [])
        if not adjacent_sectors:
             return None # Stuck
            
        # Try to find an adjacent sector we haven't explored yet
        for sector_id in adjacent_sectors:
            if str(sector_id) not in sector_data_map:
                return self._warp_payload(current_state, sector_id)
        
        # If all are explored, just pick one at random
        target_sector = random.choice(adjacent_sectors)
        return self._warp_payload(current_state, target_sector)


    def _warp_payload(self, current_state, target_sector):
        """Helper to build the warp payload."""
        
        # --- FIX: This is the ship_id bug ---
        ship_id = current_state.get("ship_info", {}).get("id") # <-- WAS "ship_id"
        
        if not ship_id:
            return None
        return {"sector_id": target_sector, "ship_id": ship_id}

    def _payload_trade_sell(self, current_state):
        if not self._can_sell(current_state):
            return None
            
        cargo = current_state.get("ship_info", {}).get("cargo", {})
        port_id = self._find_port_in_sector(current_state, current_state.get("player_location_sector"))
        port_sell_prices = current_state.get("price_cache", {}).get(str(port_id), {}).get("sell", {})

        # Find the best thing to sell
        best_commodity = None
        best_price = 0
        
        for commodity, quantity in cargo.items():
            if quantity > 0:
                price = port_sell_prices.get(commodity.upper(), 0)
                if price > best_price:
                    best_price = price
                    best_commodity = commodity.upper()
        
        if best_commodity:
            # --- CRITICAL FIX: Use the correct FLAT payload for trade.sell ---
            return {
                "port_id": port_id,
                "commodity": best_commodity,
                "quantity": int(cargo[best_commodity.lower()]) # Sell all of it
            }
        return None

    def _payload_trade_buy(self, current_state):
        if not self._can_buy(current_state):
            return None
            
        port_id = self._find_port_in_sector(current_state, current_state.get("player_location_sector"))
        port_buy_prices = current_state.get("price_cache", {}).get(str(port_id), {}).get("buy", {})
        free_holds = self._get_free_holds(current_state)
        current_credits = current_state.get("player_info", {}).get("credits", 0)

        # Find the cheapest thing to buy
        best_commodity = None
        lowest_price = float('inf')
        
        for commodity, price in port_buy_prices.items():
            if price < lowest_price:
                lowest_price = price
                best_commodity = commodity
        
        if best_commodity and current_credits > lowest_price:
            # Try to buy 10, or fill holds, or max affordable, whichever is less
            max_qty_by_credits = int(current_credits / lowest_price)
            quantity_to_buy = min(10, free_holds, max_qty_by_credits)
            
            if quantity_to_buy <= 0: return None
            
            return {
                "port_id": port_id,
                "items": [
                    {"commodity": best_commodity, "quantity": quantity_to_buy}
                ]
            }
        return None
    # --- END: Payload Generation Functions ---
