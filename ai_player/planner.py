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
    "bank.balance",
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
                    logger.error(f"Invalid target sector ID in goal '{goal_str}'. Expected an integer.")
                    return None
                
                logger.debug(f"GOTO goal: current_sector={current_sector}, target_sector={target_sector}")
                if current_sector == target_sector:
                    logger.debug("Goal 'goto' already complete (at target).")
                    return None 
                
                sector_data = current_state.get("sector_data", {}).get(str(current_sector), {})
                adjacent_sectors = sector_data.get("adjacent", [])
                logger.debug(f"GOTO goal: sector_data for current_sector {current_sector}: {sector_data}") # ADDED THIS LINE
                logger.debug(f"GOTO goal: adjacent_sectors for current_sector {current_sector}: {adjacent_sectors}") # ADDED THIS LINE
                if target_sector in adjacent_sectors:
                    return {"command": "move.warp", "data": {"to_sector_id": target_sector}}
                else:
                    logger.warning(f"Cannot 'goto' {target_sector}. Not adjacent to {current_sector}. Falling back to explore.")
                    return None

            elif goal_type == "sell":
                port_id = self._find_port_in_sector(current_state, current_sector)
                if not port_id:
                    logger.warning("Goal 'sell' failed: not at a port. Falling back.")
                    return None 
                
                commodity_to_sell = goal_target.upper() 
                cargo = current_state.get("ship_info", {}).get("cargo", {})
                quantity = cargo.get(commodity_to_sell.lower()) 
                
                if not quantity:
                    logger.warning(f"Goal 'sell' failed: no '{commodity_to_sell}' in cargo.")
                    return None 

                return {"command": "trade.sell", "data": {
                    "port_id": port_id,
                    "items": [{"commodity": commodity_to_sell, "quantity": int(quantity)}]
                }}

            elif goal_type == "buy":
                port_id = self._find_port_in_sector(current_state, current_sector)
                if not port_id:
                    logger.warning("Goal 'buy' failed: not at a port. Falling back.")
                    return None 
                
                commodity_to_buy = goal_target.upper() 
                
                free_holds = self._get_free_holds(current_state)
                if free_holds <= 0:
                    logger.warning(f"Goal 'buy' failed: no free holds to buy '{commodity_to_buy}'.")
                    return None
                
                quantity = min(10, free_holds) 

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
                port_commodities = [c.get("symbol") for c in port_info.get("commodities", [])]
                
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

    # --- Main Planner Entry Point ---

    def get_next_command(self, current_state, current_goal=None):
        """
        Selects the next command, prioritizing the strategic goal.
        """
        
        # --- 1. Check Invariants (Must-Do Actions) ---
        invariant_command = self._check_invariants(current_state)
        if invariant_command:
            # Invariant commands are simple and don't need the full payload builder
            return invariant_command

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
                if not self._is_command_ready(command_name, current_state.get("command_retry_info", {})):
                    logger.warning(f"Goal logic wants {command_name}, but it's on cooldown/blacklisted.")
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
            
            # Check for missing current sector data
            if not current_sector_data:
                logger.info(f"Invariant check: Missing sector_data for current sector {current_sector_id}. Fetching.")
                return {"command": "sector.info", "data": {"sector_id": int(current_sector_id)}}
            
            # Check for missing schema for sector.scan.density if we have the scanner
            if current_state.get("has_density_scanner") and not self.state_manager.get_schema("sector.scan.density"):
                logger.info("Invariant check: Missing schema for sector.scan.density. Fetching.")
                return {"command": "system.describe_schema", "data": {"name": "sector.scan.density"}}
            
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
                    return {"command": "sector.scan.density", "data": {}}
            
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
            actions.append("move.warp")
            player_credits_str = current_state.get("player_info", {}).get("player", {}).get("credits", "0")
            try:
                if float(player_credits_str) > 1000:
                    actions.append("bank.deposit")
            except (ValueError, TypeError):
                pass # Ignore if credits is not a valid number
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
        current_cargo_data = ship_info.get("cargo", {})
        if isinstance(current_cargo_data, dict):
            current_cargo = sum(current_cargo_data.values())
        else:
            current_cargo = 0 # Fallback for empty/malformed cargo
        logger.debug(f"Free holds calculation: max_holds={max_holds}, current_cargo_data={current_cargo_data}, current_cargo={current_cargo}, free_holds={max_holds - current_cargo}") # ADDED THIS LINE
        return max_holds - current_cargo


    def _at_port(self, current_state):
        """Checks if the ship is currently at a port."""
        sector_id = str(current_state.get("player_location_sector"))
        sector_data = current_state.get("sector_data", {}).get(sector_id, {})
        return sector_data.get("has_port", False)

    def _survey_complete(self, current_state):
        """Checks if we have price data for all commodities at the current port."""
        sector_id = str(current_state.get("player_location_sector"))
        port_id = self._find_port_in_sector(current_state, sector_id)
        if not port_id:
            return False  # No port here; nothing to survey

        port_id_str = str(port_id)
        port_info = current_state.get("port_info_by_sector", {}).get(sector_id, {})
        port_commodities = [c.get("symbol") for c in port_info.get("commodities", [])]
        
        if not port_commodities:
            # If port_info doesn't list commodities, we can't know if survey is complete.
            # Assume complete if we have any price data.
            price_cache = current_state.get("price_cache", {}).get(port_id_str, {})
            return bool(price_cache.get("buy", {})) and bool(price_cache.get("sell", {}))

        price_cache = current_state.get("price_cache", {}).get(port_id_str, {})
        for commodity_symbol in port_commodities:
            if not (price_cache.get("buy", {}).get(commodity_symbol) is not None and
                    price_cache.get("sell", {}).get(commodity_symbol) is not None):
                return False # Missing price data for at least one commodity
        
        return True

    def _can_sell(self, current_state):
        """Checks if we have cargo and are at a port that will buy it."""
        if not self._at_port(current_state) or not self._survey_complete(current_state):
            return False
        
        cargo = current_state.get("ship_info", {}).get("cargo", {})
        if not cargo:
            return False # Nothing to sell
        
        port_id = str(self._find_port_in_sector(current_state, current_state.get("player_location_sector")))
        port_sell_prices = current_state.get("price_cache", {}).get(port_id, {}).get("sell", {})
        logger.debug(f"Can sell check: cargo={cargo}, port_id={port_id}, port_sell_prices={port_sell_prices}") # ADDED THIS LINE

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
        
        return bool(port_buy_prices)

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
            logger.debug(f"No schema found for command '{command_name}'. Assuming empty payload.")
            return {}

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
            return self._get_next_warp_target(current_state)
        if field_name == "ship_id":
            return ship_id
        if field_name == "sector_id":
            return current_sector
        if field_name == "port_id":
            return self._find_port_in_sector(current_state, current_sector)
        if field_name == "idempotency_key":
            return str(uuid.uuid4())
                
        if field_name == "commodity":
            if command_name == "trade.quote": return "ORE"
        
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
                    if credits > 1000:
                        return int(credits - 1000)
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
                
                quantity = min(10, free_holds, max_affordable_quantity)
                
                if quantity > 0: return [{"commodity": commodity, "quantity": quantity}]
                else: 
                    logger.warning(f"Not enough credits ({player_credits}) or free holds ({free_holds}) to buy {commodity} at {buy_price}. Cannot generate buy command.")
                    return None # Cannot buy if no free holds or not enough credits
            if command_name == "trade.sell":
                commodity = self._get_best_commodity_to_sell(current_state)
                if not commodity: return None
                quantity = current_state.get("ship_info", {}).get("cargo", {}).get(commodity.lower(), 0)
                if quantity > 0: return [{"commodity": commodity, "quantity": int(quantity)}]
                else: return None # Cannot sell if no cargo

        logger.warning(f"No generation logic for required field '{field_name}' in command '{command_name}'.")
        return None

    # --- Payload Helper Functions ---

    def _get_next_warp_target(self, current_state):
        current_sector = current_state.get("player_location_sector")
        sector_data_map = current_state.get("sector_data", {})
        current_sector_data = sector_data_map.get(str(current_sector))
        if not current_sector_data: return None
            
        adjacent_sectors = current_sector_data.get("adjacent", [])
        if not adjacent_sectors: return None

        # --- FIX: Don't warp to the same sector ---
        possible_targets = [s for s in adjacent_sectors if s != current_sector]
        if not possible_targets: return None

        # --- FIX 3.3: Avoid recent sectors to prevent ping-pong ---
        recent = set(current_state.get("recent_sectors", [])[-5:])
        non_recent_targets = [s for s in possible_targets if s not in recent]
        
        if non_recent_targets:
            possible_targets = non_recent_targets
        # If all possible targets are recent, we'll just have to pick one
            
        # Prioritize sectors with ports identified by density scan
        sectors_with_ports = []
        unexplored_sectors = []
        for sector_id in possible_targets:
            adj_sector_info = next((s for s in current_state.get("adjacent_sectors_info", []) if s.get("sector_id") == sector_id), None)
            if adj_sector_info and adj_sector_info.get("has_port"):
                sectors_with_ports.append(sector_id)
            elif str(sector_id) not in sector_data_map:
                unexplored_sectors.append(sector_id)
        
        if sectors_with_ports:
            return random.choice(sectors_with_ports) # Go to a sector with a known port
        if unexplored_sectors:
            return random.choice(unexplored_sectors) # Go to an unexplored sector
        
        return random.choice(possible_targets) # All explored, pick random

    def _get_best_commodity_to_sell(self, current_state):
        cargo = current_state.get("ship_info", {}).get("cargo", {})
        port_id = str(self._find_port_in_sector(current_state, current_state.get("player_location_sector")))
        port_sell_prices = current_state.get("price_cache", {}).get(port_id, {}).get("sell", {})

        best_commodity = None
        best_price = 0
        for commodity, quantity in cargo.items():
            if quantity > 0:
                price = port_sell_prices.get(commodity.upper(), 0)
                if price > best_price:
                    best_price = price
                    best_commodity = commodity.upper()
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
