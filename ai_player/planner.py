import logging
import time
import random
import json
from jsonschema import validate, ValidationError
from main import load_schemas_from_docs # Import the function

logger = logging.getLogger(__name__)

class FiniteStatePlanner:
    def __init__(self, state_manager, game_connection, config, bug_reporter, bandit_policy):
        self.state_manager = state_manager
        self.game_connection = game_connection
        self.config = config
        self.bug_reporter = bug_reporter
        self.bandit_policy = bandit_policy

        # Initialize current stage, and retrieve cooldown/failure thresholds from config
        self.current_stage = self.state_manager.get("stage", "bootstrap")
        self.max_port_info_failures = self.config.get("max_port_info_failures", 3)
        self.dynamic_cooldown = self.config.get("cooldown_seconds", 15)
        
        # Build the action catalogue with dynamic preconditions and payload builders
        self.action_catalogue = self._build_action_catalogue()

    def _build_action_catalogue(self):
        return {
            "bootstrap": [
                {"command": "auth.login", "precondition": lambda s: not s.get("session_token"), "payload_builder": self._login_payload},
                {"command": "system.cmd_list", "precondition": lambda s: s.get("session_token") and not s.get("normalized_commands"), "payload_builder": self._cmd_list_payload},
                {"command": "system.describe_schema", "precondition": lambda s: s.get("session_token") and s.get("normalized_commands") and s.get("schemas_to_fetch"), "payload_builder": self._describe_next_schema_payload},
            ],
            "survey": [
                # Only request if ship_info is MISSING or older than COOLDOWN
                {"command": "ship.info", "precondition": lambda s, dynamic_cooldown: not s.get("ship_info") or (time.time() - s.get("last_ship_info_request_time", 0)) > dynamic_cooldown, "payload_builder": lambda s, dynamic_cooldown: {}},
                # Only request if sector_info is MISSING or older than COOLDOWN
                {"command": "sector.info", "precondition": lambda s, dynamic_cooldown: not s.get("sector_info_fetched_for", {}).get(s.get("player_location_sector")) or (time.time() - s.get("last_sector_info_request_time", 0)) > dynamic_cooldown, "payload_builder": lambda s, dynamic_cooldown: {}},
                {"command": "trade.quote", "precondition": self._can_get_trade_quote, "payload_builder": lambda s, dynamic_cooldown: self._trade_quote_payload(s)},
            ],
            "explore": [
                {"command": "move.warp", "precondition": self._can_warp, "payload_builder": lambda s, cooldown: self._warp_payload(s, cooldown)},
                # Fallback actions if warp is on cooldown:
                {"command": "ship.info", "precondition": lambda s, dynamic_cooldown: not s.get("ship_info") or (time.time() - s.get("last_ship_info_request_time", 0)) > dynamic_cooldown, "payload_builder": lambda s, dynamic_cooldown: {}},
                {"command": "sector.info", "precondition": lambda s, dynamic_cooldown: not s.get("sector_info_fetched_for", {}).get(s.get("player_location_sector")) or (time.time() - s.get("last_sector_info_request_time", 0)) > dynamic_cooldown, "payload_builder": lambda s, dynamic_cooldown: {}},
            ],
            "exploit": [
                {"command": "trade.buy", "precondition": self._can_buy, "payload_builder": lambda s, dynamic_cooldown: self._buy_payload(s)},
                {"command": "trade.sell", "precondition": self._can_sell, "payload_builder": lambda s, dynamic_cooldown: self._sell_payload(s)},
                {"command": "move.warp", "precondition": self._can_warp, "payload_builder": lambda s, dynamic_cooldown: self._warp_payload(s, dynamic_cooldown)},
            ],
            "expand": [] # Future stage
        }

    def _login_payload(self, state):
        return {
            "username": self.config.get("player_username"),
            "password": self.config.get("player_password")
        }

    def _describe_schema_payload(self, state):
        # This payload builder is a placeholder. The actual logic for iterating through schemas
        # and sending individual describe_schema commands is handled in main.py for now.
        # This action definition is primarily for the precondition check in bootstrap.
        return {"name": "system"} # Dummy payload

    def _cmd_list_payload(self, state):
        # No specific data needed for cmd_list
        return {}

    def _describe_next_schema_payload(self, state):
        schemas_to_fetch = state.get("schemas_to_fetch", [])
        if schemas_to_fetch:
            command_name = schemas_to_fetch.pop(0) # Get the next command to fetch schema for
            self.state_manager.set("schemas_to_fetch", schemas_to_fetch) # Update state
            return {"type": "command", "name": command_name}
        return None # Should not happen if precondition is met

    def _validate_payload(self, command_name: str, payload: dict) -> bool:
        logger.debug(f"Validating payload for command: {command_name}")
        cached_schemas = self.state_manager.get("cached_schemas", {})
        logger.debug(f"Cached schemas in _validate_payload: {json.dumps(cached_schemas, indent=2)}")
        
        schema_info = cached_schemas.get(command_name)
        logger.debug(f"Schema info for '{command_name}': {json.dumps(schema_info, indent=2)}")

        if not schema_info:
            logger.warning(f"No schema_info found for command '{command_name}'. Skipping validation.")
            return True # Cannot validate, so assume valid for now
        
        request_schema = schema_info.get("request_schema")
        logger.debug(f"Request schema for '{command_name}': {json.dumps(request_schema, indent=2)}")

        if not request_schema or request_schema == {}: # Explicitly check for empty schema
            logger.warning(f"Empty or no request_schema found for command '{command_name}' in cached schema. Skipping validation.")
            return True

        try:
            validate(instance=payload, schema=request_schema)
            return True
        except ValidationError as e:
            logger.error(f"Payload validation failed for command '{command_name}': {e.message}")
            self.bug_reporter.triage_protocol_error(
                {"command": command_name, "data": payload}, # Sent command info
                {"error": {"message": e.message, "details": str(e)}}, # Response-like error info
                self.state_manager.get_all(),
                f"Payload validation failed for command '{command_name}': {e.message}",
                self.state_manager.get("last_commands_history", []),
                self.state_manager.get("last_responses_history", []),
                sent_schema=request_schema, # Pass the schema used for validation
                validated=False # Validation failed
            )
            return False

    def _can_warp(self, state, dynamic_cooldown):
        if not state.get("ship_info", {}).get("id"):
            return False
        # Check if there are adjacent sectors and enough fuel/turns (placeholder for now)
        sector_data = state.get("sector_data", {})
        sector_data_for_current_sector = sector_data.get(str(state.get("player_location_sector")), {})
        adjacent_sectors = sector_data_for_current_sector.get("adjacent", [])
        
        # Check for strategic trade route
        best_route = self._find_best_trade_route(state)
        current_sector = state.get("player_location_sector")

        if best_route:
            if current_sector != best_route["buy_sector_id"] and current_sector != best_route["sell_sector_id"]:
                # If we are not at the buy or sell sector, we need to warp towards one of them
                # For simplicity, let's assume we always warp to the buy_sector first if not there
                if current_sector != best_route["buy_sector_id"]:
                    # We need to warp to buy_sector_id
                    # For now, just check if it's reachable (adjacent)
                    if best_route["buy_sector_id"] in adjacent_sectors:
                        return True
                elif current_sector != best_route["sell_sector_id"]:
                    # We need to warp to sell_sector_id
                    # For now, just check if it's reachable (adjacent)
                    if best_route["sell_sector_id"] in adjacent_sectors:
                        return True
            elif current_sector == best_route["buy_sector_id"] and state.get("ship_info", {}).get("cargo", {}).get(best_route["commodity"], 0) < best_route["quantity"]:
                # We are at the buy sector but haven't bought enough, so don't warp yet
                return False
            elif current_sector == best_route["sell_sector_id"] and state.get("ship_info", {}).get("cargo", {}).get(best_route["commodity"], 0) > 0:
                # We are at the sell sector and still have cargo to sell, so don't warp yet
                return False

        return bool(adjacent_sectors) # Fallback to random warp if no strategic route or not yet ready to warp for it

    def _warp_payload(self, state, dynamic_cooldown):
        current_sector = state.get("player_location_sector", 1)
        sector_data = state.get("sector_data", {})
        sector_data_for_current_sector = sector_data.get(str(state.get("player_location_sector")), {})
        adjacent_sectors = sector_data_for_current_sector.get("adjacent", [])
        
        best_route = self._find_best_trade_route(state)

        if best_route:
            target_sector = None
            # If we are at the buy sector and have bought enough, warp to sell sector
            if current_sector == best_route["buy_sector_id"] and state.get("ship_info", {}).get("cargo", {}).get(best_route["commodity"], 0) >= best_route["quantity"]:
                target_sector = best_route["sell_sector_id"]
            # If we are not at the buy sector, warp to buy sector
            elif current_sector != best_route["buy_sector_id"]:
                target_sector = best_route["buy_sector_id"]
            
            if target_sector and target_sector in adjacent_sectors:
                ship_id = state.get("ship_info", {}).get("id")
                if not ship_id:
                    logger.error("Cannot warp: ship_id is missing from state.")
                    return None
                logger.info(f"Strategic warp: Moving towards sector {target_sector}.")
                return {"sector_id": target_sector}

        if adjacent_sectors:
            # Fallback to intelligent warp if no strategic route or not ready for strategic warp
            # Pick the adjacent sector with the highest value
            best_adjacent_sector = None
            highest_value = -float('inf')

            for sector_id in adjacent_sectors:
                # Avoid warping back to the current sector unless it's the only option
                if sector_id == current_sector and len(adjacent_sectors) > 1:
                    continue
                
                value = self._evaluate_sector_value(state, sector_id, dynamic_cooldown)
                if value > highest_value:
                    highest_value = value
                    best_adjacent_sector = sector_id
            
            if best_adjacent_sector:
                logger.info(f"Adaptive exploration: Warping to sector {best_adjacent_sector} with value {highest_value}.")
                return {"sector_id": best_adjacent_sector}
            elif adjacent_sectors: # Fallback to any adjacent if no "best" found (e.g., all have same low value)
                chosen_sector = random.choice(adjacent_sectors)
                logger.info(f"Adaptive exploration: Warping to sector {chosen_sector} (random fallback).")
                return {"sector_id": chosen_sector}
        return None # Cannot warp

    def _can_get_trade_quote(self, state, dynamic_cooldown):
        # Only try if we have the port ID and the command isn't permanently broken.
        broken_trade_quote = "trade.quote" in [item["command"] for item in state.get("broken_commands", [])]
        ports = state.get("sector_data", {}).get(str(state.get("player_location_sector")), {}).get("ports", [])
        
        logger.debug(f"_can_get_trade_quote: broken_trade_quote={broken_trade_quote}, ports_present={bool(ports)}")

        if broken_trade_quote:
            return False
        
        return bool(ports)

    def _trade_quote_payload(self, state):
        # Assuming the current port is always the first one listed in the sector.
        ports = state.get("sector_data", {}).get(str(state.get("player_location_sector")), {}).get("ports", [])
        if ports:
            port_id = ports[0].get("id")
            # Ask about a few common commodities dynamically, or hardcode known goods for now
            commodity = random.choice(['ore', 'organics', 'equipment']) 
            return {"port_id": port_id, "commodity": commodity, "quantity": 1}
        return None

    def _can_buy(self, state, dynamic_cooldown):
        # Preconditions for buying:
        # 1. Have a valid price_cache for the current sector and port
        # 2. Have enough credits
        # 3. Have cargo space
        # 4. Port has stock of the commodity (implied by price_cache having a buy price)
        current_sector = state.get("player_location_sector")
        ports_in_sector = state.get("sector_data", {}).get(str(current_sector), {}).get("ports", [])
        
        if not ports_in_sector:
            return False
        
        current_port_id = ports_in_sector[0].get("id") # Assuming current port is the first one
        
        price_cache_for_port = state.get("price_cache", {}).get(current_sector, {}).get(current_port_id, {})
        ship_info = state.get("ship_info", {})
        current_credits = state.get("current_credits", 0.0)
        
        if not price_cache_for_port or not ship_info:
            return False

        # Find a commodity that can be bought
        for commodity, prices in price_cache_for_port.items():
            buy_price = prices.get("buy")
            current_cargo_count = sum(ship_info.get('cargo', {}).values())
            holds_free = ship_info.get('holds', 0) - current_cargo_count
            if buy_price is not None and current_credits >= buy_price and holds_free > 0:
                return True
        return False

    def _buy_payload(self, state):
        current_sector = state.get("player_location_sector")
        ports_in_sector = state.get("sector_data", {}).get(str(current_sector), {}).get("ports", [])
        
        if not ports_in_sector:
            return None
        
        port_id = ports_in_sector[0].get("id") # Assuming current port is the first one
        
        price_cache_for_port = state.get("price_cache", {}).get(current_sector, {}).get(port_id, {})
        ship_info = state.get("ship_info", {})
        current_credits = state.get("current_credits", 0.0)
        
        if not ports_in_sector or not price_cache_for_port or not ship_info:
            return None

        current_cargo_count = sum(ship_info.get('cargo', {}).values())
        holds_free = ship_info.get('holds', 0) - current_cargo_count

        # Find a profitable commodity to buy
        for commodity, prices in price_cache_for_port.items():
            buy_price = prices.get("buy")
            if buy_price is not None and current_credits >= buy_price and holds_free > 0:
                # For now, buy 1 unit. Later, this will be more sophisticated.
                return {"port_id": port_id, "commodity": commodity, "quantity": 1}
        return None

    def _can_sell(self, state, dynamic_cooldown):
        # Preconditions for selling:
        # 1. Have a valid price_cache for the current sector and port
        # 2. Have cargo to sell
        current_sector = state.get("player_location_sector")
        ports_in_sector = state.get("sector_data", {}).get(str(current_sector), {}).get("ports", [])
        
        if not ports_in_sector:
            return False
        
        current_port_id = ports_in_sector[0].get("id") # Assuming current port is the first one
        
        price_cache_for_port = state.get("price_cache", {}).get(current_sector, {}).get(current_port_id, {})
        ship_info = state.get("ship_info", {})
        
        if not price_cache_for_port or not ship_info:
            return False

        # Find a commodity that can be sold
        for commodity, prices in price_cache_for_port.items():
            sell_price = prices.get("sell")
            if sell_price is not None and ship_info.get("cargo", {}).get(commodity, 0) > 0:
                return True
        return False

    def _sell_payload(self, state):
        current_sector = state.get("player_location_sector")
        ports_in_sector = state.get("sector_data", {}).get(str(current_sector), {}).get("ports", [])
        
        if not ports_in_sector:
            return None
        
        port_id = ports_in_sector[0].get("id") # Assuming current port is the first one

        price_cache_for_port = state.get("price_cache", {}).get(current_sector, {}).get(port_id, {})
        ship_info = state.get("ship_info", {})
        
        if not ports_in_sector or not price_cache_for_port or not ship_info:
            return None

        # Find a profitable commodity to sell
        for commodity, prices in price_cache_for_port.items():
            sell_price = prices.get("sell")
            if sell_price is not None and ship_info.get("cargo", {}).get(commodity, 0) > 0:
                # For now, sell 1 unit. Later, this will be more sophisticated.
                return {"port_id": port_id, "commodity": commodity, "quantity": 1}
        return None

    def _can_deposit(self, state, dynamic_cooldown):
        # Preconditions for depositing:
        # 1. Have credits
        return state.get("current_credits", 0.0) > 0

    def _deposit_payload(self, state):
        # Deposit all current credits
        return {"amount": state.get("current_credits", 0.0)}

    def _can_withdraw(self, state, dynamic_cooldown):
        # Preconditions for withdrawing:
        # 1. Have credits in bank (placeholder - requires bank.info)
        # 2. Need credits for a purchase (placeholder - requires knowing future purchase)
        return False # For now, don't withdraw automatically

    def _withdraw_payload(self, state):
        return {}

    def _can_bank_info(self, state):
        # Only request if bank.info is MISSING or older than COOLDOWN
        return (time.time() - state.get("last_bank_info_request_time", 0)) > self.dynamic_cooldown

    def _bank_info_payload(self, state):
        return {}

    def _find_best_trade_route(self, state):
        """
        Analyzes the price_cache across all known sectors and ports to identify
        the most profitable trade route.
        Returns a dictionary with route details or None if no profitable route.
        """
        price_cache = state.get("price_cache", {})
        ship_info = state.get("ship_info", {})
        current_credits = state.get("current_credits", 0.0)
        
        if not ship_info:
            return None

        holds_free = ship_info.get('holds', 0) - sum(ship_info.get('cargo', {}).values())
        if holds_free <= 0 and not ship_info.get('cargo'): # No space and no cargo to sell
            return None

        best_profit = 0
        best_route = None

        # Iterate through all known sectors and their ports
        for buy_sector_id_str, sector_ports_data in price_cache.items():
            buy_sector_id = int(buy_sector_id_str)
            for buy_port_id_str, buy_port_prices in sector_ports_data.items():
                buy_port_id = int(buy_port_id_str)

                for commodity, buy_prices in buy_port_prices.items():
                    buy_price_per_unit = buy_prices.get("buy")
                    if buy_price_per_unit is None:
                        continue

                    # Calculate how much we can afford/carry
                    max_buy_quantity_by_credits = int(current_credits / buy_price_per_unit) if buy_price_per_unit > 0 else 0
                    max_buy_quantity = min(holds_free, max_buy_quantity_by_credits)

                    if max_buy_quantity <= 0:
                        continue

                    # Now look for a place to sell this commodity
                    for sell_sector_id_str, sell_sector_ports_data in price_cache.items():
                        sell_sector_id = int(sell_sector_id_str)
                        for sell_port_id_str, sell_port_prices in sell_sector_ports_data.items():
                            sell_port_id = int(sell_port_id_str)

                            # Don't trade at the same port
                            if buy_sector_id == sell_sector_id and buy_port_id == sell_port_id:
                                continue

                            sell_prices = sell_port_prices.get(commodity)
                            if sell_prices:
                                sell_price_per_unit = sell_prices.get("sell")
                                if sell_price_per_unit is None:
                                    continue

                                profit_per_unit = sell_price_per_unit - buy_price_per_unit
                                if profit_per_unit > 0:
                                    total_profit = profit_per_unit * max_buy_quantity

                                    if total_profit > best_profit:
                                        best_profit = total_profit
                                        best_route = {
                                            "buy_sector_id": buy_sector_id,
                                            "buy_port_id": buy_port_id,
                                            "sell_sector_id": sell_sector_id,
                                            "sell_port_id": sell_port_id,
                                            "commodity": commodity,
                                            "quantity": max_buy_quantity,
                                            "profit": total_profit
                                        }
        return best_route

    def _evaluate_sector_value(self, state, sector_id: int, dynamic_cooldown: float) -> float:
        """
        Calculates a value score for a given sector based on various factors.
        """
        value = 0.0
        sector_data = state.get("sector_data", {}).get(str(sector_id), {})
        price_cache = state.get("price_cache", {}).get(sector_id, {})
        current_time = time.time()

        # Factor 1: Presence of ports
        if sector_data.get("ports"):
            value += 10.0 # Base value for having ports

        # Factor 2: Freshness and profitability of cached prices
        if price_cache:
            for port_id, port_prices in price_cache.items():
                for commodity, prices in port_prices.items():
                    # Reward for fresh prices (e.g., within the last hour)
                    if current_time - prices.get("timestamp", 0) < 3600:
                        value += 2.0
                    
                    # Reward for potential profitability (simple heuristic)
                    if prices.get("buy") and prices.get("sell") and prices["sell"] > prices["buy"]:
                        value += (prices["sell"] - prices["buy"]) / 10.0 # Scale profit to value

        # Factor 3: Number of adjacent sectors (exploration potential)
        value += len(sector_data.get("adjacent", [])) * 0.5

        # Factor 4: If sector info is old, penalize slightly to encourage re-survey
        if (current_time - state.get("sector_info_fetched_for", {}).get(sector_id, 0)) > dynamic_cooldown * 2:
            value -= 1.0

        return value

    def _generate_context_key(self, state: dict) -> str:
        """Generates a context key for the bandit policy based on the current state."""
        stage = state.get("stage","bootstrap")
        sector = state.get("player_location_sector",1)
        have_ship = 1 if state.get("ship_info",{}).get("id") else 0
        return f"{stage}-{sector}-ship{have_ship}"

    def transition_stage(self, new_stage):
        logger.info(f"Planner: Transitioning from stage '{self.current_stage}' to '{new_stage}'.")
        self.current_stage = new_stage
        self.state_manager.set("stage", new_stage)

    def get_next_command(self, llm_suggestion=None):
        current_state = self.state_manager.get_all()
        logger.info(f"Planner: Current stage is '{self.current_stage}'.")
        
        # Determine dynamic cooldown based on server rate limits
        rate_limit_info = current_state.get("rate_limit_info", {})
        dynamic_cooldown = self.config.get("cooldown_seconds", 15) # Default cooldown from config

        if rate_limit_info:
            remaining = rate_limit_info.get("remaining", 1)
            reset_at = rate_limit_info.get("reset_at", 0) # Use reset_at
            
            if remaining <= 1 and reset_at > time.time(): # Compare with reset_at
                # If we're near the limit, wait until reset_at plus a small buffer
                dynamic_cooldown = max(dynamic_cooldown, reset_at - time.time() + 1)
                logger.warning(f"Dynamic cooldown adjusted to {dynamic_cooldown:.2f}s due to rate limit (remaining: {remaining}).")


        # Stage Transition Logic
        if self.current_stage == "bootstrap":
            logger.debug(f"Bootstrap check: session_token={bool(current_state.get('session_token'))}, cached_schemas={bool(current_state.get('cached_schemas'))}, normalized_commands={bool(current_state.get('normalized_commands'))}, schemas_to_fetch={len(current_state.get('schemas_to_fetch', []))}")
            # Relaxed bootstrap exit criteria: session token present AND (any schemas OR cmd_list loaded)
            if current_state.get("session_token") and current_state.get("normalized_commands") and not current_state.get("schemas_to_fetch"):
                self.transition_stage("survey")
            else:
                # If not logged in or schemas not cached, try bootstrap actions
                for action_def in self.action_catalogue["bootstrap"]:
                    if action_def["precondition"](current_state):
                        payload = action_def["payload_builder"](current_state)
                        if payload is not None:
                            logger.debug(f"Planner: Bootstrap candidate: {action_def['command']}")
                            return {"command": action_def["command"], "data": payload}
                
                # Fallback: If schemas are still to be fetched and no bootstrap action could be taken,
                # load schemas from docs as a last resort.
                if current_state.get("schemas_to_fetch"):
                    logger.warning("Bootstrap: Could not fetch all schemas via system.describe_schema. Falling back to loading from docs.")
                    load_schemas_from_docs(self.state_manager)
                    # After loading from docs, re-evaluate if we can transition or need more actions
                    if not self.state_manager.get("schemas_to_fetch"): # If docs loaded all needed schemas
                        self.transition_stage("survey")
                        return self.get_next_command(llm_suggestion) # Try to get a command for the new stage
                
                logger.error("Bootstrap: Stuck. Cannot login or cache schemas.")
                return None # Cannot proceed

        if self.current_stage == "survey":
            # Check if basic info is gathered and fresh
            ship_info_fresh = (time.time() - current_state.get("last_ship_info_request_time", 0)) <= dynamic_cooldown and bool(current_state.get("ship_info"))
            sector_info_fresh = (time.time() - current_state.get("last_sector_info_request_time", 0)) <= dynamic_cooldown and bool(current_state.get("sector_info_fetched_for", {}).get(current_state.get("player_location_sector")))
            
            # If trade.quote is broken, we can't rely on it for survey completion
            trade_quote_broken = "trade.quote" in [item["command"] for item in current_state.get("broken_commands", [])]
            price_cache_available = current_state.get("price_cache") and current_state.get("player_location_sector") in current_state.get("price_cache", {})

            logger.debug(f"Survey stage checks: ship_info_fresh={ship_info_fresh}, sector_info_fresh={sector_info_fresh}, price_cache_available={price_cache_available}, trade_quote_broken={trade_quote_broken}")

            if ship_info_fresh and sector_info_fresh and (price_cache_available or trade_quote_broken):
                self.transition_stage("exploit") # Move to exploit if survey is complete
            
        # Filter candidate actions based on preconditions, cooldowns, and retry info
        candidates = []
        command_retry_info = current_state.get("command_retry_info", {})
        MAX_RETRIES = self.config.get("max_command_retries", 5) # Max retries before considering permanently broken

        for action_def in self.action_catalogue.get(self.current_stage, []):
            command_name = action_def["command"]
            next_allowed_time = current_state.get("next_allowed", {}).get(command_name, 0)
            
            retry_data = command_retry_info.get(command_name, {"failures": 0, "next_retry_time": 0})

            # Check if command is in backoff
            if time.time() < retry_data["next_retry_time"]:
                logger.debug(f"Command '{command_name}' is in backoff. Next retry at {retry_data['next_retry_time']:.2f}.")
                continue # Skip this command if it's still in backoff

            # Check if command has exceeded max retries and should be considered permanently broken
            if retry_data["failures"] >= MAX_RETRIES:
                logger.warning(f"Command '{command_name}' exceeded max retries ({MAX_RETRIES}). Considering it permanently broken.")
                # Add to broken_commands if not already there
                broken_cmds = current_state.get("broken_commands", [])
                if command_name not in [item["command"] for item in broken_cmds]:
                    broken_cmds.append({"command": command_name, "error": "Exceeded max retries"})
                    self.state_manager.set("broken_commands", broken_cmds)
                continue # Skip this command

            if time.time() >= next_allowed_time:
                if command_name in ["ship.info", "sector.info"] and self.current_stage == "survey":
                    if action_def["precondition"](current_state, dynamic_cooldown):
                        candidates.append(action_def)
                elif action_def["precondition"](current_state, dynamic_cooldown):
                    candidates.append(action_def)
        
        logger.info(f"Planner: Candidate actions for stage '{self.current_stage}': {[c['command'] for c in candidates]}")

        if llm_suggestion:
            # Normalize LLM suggestion command
            normalized_llm_command = current_state.get("normalized_commands", {}).get(llm_suggestion.get("command"), llm_suggestion.get("command"))
            llm_suggestion["command"] = normalized_llm_command # Update command name in suggestion

            # Check if LLM suggestion is a valid and allowed action for the current stage and not on cooldown
            for action_def in candidates:
                if action_def["command"] == normalized_llm_command:
                    # Validate LLM's suggested payload against schema
                    if self._validate_payload(normalized_llm_command, llm_suggestion.get("data", {})):
                        # Apply cooldown to this command
                        next_allowed = current_state.get("next_allowed", {})
                        next_allowed[action_def["command"]] = time.time() + dynamic_cooldown
                        self.state_manager.set("next_allowed", next_allowed)
                        return llm_suggestion # Return LLM's suggestion directly
                    else:
                        logger.warning(f"LLM suggested command '{normalized_llm_command}' with invalid payload. Discarding suggestion.")
                        break # Discard this LLM suggestion and proceed to fallback

        # Fallback to deterministic/bandit selection if no valid LLM suggestion or no LLM suggestion
        if candidates:
            # Use bandit policy to choose an action
            candidate_commands = [action_def["command"] for action_def in candidates]
            context_key = self._generate_context_key(current_state) # Generate context key
            
            # Loop through candidates, trying to find one with a valid payload
            for _ in range(len(candidates)): # Try each candidate once
                chosen_command_name = self.bandit_policy.choose_action(candidate_commands, context_key)
                chosen_action_def = next( (action_def for action_def in candidates if action_def["command"] == chosen_command_name), None)

                if chosen_action_def:
                    payload = chosen_action_def["payload_builder"](current_state, dynamic_cooldown)
                    if payload is None:
                        logger.warning(f"Payload builder for command '{chosen_action_def['command']}' returned None. Trying another candidate.")
                        candidate_commands.remove(chosen_command_name) # Remove from consideration
                        continue
                    
                    # Validate payload before returning
                    if self._validate_payload(chosen_action_def["command"], payload):
                        # Apply cooldown to this command
                        next_allowed = current_state.get("next_allowed", {})
                        next_allowed[chosen_action_def["command"]] = time.time() + dynamic_cooldown
                        self.state_manager.set("next_allowed", next_allowed)
                        return {"command": chosen_action_def["command"], "data": payload}
                    else:
                        logger.warning(f"Generated payload for command '{chosen_action_def['command']}' is invalid. Trying another candidate.")
                        candidate_commands.remove(chosen_command_name) # Remove from consideration
                        continue
                candidate_commands.remove(chosen_command_name) # If chosen_action_def is None, remove it

        logger.warning(f"No valid payloads for candidates in stage '{self.current_stage}'.")
        return None
        
        # If exploit stage has no actions, transition to explore
        if self.current_stage == "exploit" and not candidates:
            logger.info("Exploit stage has no valid actions. Transitioning to explore stage.")
            self.transition_stage("explore")
            # After transitioning, immediately try to get a command for the new stage
            return self.get_next_command(llm_suggestion)

        return None
