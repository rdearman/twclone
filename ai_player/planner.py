import logging
import time
import random
import json
from jsonschema import validate, ValidationError
from bandit_policy import make_context_key # Import the unified context key function

logger = logging.getLogger(__name__)

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

#    def _build_action_catalogue(self):
#        return {
#            "bootstrap": [
#                {"command": "auth.login", "precondition": lambda s: not s.get("session_token"), "payload_builder": self._login_payload},
#                {"command": "ship.info", "precondition": lambda s: s.get("session_token") and not s.get("ship_info"), "payload_builder": lambda s: {}}, # Get ship info early
#                {"command": "system.cmd_list", "precondition": lambda s: s.get("session_token") and not s.get("normalized_commands"), "payload_builder": self._cmd_list_payload},
#                {"command": "system.describe_schema", "precondition": lambda s: s.get("session_token") and s.get("normalized_commands") and s.get("schemas_to_fetch"), "payload_builder": self._describe_next_schema_payload},
#            ],
#            "survey": [
#                # Only request if ship_info is MISSING or older than COOLDOWN
#                {"command": "ship.info", "precondition": lambda s, dynamic_cooldown: not s.get("ship_info") or (time.time() - s.get("last_ship_info_request_time", 0)) > dynamic_cooldown, "payload_builder": lambda s, dynamic_cooldown: {}},
#                {"command": "sector.info", "precondition": lambda s, dynamic_cooldown: not s.get("sector_info_fetched_for", {}).get(s.get("player_location_sector")) or (time.time() - s.get("last_sector_info_request_time", 0)) > dynamic_cooldown, "payload_builder": lambda s, dynamic_cooldown: {"sector_id": s.get("player_location_sector")}},
#                {"command": "trade.quote", "precondition": self._can_get_trade_quote, "payload_builder": lambda s, dynamic_cooldown: self._trade_quote_payload(s)},
#            ],
#            "explore": [
#                {"command": "move.warp", "precondition": self._can_warp, "payload_builder": lambda s, cooldown: self._warp_payload(s, cooldown)},
#                # Fallback actions if warp is on cooldown:
#                {"command": "ship.info", "precondition": lambda s, dynamic_cooldown: not s.get("ship_info") or (time.time() - s.get("last_ship_info_request_time", 0)) > dynamic_cooldown, "payload_builder": lambda s, dynamic_cooldown: {}},
#                {"command": "sector.info", "precondition": lambda s, dynamic_cooldown: not s.get("sector_info_fetched_for", {}).get(s.get("player_location_sector")) or (time.time() - s.get("last_sector_info_request_time", 0)) > dynamic_cooldown, "payload_builder": lambda s, dynamic_cooldown: {}},
#            ],
#            "exploit": [
#                {"command": "trade.buy", "precondition": self._can_buy, "payload_builder": lambda s, dynamic_cooldown: self._buy_payload(s)},
#                {"command": "trade.sell", "precondition": self._can_sell, "payload_builder": lambda s, dynamic_cooldown: self._sell_payload(s)},
#                {"command": "move.warp", "precondition": self._can_warp, "payload_builder": lambda s, dynamic_cooldown: self._warp_payload(s, dynamic_cooldown)},
#            ],
#            "expand": [] # Future stage
#        }
#


    def _build_action_catalogue(self):
        return {
            "bootstrap": [
                {
                    "command": "auth.login",
                    "precondition": lambda s: (
                        not s.get("session_token")
                        and not any(
                            cmd.get("command") == "auth.login"
                            for cmd in s.get("pending_commands", {}).values()
                        )
                    ),
                    "payload_builder": self._login_payload,
                },
                {
                    "command": "system.cmd_list",
                    "precondition": lambda s: (
                        s.get("session_token")
                        and not s.get("normalized_commands")  # empty dict/list -> False
                        and not any(
                            cmd.get("command") == "system.cmd_list"
                            for cmd in s.get("pending_commands", {}).values()
                        )
                    ),
                    "payload_builder": self._cmd_list_payload,
                },
                {
                    "command": "system.describe_schema",
                    "precondition": lambda s: (
                        s.get("session_token")
                        and not s.get("cached_schemas")  # empty dict means nothing cached yet
                        and len(s.get("schemas_to_fetch", [])) > 0
                        and not any(
                            cmd.get("command") == "system.describe_schema"
                            for cmd in s.get("pending_commands", {}).values()
                        )
                    ),
                    "payload_builder": self._describe_schema_payload
                },
                {
                    "command": "ship.info", # Get ship info early
                    "precondition": lambda s: (
                        s.get("session_token")
                        and not s.get("ship_info")
                        and not any(
                            cmd.get("command") == "ship.info"
                            for cmd in s.get("pending_commands", {}).values()
                        )
                    ), 
                    "payload_builder": lambda s: {}
                },
                {
                    "command": "player.my_info",
                    "precondition": lambda s: s.get("session_token") and s.get("last_player_info_request_time") == 0,
                    "payload_builder": lambda s: {}
                },
                {
                    "command": "bank.balance",
                    "precondition": lambda s: s.get("session_token") and s.get("last_bank_info_request_time") == 0,
                    "payload_builder": self._bank_balance_payload
                },
            ],
            "survey": [
                {"command": "player.my_info", "precondition": self._can_get_player_info, "payload_builder": self._player_info_payload},
                {"command": "trade.port_info", "precondition": self._can_get_port_info, "payload_builder": self._port_info_payload},
                {"command": "trade.quote", "precondition": self._can_get_trade_quote, "payload_builder": self._trade_quote_payload},
                {"command": "bank.balance", "precondition": self._can_bank_balance, "payload_builder": self._bank_balance_payload},
            ],
            "explore": [
                {"command": "ship.info", "precondition": self._can_get_ship_info, "payload_builder": lambda s, c: {}},
                {"command": "sector.info", "precondition": self._can_get_sector_info, "payload_builder": self._sector_info_payload},
                {"command": "move.warp", "precondition": self._can_warp, "payload_builder": self._warp_payload},
                {"command": "bank.balance", "precondition": self._can_bank_balance, "payload_builder": self._bank_balance_payload},
            ],
            "exploit": [
                {"command": "trade.buy", "precondition": self._can_buy, "payload_builder": self._buy_payload},
                {"command": "trade.sell", "precondition": self._can_sell, "payload_builder": self._sell_payload},
                {"command": "move.warp", "precondition": self._can_warp, "payload_builder": self._warp_payload},
                {"command": "bank.balance", "precondition": self._can_bank_balance, "payload_builder": self._bank_balance_payload},
                {
                    "command": "bank.deposit",
                    "precondition": self._can_deposit,
                    "payload_builder": self._deposit_payload,
                },
                {
                    "command": "bank.withdraw",
                    "precondition": self._can_withdraw,
                    "payload_builder": self._withdraw_payload,
                },
            ],
            "expand": [] # Future stage
        }

    def _can_get_sector_info(self, state, dynamic_cooldown):
        return not state.get("sector_info_fetched_for", {}).get(str(state.get("player_location_sector"))) or \
               (time.time() - state.get("last_sector_info_request_time", 0)) > dynamic_cooldown

    def _sector_info_payload(self, state, dynamic_cooldown):
        return {"sector_id": state.get("player_location_sector")}

    def _can_get_port_info(self, state, dynamic_cooldown):
        sector_id = str(state.get("player_location_sector"))
        sector_data = state.get("sector_data", {}).get(sector_id, {})
        ports = sector_data.get("ports") or []
        if not ports:
            return False

        fetched = state.get("port_info_by_sector", {})
        return sector_id not in fetched

    def _port_info_payload(self, state, dynamic_cooldown):
        sector_id = str(state.get("player_location_sector"))
        ports = state.get("sector_data", {}).get(sector_id, {}).get("ports", [])
        if not ports:
            return None
        return {"port_id": ports[0].get("id")}

    def _can_get_ship_info(self, state, dynamic_cooldown):
        # Simple cooldown to avoid spamming
        return (time.time() - state.get("last_ship_info_request_time", 0)) > dynamic_cooldown

    def _can_get_player_info(self, state, dynamic_cooldown):
        # Simple cooldown to avoid spamming
        return (time.time() - state.get("last_player_info_request_time", 0)) > dynamic_cooldown

    def _player_info_payload(self, state, dynamic_cooldown):
        return {}

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
        # This is a simplified precondition. The main logic is in the payload builder.
        # We can consider warping if we have a ship.
        return bool((state.get("ship_info") or {}).get("id"))

    def _warp_payload(self, state, dynamic_cooldown):
        logger.debug("Calculating warp payload...")
        
        route = state.get("current_trade_route")
        current_sector = state.get("player_location_sector")
        ship_id = state.get("ship_info", {}).get("id")
        if not ship_id:
            logger.error("Cannot warp: ship_id is missing from state.")
            return None

        # --- 1. Trade Route Navigation ---
        if route:
            target_sector = None
            # At buy sector with cargo -> go to sell sector
            if str(current_sector) == str(route["buy_sector_id"]) and (state.get("ship_info", {}).get("cargo", {}).get(route["commodity"], 0) or 0) > 0:
                target_sector = route["sell_sector_id"]
                logger.info(f"Trade Route: Cargo loaded. Warping to sell sector {target_sector}.")
            # Not at buy sector -> go to buy sector
            elif str(current_sector) != str(route["buy_sector_id"]):
                # We should also not be at the sell sector with cargo
                if not (str(current_sector) == str(route["sell_sector_id"]) and (state.get("ship_info", {}).get("cargo", {}).get(route["commodity"], 0) or 0) > 0):
                    target_sector = route["buy_sector_id"]
                    logger.info(f"Trade Route: Heading to buy sector {target_sector}.")

            if target_sector:
                return {"sector_id": target_sector, "ship_id": ship_id}

        # --- 2. Exploration Navigation (Fallback) ---
        logger.debug("No trade route, or route is completed for now. Falling back to exploration logic.")
        
        if not current_sector:
            logger.error("Cannot warp: current_sector is unknown in state.")
            return None
        
        sector_data_map = state.get("sector_data", {})
        current_sector_data = sector_data_map.get(str(current_sector))
        if not current_sector_data:
            logger.error(f"Cannot warp: No sector_data for current sector {current_sector}.")
            return None
            
        adjacent_sectors = current_sector_data.get("adjacent", [])
        if not adjacent_sectors:
            logger.warning(f"Cannot warp: Sector {current_sector} has no adjacent sectors. Attempting to jump to a random known sector.")
            all_known_sectors = list(state.get("sector_data", {}).keys())
            # Remove current sector from list of possible targets
            all_known_sectors = [s for s in all_known_sectors if int(s) != current_sector]
            if all_known_sectors:
                target_sector = random.choice(all_known_sectors)
                logger.info(f"Jumping to random known sector: {target_sector}")
                return {"sector_id": int(target_sector), "ship_id": ship_id}
            else:
                logger.error("In a dead end and no other sectors are known. Truly stuck.")
                return None
            
        possible_targets = [s for s in adjacent_sectors if s != current_sector]
        if not possible_targets:
            logger.warning(f"No valid warp targets from sector {current_sector}.")
            return None
            
        visited_sectors = set(state.get("visited_sectors", []))
        unvisited_targets = [s for s in possible_targets if s not in visited_sectors]
        
        target_sector = None

        if unvisited_targets:
            unvisited_targets.sort(reverse=True)
            target_sector = unvisited_targets[0]
            logger.info(f"Exploring: Preferring highest *unvisited* sector: {target_sector}")
        else:
            previous_sector = state.get("previous_sector_id")
            fallback_targets = [s for s in possible_targets if s != previous_sector]
            if not fallback_targets:
                fallback_targets = possible_targets
            target_sector = random.choice(fallback_targets)
            logger.info(f"Exploring: All adjacent sectors visited. Picking random valid target: {target_sector}")

        logger.info(f"Warp payload created: Warping to sector {target_sector} with ship {ship_id}.")
        return {"sector_id": target_sector, "ship_id": ship_id}

    def _can_get_trade_quote(self, state, dynamic_cooldown):
        broken_trade_quote = "trade.quote" in [item["command"] for item in state.get("broken_commands", [])]
        if broken_trade_quote:
            return False

        ports = state.get("sector_data", {}).get(str(state.get("player_location_sector")), {}).get("ports", [])
        if not ports:
            return False

        # Don't quote if survey is already complete for this sector/port
        if self._survey_complete(state):
            logger.debug("Can't get trade quote: survey is complete.")
            return False
        
        return True

    def _trade_quote_payload(self, state, dynamic_cooldown):
        current_sector = str(state.get("player_location_sector"))
        ports = state.get("sector_data", {}).get(current_sector, {}).get("ports", [])
        if not ports:
            return None
        
        port_id = ports[0].get("id")
        if not port_id:
            return None

        # Get the list of commodities for this port from the cached port_info
        port_info = state.get("port_info_by_sector", {}).get(current_sector, {}).get(str(port_id))
        if not port_info:
            commodity = random.choice(['ore', 'organics', 'equipment'])
            return {"port_id": port_id, "commodity": commodity, "quantity": 1}

        available_commodities = (port_info.get("port", {}) or port_info).get("commodities", [])
        if not available_commodities:
            commodity = random.choice(['ore', 'organics', 'equipment'])
            return {"port_id": port_id, "commodity": commodity, "quantity": 1}

        # Find the next commodity to survey
        price_cache_for_port = state.get("price_cache", {}).get(current_sector, {}).get(str(port_id), {})
        
        for commodity in available_commodities:
            if commodity not in price_cache_for_port:
                logger.info(f"Systematic survey: Quoting for unsurveyed commodity '{commodity}' at port {port_id}.")
                return {"port_id": port_id, "commodity": commodity, "quantity": 1}
                
        logger.info(f"All commodities at port {port_id} seem to be surveyed.")
        return None

    def _can_buy(self, state, dynamic_cooldown):
        ship = state.get("ship_info")
        if not ship:
            logger.info("Cannot buy: Ship info not available.")
            return False

        holds = ship.get("holds", 0)
        cargo = ship.get("cargo", {})
        current_cargo = sum(cargo.values())
        free_holds = holds - current_cargo
        logger.debug(f"_can_buy debug: ship={ship}, holds={holds}, cargo={cargo}, current_cargo={current_cargo}, free_holds={free_holds}")
        if free_holds <= 0:
            logger.info("Cannot buy: No free cargo holds.")
            return False

        current_credits = state.get("current_credits", 0.0)
        if current_credits <= 0: # Must have some credits to buy
            logger.info("Cannot buy: Insufficient credits.")
            return False

        sector = state.get("player_location_sector")
        price_cache = state.get("price_cache", {})
        sector_cache = price_cache.get(str(sector), {})

        # Check if there are any commodities with a buy price in the current sector
        has_buyable_commodities = False
        for port_id, port_prices in sector_cache.items():
            for commodity, prices in port_prices.items():
                if prices.get("buy") is not None and prices.get("buy") > 0:
                    has_buyable_commodities = True
                    break
            if has_buyable_commodities:
                break
        
        logger.debug(f"_can_buy debug: free_holds={free_holds}, current_credits={current_credits}, sector_cache={sector_cache}, has_buyable_commodities={has_buyable_commodities}")
        if not has_buyable_commodities:
            logger.info(f"Cannot buy: No buyable commodities found in sector {sector}.")
            return False

        return True

    def _buy_payload(self, state, dynamic_cooldown):
        route = state.get("current_trade_route")
        current_sector = state.get("player_location_sector")

        # Prioritize buying based on the current trade route
        if route and str(current_sector) == str(route["buy_sector_id"]):
            ship_info = state.get("ship_info", {})
            holds = ship_info.get("holds", 0)
            cargo = ship_info.get("cargo", {})
            current_cargo = sum(cargo.values())
            free_holds = max(0, holds - current_cargo)
            
            current_credits = state.get("current_credits", 0.0)
            buy_price = route.get("buy_price")

            if free_holds > 0 and buy_price and current_credits >= buy_price:
                # Buy as much as the route suggests, constrained by holds and credits
                max_qty_by_credits = int(current_credits / buy_price) if buy_price > 0 else 0
                quantity_to_buy = min(route["quantity"], free_holds, max_qty_by_credits)
                
                if quantity_to_buy > 0:
                    logger.info(f"Following trade route: buying {quantity_to_buy} of {route['commodity']} at sector {current_sector}.")
                    return {
                        "port_id": route["buy_port_id"],
                        "items": [
                            {
                                "commodity": route["commodity"],
                                "quantity": quantity_to_buy
                            }
                        ]
                    }

        # Fallback to original logic: find the single cheapest commodity in the current sector
        sector_id = state.get("player_location_sector")
        price_cache = state.get("price_cache", {})
        sector_prices = price_cache.get(str(sector_id), {})
        if not sector_prices:
            return None

        cheapest_commodity = None
        cheapest_price = float('inf')
        target_port_id = None

        for port_id_str, port_data in sector_prices.items():
            for commodity, prices in port_data.items():
                buy_price = prices.get("buy")
                if buy_price is not None and buy_price > 0 and buy_price < cheapest_price:
                    cheapest_price = buy_price
                    cheapest_commodity = commodity
                    target_port_id = int(port_id_str)

        if not cheapest_commodity or not target_port_id:
            return None

        ship = state.get("ship_info", {})
        holds = ship.get("holds", 0)
        cargo = ship.get("cargo", {})
        current_cargo = sum(cargo.values())
        free_holds = max(0, holds - current_cargo)
        
        current_credits = state.get("current_credits", 0.0)
        if cheapest_price > current_credits:
            return None # Cannot afford even the cheapest item

        # Buy up to 10 units, or whatever fits and is affordable
        max_qty_by_credits = int(current_credits / cheapest_price) if cheapest_price > 0 else 0
        quantity = min(10, free_holds, max_qty_by_credits)
        if quantity <= 0:
            return None

        logger.info(f"Opportunistic buy: buying {quantity} of cheapest commodity {cheapest_commodity}.")
        return {
            "port_id": target_port_id,
            "items": [
                {
                    "commodity": cheapest_commodity,
                    "quantity": quantity,
                }
            ]
        }

    def _can_sell(self, state, dynamic_cooldown):
        ship_info = state.get("ship_info")
        if not ship_info:
            logger.info("Cannot sell: Ship info not available.")
            return False
        
        cargo = ship_info.get("cargo", {})
        if not any(v > 0 for v in cargo.values()):
            logger.info("Cannot sell: No cargo to sell.")
            return False

        sector = state.get("player_location_sector")
        price_cache = state.get("price_cache", {})
        sector_cache = price_cache.get(str(sector), {})

        # Check if there are any commodities with a sell price in the current sector
        has_sellable_commodities = False
        for port_id, port_prices in sector_cache.items():
            for commodity, prices in port_prices.items():
                if cargo.get(commodity, 0) > 0 and prices.get("sell") is not None and prices.get("sell") > 0:
                    has_sellable_commodities = True
                    break
            if has_sellable_commodities:
                break
        
        logger.debug(f"_can_sell debug: cargo={cargo}, sector_cache={sector_cache}, has_sellable_commodities={has_sellable_commodities}")
        if not has_sellable_commodities:
            logger.info(f"Cannot sell: No sellable commodities found in sector {sector} for current cargo.")
            return False

        return True

    def _sell_payload(self, state, dynamic_cooldown):
        sector = state.get("player_location_sector")
        price_cache = state.get("price_cache", {})
        sector_cache = price_cache.get(str(sector), {})
        if not sector_cache:
            return None

        ship_info = state.get("ship_info", {})
        cargo = ship_info.get("cargo", {})
        if not cargo:
            return None

        holds = ship_info.get("holds", 0)
        current_cargo_total = sum(cargo.values())
        holds_full = current_cargo_total >= holds

        best = None  # (profit_per_unit, port_id, commodity, quantity)

        for port_id_str, port_prices in sector_cache.items():
            port_id = int(port_id_str)
            for commodity, amount in cargo.items():
                if amount <= 0:
                    continue
                prices = port_prices.get(commodity)
                if not prices:
                    continue
                
                sell_price = prices.get("sell")
                not_sellable_until = prices.get("not_sellable_until", 0)

                if sell_price is None or sell_price <= 0 or time.time() < not_sellable_until:
                    logger.debug(f"Skipping {commodity} at port {port_id}: sell_price={sell_price}, not_sellable_until={not_sellable_until}")
                    continue

                # For now profit_per_unit == sell_price; if you later track buy_price, use that.
                profit_per_unit = sell_price
                
                # Prioritize selling if holds are full, even if profit isn't optimal
                if holds_full:
                    # If holds are full, any sell is good to free up space
                    # We'll just take the first valid sell opportunity
                    logger.info(f"Holds full, prioritizing sell of {commodity} at port {port_id} for {sell_price} credits/unit.")
                    return {
                        "port_id": port_id,
                        "items": [
                            {
                                "commodity": commodity,
                                "quantity": amount # Sell all of this commodity
                            }
                        ]
                    }

                if not best or profit_per_unit > best[0]:
                    best = (profit_per_unit, port_id, commodity, amount)

        if not best:
            return None

        _, port_id, commodity, quantity = best

        # Align the payload shape with the server schema here
        return {
            "port_id": port_id,
            "items": [
                {
                    "commodity": commodity,
                    "quantity": quantity
                }
            ]
        }

    def _can_deposit(self, state, dynamic_cooldown):
        # Deposit if petty cash > 5250 + 20% (6300)
        current_credits = state.get("current_credits", 0.0)
        return current_credits > 6300

    def _deposit_payload(self, state, dynamic_cooldown):
        # Deposit all current credits above the threshold
        amount_to_deposit = int(state.get("current_credits", 0.0) - 5250) # Keep 5250 petty cash
        if amount_to_deposit > 0:
            return {"amount": amount_to_deposit}
        return None

    def _can_withdraw(self, state, dynamic_cooldown):
        # Withdraw if petty cash < 2500
        current_credits = state.get("current_credits", 0.0)
        bank_balance = state.get("bank_balance", 0.0) # Assuming bank_balance is stored in state
        return current_credits < 2500 and bank_balance > 0

    def _withdraw_payload(self, state, dynamic_cooldown):
        # Withdraw enough to bring petty cash up to 2500, or all available bank balance
        current_credits = state.get("current_credits", 0.0)
        bank_balance = state.get("bank_balance", 0.0)
        amount_needed = 2500 - current_credits
        
        if amount_needed > 0 and bank_balance > 0:
            amount_to_withdraw = min(int(amount_needed), int(bank_balance))
            if amount_to_withdraw > 0:
                return {"amount": amount_to_withdraw}
        return None

    def _can_bank_balance(self, state, dynamic_cooldown):
        # Only request if bank.balance is MISSING or older than COOLDOWN
        return (time.time() - state.get("last_bank_info_request_time", 0)) > dynamic_cooldown

    def _bank_balance_payload(self, state, dynamic_cooldown):
        return {}
    
    def _can_use_server_pathfinding(self, state) -> bool:
        """Checks if the server-side pathfinding command is available."""
        normalized_commands = state.get("normalized_commands", {})
        cached_schemas = state.get("cached_schemas", {})
        is_available = "move.path_to_sector" in normalized_commands and "move.path_to_sector" in cached_schemas
        logger.debug(f"Server pathfinding (move.path_to_sector) available: {is_available}")
        return is_available

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
                                            "profit": total_profit,
                                            "buy_price": buy_price_per_unit,
                                            "sell_price": sell_price_per_unit
                                        }
        logger.debug(f"_find_best_trade_route: Best route found: {best_route}")
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

    def _survey_complete(self, state):
        sector_id = str(state.get("player_location_sector"))
        logger.debug(f"_survey_complete debug: checking sector_id={sector_id}")
        if not sector_id:
            logger.debug("_survey_complete debug: No sector_id, returning False.")
            return False

        sector_data = state.get("sector_data", {}).get(sector_id)
        logger.debug(f"_survey_complete debug: sector_data={sector_data}")
        if not sector_data or not sector_data.get("ports"):
            logger.debug("_survey_complete debug: No sector_data or no ports, returning True.")
            return True  # Nothing to survey here.

        ports = sector_data.get("ports", [])
        logger.debug(f"_survey_complete debug: ports={ports}")
        if not ports:
            logger.debug("_survey_complete debug: Empty ports list, returning True.")
            return True

        port_id = ports[0].get("id")
        logger.debug(f"_survey_complete debug: port_id={port_id}")
        if not port_id:
            logger.debug("_survey_complete debug: No port_id, returning True.")
            return True

        # Check if we have the port info needed to know what to survey
        port_info = state.get("port_info_by_sector", {}).get(sector_id, {}).get(str(port_id))
        logger.debug(f"_survey_complete debug: port_info={port_info}")
        if not port_info:
            logger.debug("_survey_complete debug: No port_info, returning False.")
            return False # We don't have port info, so survey is not complete

        available_commodities = port_info.get("commodities", [])
        logger.debug(f"_survey_complete debug: available_commodities={available_commodities}")
        if not available_commodities:
            logger.debug("_survey_complete debug: No available_commodities, returning True.")
            return True # Nothing to survey.

        # Check if all available commodities are in the price cache for this port
        price_cache_for_port = state.get("price_cache", {}).get(sector_id, {}).get(str(port_id), {})
        logger.debug(f"_survey_complete debug: price_cache_for_port={price_cache_for_port}")
        
        surveyed_count = 0
        for commodity in available_commodities:
            if commodity in price_cache_for_port:
                surveyed_count += 1
                
        is_complete = surveyed_count >= len(available_commodities)
        logger.debug(f"_survey_complete debug: surveyed_count={surveyed_count}, len(available_commodities)={len(available_commodities)}, is_complete={is_complete}")
        if is_complete and len(available_commodities) > 0:
            logger.info(f"Survey of port {port_id} is complete. All {len(available_commodities)} commodities have been priced.")
        
        return is_complete

    def transition_stage(self, new_stage):
        logger.info(f"Planner: Transitioning from stage '{self.current_stage}' to '{new_stage}'.")
        self.current_stage = new_stage
        self.state_manager.set("stage", new_stage)

    def _select_stage(self, current_state):
        logger.debug(f"_select_stage: player_location_sector from current_state: {current_state.get('player_location_sector')}")
        if not current_state.get("session_token") or not current_state.get("ship_info"):
            return "bootstrap"

        ship_info = current_state.get("ship_info", {})
        holds = ship_info.get("holds", 0)
        cargo_total = sum(ship_info.get("cargo", {}).values())
        holds_full = cargo_total >= holds

        logger.debug(f"_select_stage debug: holds_full={holds_full}, cargo_total={cargo_total}, holds={holds}")

        # If holds are full and we can sell, force exploit stage to prioritize selling
        if holds_full and self._can_sell(current_state, 0):
            logger.info("Holds are full and can sell; forcing 'exploit' stage to prioritize selling.")
            return "exploit"

        current_sector = current_state.get("player_location_sector")
        sector_data = current_state.get("sector_data", {})
        has_port = sector_data.get(str(current_sector), {}).get("ports")

        logger.debug(f"_select_stage debug: current_sector={current_sector}, has_port={has_port}")

        if has_port:
            survey_is_complete = self._survey_complete(current_state)
            logger.debug(f"_select_stage debug: survey_is_complete={survey_is_complete}")
            if not survey_is_complete:
                return "survey"
            else:
                # Survey is complete. Decide if we can trade.
                can_buy = self._can_buy(current_state, 0) # Pass 0 for cooldown
                can_sell = self._can_sell(current_state, 0)
                logger.debug(f"_select_stage debug: can_buy={can_buy}, can_sell={can_sell}")
                if can_buy or can_sell:
                    # Check if trade.buy or trade.sell are broken
                    broken_commands = [c["command"] for c in current_state.get("broken_commands", [])]
                    if "trade.buy" in broken_commands and "trade.sell" in broken_commands:
                        return "explore" # Both trade commands are broken, so can't exploit
                    elif "trade.buy" in broken_commands and not can_sell:
                        return "explore" # Buy is broken and can't sell
                    elif "trade.sell" in broken_commands and not can_buy:
                        return "explore" # Sell is broken and can't buy
                    else:
                        return "exploit"
                else:
                    # Survey is complete, but we can't act on the prices. Explore.
                    return "explore"

        return "explore"

    def get_next_command(self, llm_suggestion=None):
        current_state = self.state_manager.get_all()
        logger.debug(f"get_next_command: player_location_sector from current_state: {current_state.get('player_location_sector')}")

        # If the bot is waiting for turns, do not generate any commands
        if current_state.get("waiting_for_turns"):
            logger.info("Bot is waiting for turns cooldown. No commands will be generated.")
            return None

        # --- Hard invariants first ---

        # 1. If we lost the session, we must go back to bootstrap.
        if not current_state.get("session_token"):
            if self.current_stage != "bootstrap":
                logger.warning("No session_token; forcing stage=bootstrap.")
                self.transition_stage("bootstrap")
            # Let the existing bootstrap block handle the rest
        else:
            # 2. If we have a session but no ship_info, *force* ship.info
            if not current_state.get("ship_info"):
                last = current_state.get("last_ship_info_request_time", 0)
                cooldown = self.config.get("cooldown_seconds", 15)
                if time.time() - last >= cooldown:
                    logger.info("No ship_info in state; forcing ship.info before anything else.")
                    return {"command": "ship.info", "data": {}}
                else:
                    logger.debug("Waiting for ship.info response (still within cooldown).")
                    # continue to maybe send sector.info etc., but do NOT warp
        
        self.current_stage = self._select_stage(current_state)
        self.state_manager.set("stage", self.current_stage)
        logger.info(f"Planner: Current stage is '{self.current_stage}'.")

        if self.current_stage == "exploit":
            best_route = self._find_best_trade_route(current_state)
            self.state_manager.set("current_trade_route", best_route)
            if best_route:
                logger.info(f"Found best trade route with profit {best_route['profit']}.")
        else:
            # Clear the route if not in exploit stage
            self.state_manager.set("current_trade_route", None)

        # After bootstrap logic, before candidate selection
        if self.current_stage in ("exploit", "explore"):
            current_sector = str(current_state.get("player_location_sector"))
            has_port = bool(current_state.get("sector_data", {}).get(current_sector, {}).get("ports"))
            if has_port and current_sector not in current_state.get("port_info_by_sector", {}):
                logger.info("At a port with no port_info for this sector; switching to survey stage.")
                self.transition_stage("survey")

        if self.current_stage == "survey":
            if self._survey_complete(current_state):
                logger.info("Survey complete for sector %s; ready for next stage.",
                            current_state.get("player_location_sector"))
                # Let the next tick's _select_stage decide what to do now that prices are known.

        # Determine dynamic cooldown based on server rate limits
        rate_limit_info = current_state.get("rate_limit_info", {})
        dynamic_cooldown = self.config.get("cooldown_seconds", 15)

        if rate_limit_info:
            remaining = rate_limit_info.get("remaining", 1)
            reset_at = rate_limit_info.get("reset_at", 0)
            if remaining <= 1 and reset_at > time.time():
                dynamic_cooldown = max(dynamic_cooldown, reset_at - time.time() + 1)
                logger.warning(f"Dynamic cooldown adjusted to {dynamic_cooldown:.2f}s due to rate limit.")

        # --- Action Selection ---
        action_defs = self.action_catalogue.get(self.current_stage, [])
        
        if self.current_stage == "bootstrap":
            for action_def in action_defs:
                if action_def["precondition"](current_state):
                    payload = action_def["payload_builder"](current_state)
                    if payload is not None:
                        return {"command": action_def["command"], "data": payload}
            logger.error("Bootstrap: Stuck. No valid actions found.")
            return None

        # Determine if holds are full
        ship_info = current_state.get("ship_info", {})
        holds = ship_info.get("holds", 0)
        cargo_total = sum(ship_info.get("cargo", {}).values())
        holds_full = cargo_total >= holds

        # --- Prioritize selling if holds are full ---
        if holds_full and self._can_sell(current_state, 0):
            logger.info("Holds are full and can sell. Prioritizing 'trade.sell' command.")
            # Create a temporary action_def for trade.sell
            sell_action_def = next((ad for ad in action_defs if ad["command"] == "trade.sell"), None)
            if sell_action_def:
                payload = sell_action_def["payload_builder"](current_state, dynamic_cooldown)
                if payload is not None and self._validate_payload("trade.sell", payload):
                    self.state_manager.set("last_stage", self.current_stage)
                    self.state_manager.set("last_action", "trade.sell")
                    self.state_manager.set("last_context_key", make_context_key(current_state))
                    return {"command": "trade.sell", "data": payload}
                else:
                    logger.warning("Prioritized 'trade.sell' payload generation or validation failed despite holds being full.")
            else:
                logger.error("Could not find 'trade.sell' action definition even though holds are full and can sell.")
            # If for some reason sell fails, proceed to normal action selection, but this should ideally not happen.

        # --- For all other stages and if holds are not full or sell failed ---
        candidates = []
        command_retry_info = current_state.get("command_retry_info", {})
        max_retries = self.config.get("max_retries_per_command", 3)

        for action_def in action_defs:
            command_name = action_def["command"]
            retry_data = command_retry_info.get(command_name, {"failures": 0, "next_retry_time": 0})

            if time.time() < retry_data["next_retry_time"]:
                continue
            
            if retry_data["failures"] >= max_retries:
                if command_name not in GAMEPLAY_COMMANDS:
                    broken_cmds = [c["command"] for c in current_state.get("broken_commands", [])]
                    if command_name not in broken_cmds:
                        logger.warning(
                            f"Command '{command_name}' exceeded max retries ({max_retries}). "
                            f"Reporting as broken."
                        )
                        self.bug_reporter.triage_invariant_failure(
                            "Command Exceeded Max Retries",
                            f"Command '{command_name}' failed {retry_data['failures']} times.",
                            current_state
                        )
                        all_broken = current_state.get("broken_commands", [])
                        all_broken.append({"command": command_name, "error": "Exceeded max retries"})
                        self.state_manager.set("broken_commands", all_broken)
                # Always skip this tick, but *don’t* mark core gameplay commands as permanently broken
                continue

            if action_def["precondition"](current_state, dynamic_cooldown):
                candidates.append(action_def)
        
        logger.info(f"Planner: Candidate actions for stage '{self.current_stage}': {[c['command'] for c in candidates]}")

        # QA Mode: Prioritize focus commands
        if self.config.get("qa_mode"):
            qa_focus_commands = self.config.get("qa_focus_commands", [])
            qa_candidates = [c for c in candidates if c["command"] in qa_focus_commands]
            if qa_candidates:
                logger.info(f"QA Mode: Prioritizing focus commands: {[c['command'] for c in qa_candidates]}")
                candidates = qa_candidates

        if not candidates:
            if self.current_stage == "exploit":
                self.transition_stage("explore")
                return None # Allow the main loop to call get_next_command again in the next tick
            logger.warning(f"No valid actions found for stage '{self.current_stage}'.")
            return None

        # LLM Suggestion or Bandit Policy
        if llm_suggestion:
            normalized_llm_command = current_state.get("normalized_commands", {}).get(llm_suggestion.get("command"), llm_suggestion.get("command"))
            llm_suggestion["command"] = normalized_llm_command
            
            for action_def in candidates:
                if action_def["command"] == normalized_llm_command:
                    if self._validate_payload(normalized_llm_command, llm_suggestion.get("data", {})):
                        return llm_suggestion
                    else:
                        logger.warning(f"LLM suggested invalid payload for {normalized_llm_command}. Falling back to bandit.")
                        break
        
        # Fallback to bandit policy
        candidate_commands = [action_def["command"] for action_def in candidates]
        context_key = make_context_key(current_state)
        
        # Stuck detection
        if self.config.get("qa_mode"):
            history = current_state.get("last_commands_history", [])
            if len(history) >= 20:
                last_20_commands = history[-20:]
                if len(set(last_20_commands)) == 1:
                    command = last_20_commands[0]
                    self.bug_reporter.report_bug(
                        bug_type="Agent Stuck",
                        description=f"Agent repeated {command} 20 times in a row.",
                        reproducer={"history": last_20_commands},
                        frames=[], # no frames available
                        server_caps=current_state.get("server_capabilities", {}),
                        agent_state=current_state,
                        last_commands_history=history,
                        last_responses_history=self.state_manager.get("last_responses_history", []),
                        replay_commands=last_20_commands,
                    )

        for _ in range(len(candidates)):
            chosen_command_name = self.bandit_policy.choose_action(candidate_commands, context_key)
            chosen_action_def = next((c for c in candidates if c["command"] == chosen_command_name), None)

            if chosen_action_def:
                payload = chosen_action_def["payload_builder"](current_state, dynamic_cooldown)
                if payload is not None and self._validate_payload(chosen_command_name, payload):
                    self.state_manager.set("last_stage", self.current_stage)
                    self.state_manager.set("last_action", chosen_command_name)
                    self.state_manager.set("last_context_key", context_key)
                    return {"command": chosen_command_name, "data": payload}
                
                if chosen_command_name in candidate_commands:
                    candidate_commands.remove(chosen_command_name)
        
        logger.error(f"All candidate actions for stage '{self.current_stage}' failed payload generation or validation.")
        return None
