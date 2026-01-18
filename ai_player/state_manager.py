import json
import os
import time
import logging
from helpers import canon_commodity


logger = logging.getLogger(__name__)

class StateManager:
    def __init__(self, state_file, config):
        self.state_file = state_file
        self.config = config
        self.state = {
            "session_id": None,
            "player_info": None,
            "ship_info": None,
            "player_location_sector": None,
            "sector_data": {},          # Caches for sector.info
            "universe_map": {},         # NEW: For systematic exploration
            "port_info_by_sector": {},  # Caches for port.info
            "price_cache": {},          # Caches for trade.quote
            "bank_balance": 0,
            "command_retry_info": {},   # Tracks failures/cooldowns
            "strategy_plan": [],        # NEW: For multi-stage plans
            "trade_successful": False,  # For bandit reward,
            "q_table": {},              # Persist learning
            "n_table": {},              # Persist learning
            "command_schemas": {},      # NEW: Cache for command schemas
            "schema_blacklist": [],     # NEW: List of commands that failed schema fetching
            "pending_schema_requests": {}, # NEW: Map of schema request ID to command name
            "recent_sectors": [],       # NEW: To avoid ping-ponging
            "pending_commands": {},     # NEW: To track sent commands for error correlation
            "port_trade_blacklist": [], # NEW: List of ports that failed on trade
            "warp_blacklist": [],       # NEW: List of sectors that are unreachable via warp
            "current_path": [],         # NEW: Stores the server-provided path for navigation
            "last_action_result": None, # NEW: Feedback loop for LLM
            "last_quotes": {},          # NEW: Deterministic trade rule support
            "command_sent_in_response_loop": False, # NEW: To prevent double-sending
        }
        self.load_state()

    def load_state(self):
        """Loads the last saved state from the state file."""
        try:
            if os.path.exists(self.state_file):
                with open(self.state_file, 'r') as f:
                    # Merge saved state with defaults
                    saved_state = json.load(f)
                    self.state.update(saved_state)
                    logger.info("Loaded state from %s", self.state_file)
                    
                    # --- THIS IS THE FIX ---
                    # ***CRITICAL: Reset transient and cached data on load***
                    self.state["strategy_plan"] = []
                    self.state["command_retry_info"] = {} 
                    self.state["session_id"] = None # Always force re-login
                    self.state["command_schemas"] = {} # Always force schema refetch
                    self.state["schema_blacklist"] = [] # NEW: Clear schema blacklist on load
                    self.state["pending_schema_requests"] = {} # NEW: Clear pending schema requests on load
                    self.state["recent_sectors"] = [] # NEW: Clear recent sectors on load
                    self.state["pending_commands"] = {} # NEW: Clear pending commands on load
                    # self.state["port_trade_blacklist"] = [] # NEW: Clear port blacklist on load -- KEEP THIS TO AVOID BAD PORTS PERSISTING
                    # self.state["warp_blacklist"] = [] # NEW: Clear warp blacklist on load -- KEEP THIS TO AVOID BAD WARPS PERSISTING
                    self.state["current_path"] = [] # NEW: Clear current path on load
                    self.state["last_action_result"] = None # NEW: Clear feedback on load
                    
                    # --- ADD THESE LINES TO CLEAR CACHES & FORCE BOOTSTRAP ---
                    logger.warning("Clearing cached world data to force re-exploration.")
                    # self.state["sector_data"] = {} -- KEEP THIS, PART OF UNIVERSE_MAP
                    # self.state["port_info_by_sector"] = {} -- KEEP THIS, PART OF MARKET KNOWLEDGE
                    # self.state["price_cache"] = {} -- KEEP THIS, PART OF MARKET KNOWLEDGE
                    
                    # Force re-fetch of player state
                    self.state["player_info"] = None
                    self.state["ship_info"] = None
                    self.state["player_location_sector"] = None
                    self.state["needs_bootstrap"] = True
                    # --- END FIX ---

        except json.JSONDecodeError:
            logger.error("Failed to decode state file. Starting with fresh state.")
            self.save_state() # Save a fresh, valid file
        except Exception as e:
            logger.error(f"Error loading state: {e}. Starting fresh.")
            # Don't save here, just use default state

    def save_state(self):
        """Saves the current state to the state file."""
        try:
            with open(self.state_file, 'w') as f:
                json.dump(self.state, f, indent=4)
        except IOError as e:
            logger.error(f"Failed to save state to %s: %s", self.state_file, e)

    def get(self, key, default=None):
        """Gets a value from the state."""
        return self.state.get(key, default)

    def get_all(self):
        """Returns a copy of the entire state dictionary."""
        return self.state.copy()

    def set(self, key, value):
        """Sets a value in the state."""
        self.state[key] = value
        # For simplicity, we save on every set.
        # For performance, you might want to save less often.
        self.save_state()

    def set_default(self, key, default_value):
        """Sets a value in the state only if it doesn't exist."""
        if key not in self.state:
            self.state[key] = default_value

    def set_last_action_result(self, result_dict):
        """Updates the result of the last attempted action for LLM feedback."""
        self.state["last_action_result"] = result_dict
        self.save_state()

    def set_turns(self, turns):
        """Updates the turns_remaining in player_info."""
        if self.state["player_info"] and "player" in self.state["player_info"]:
            self.state["player_info"]["player"]["turns_remaining"] = turns
            logger.info(f"Updated turns_remaining to {turns}.")
            self.save_state()

    def update_last_quote(self, port_id, commodity, price, timestamp):
        """Stores the most recent successful quote details."""
        if "last_quotes" not in self.state:
            self.state["last_quotes"] = {}
        
        key = f"{port_id}_{commodity}"
        self.state["last_quotes"][key] = {
            "port_id": port_id,
            "commodity": commodity,
            "unit_price": price,
            "timestamp": timestamp
        }
        self.save_state()

    # --- Command Retry & Cooldown Logic ---

    def record_command_sent(self, command_name):
        """Updates the cooldown for a command that was just sent."""
        info = self.state["command_retry_info"].get(command_name, {})
        cooldown_sec = self.config.get("default_cooldown", 5)
        
        info["next_retry_time"] = time.time() + cooldown_sec
        self.state["command_retry_info"][command_name] = info
        # No need to save state here, it'll be saved on response

    def record_command_success(self, command_name):
        """Resets failure count for a command that succeeded."""
        if command_name in self.state["command_retry_info"]:
            self.state["command_retry_info"][command_name]["failures"] = 0
            self.save_state()

    def record_command_failure(self, command_name):
        """Increments failure count and sets a longer cooldown."""
        info = self.state["command_retry_info"].get(command_name, {})
        failures = info.get("failures", 0) + 1
        
        info["failures"] = failures
        
        # Implement exponential backoff
        backoff_time = self.config.get("default_cooldown", 5) * (2 ** failures)
        backoff_time = min(backoff_time, 300) # Cap at 5 minutes
        
        info["next_retry_time"] = time.time() + backoff_time
        
        self.state["command_retry_info"][command_name] = info
        logger.warning("Command %s failed %d time(s). Backing off for %d sec.",
                     command_name, failures, backoff_time)
        self.save_state()

    def get_valid_goto_sectors(self):
        """Return a list of sector_ids that are adjacent to the current sector and not warp-blacklisted."""
        current_sector = self.state.get("player_location_sector")
        if current_sector is None:
            return []
            
        sector_data = self.state.get("sector_data", {}).get(str(current_sector), {})
        adj = sector_data.get("adjacent_sectors") or sector_data.get("adjacent") or []
        warp_blacklist = set(self.state.get("warp_blacklist", []))
        
        # adj contains dicts like {"to_sector": 2}, extract the sector IDs
        adjacent_sectors = [s["to_sector"] if isinstance(s, dict) else s for s in adj]
        return [s for s in adjacent_sectors if s not in warp_blacklist]

    def find_path(self, from_sector, to_sector):
        """
        Finds the shortest path between from_sector and to_sector using BFS on cached sector data.
        Returns a list of sector IDs [from, ..., to] or None if no path found.
        """
        start = str(from_sector)
        goal = str(to_sector)
        
        if start == goal:
            return [from_sector]
            
        queue = [(start, [from_sector])]
        visited = {start}
        
        while queue:
            current, path = queue.pop(0)
            sector_data = self.state.get("sector_data", {}).get(current, {})
            adjacent = sector_data.get("adjacent_sectors") or sector_data.get("adjacent") or []
            
            for neighbor in adjacent:
                # Extract sector ID from dict or use directly if int
                neighbor_id = neighbor["to_sector"] if isinstance(neighbor, dict) else neighbor
                neighbor_str = str(neighbor_id)
                if neighbor_str == goal:
                    return path + [neighbor_id]
                
                if neighbor_str not in visited:
                    visited.add(neighbor_str)
                    # Only traverse if we have data for the neighbor? 
                    # Actually, for navigation, we assume adjacency is bidirectional/valid 
                    # even if we haven't visited the neighbor yet.
                    queue.append((neighbor_str, path + [neighbor_id]))
                    
        return None

    # --- State Updaters ---

    def update_sector_data(self, sector_id, data):
        """Updates the cache for a specific sector and marks it as explored in the universe_map."""
        if 'sector_data' not in self.state:
            self.state['sector_data'] = {}
        
        enriched_data = dict(data)
        
        # Fallback for has_port check
        ports = enriched_data.get('ports')
        if 'has_port' not in enriched_data and ports is not None:
            enriched_data['has_port'] = len(ports) > 0
            
        # Fallback for has_planet check
        planets = enriched_data.get('celestial_objects') or enriched_data.get('planets')
        if 'has_planet' not in enriched_data and planets is not None:
            enriched_data['has_planet'] = len(planets) > 0
        
        # Normalize adjacent_sectors to adjacent (Unconditional)
        if 'adjacent_sectors' in enriched_data:
            enriched_data['adjacent'] = enriched_data['adjacent_sectors']
            logger.info(f"Normalized adjacent_sectors for sector {sector_id}. Keys: {enriched_data.keys()}")

        self.state['sector_data'][str(sector_id)] = enriched_data
        if 'universe_map' not in self.state:
            self.state['universe_map'] = {}
        
        if str(sector_id) not in self.state['universe_map']:
            self.state['universe_map'][str(sector_id)] = {}
        
        self.state['universe_map'][str(sector_id)]['is_explored'] = True
        self.state['universe_map'][str(sector_id)]['has_port'] = enriched_data.get('has_port', False)

        self.save_state()

    def update_port_info(self, sector_id, port_data):
        """Updates the cache for a specific port, keyed by sector ID."""
        if 'port_info_by_sector' not in self.state:
            self.state['port_info_by_sector'] = {}
        
        # Construct commodities list from flat fields
        commodities = []
        if "ore_on_hand" in port_data:
            commodities.append({"commodity": "ORE", "supply": port_data["ore_on_hand"]})
        if "organics_on_hand" in port_data:
            commodities.append({"commodity": "ORG", "supply": port_data["organics_on_hand"]})
        if "equipment_on_hand" in port_data:
            commodities.append({"commodity": "EQU", "supply": port_data["equipment_on_hand"]})
        
        # Also handle generic 'commodities' list if provided by server in that format
        if "commodities" in port_data:
            for c in port_data["commodities"]:
                # If it's a dict like {"commodity": "ore", ...}
                if isinstance(c, dict):
                    c_code = canon_commodity(c.get("commodity") or c.get("symbol") or c.get("code"))
                    if c_code:
                        # Update or append
                        existing = next((x for x in commodities if x["commodity"] == c_code), None)
                        if existing:
                            existing.update(c)
                            existing["commodity"] = c_code # Ensure canonical
                        else:
                            c["commodity"] = c_code
                            commodities.append(c)
        
        # We only care about one port per sector for this bot
        self.state['port_info_by_sector'][str(sector_id)] = {
            "port_id": port_data.get("id"),
            "name": port_data.get("name"),
            "class": port_data.get("class"),
            "commodities": commodities 
        }
        self.save_state()

    def update_price_cache(self, quote_data):
        """Updates the price cache from a trade.quote response."""
        port_id = str(quote_data.get("port_id"))
        if not port_id:
            return

        if port_id not in self.state["price_cache"]:
            self.state["price_cache"][port_id] = {"buy": {}, "sell": {}}
            
        raw_commodity = quote_data.get("commodity")
        if not raw_commodity:
            return

        # NORMALISE commodity keys (server uses ORE/ORG/EQU)
        commodity = canon_commodity(raw_commodity)
        if not commodity:
            logger.warning(f"Ignoring quote for unknown commodity: {raw_commodity}")
            return
        
        # Update price for the specific commodity
        self.state["price_cache"][port_id]["buy"][commodity] = quote_data.get("buy_price")
        self.state["price_cache"][port_id]["sell"][commodity] = quote_data.get("sell_price")
            
        self.save_state()

    def update_cargo_after_buy(self, items_bought, port_id):
        """Updates cargo list after a successful buy, including purchase price."""
        if 'ship_info' not in self.state or self.state['ship_info'] is None:
            self.state['ship_info'] = {'cargo': []}
        
        if 'cargo' not in self.state['ship_info'] or not isinstance(self.state['ship_info']['cargo'], list):
            self.state['ship_info']['cargo'] = []

        # We don't need to look up price cache for unit_price if it is in items_bought
        # But we might want to normalize keys if we did.
        
        for item in items_bought:
            commodity = canon_commodity(item.get("commodity"))
            # Note: trade.buy_receipt_v1 uses "units" not "quantity"!
            quantity = item.get("units") or item.get("quantity")
            # Note: trade.buy_receipt_v1 uses "price_per_unit" not "unit_price"!
            purchase_price = item.get("price_per_unit") or item.get("unit_price")

            if not all([commodity, quantity, purchase_price is not None]):
                logger.warning(f"Could not log purchase for {item}, missing data (commodity, quantity, or unit_price).")
                continue

            # Check if we already have a batch of this commodity at the same price and origin
            found = False
            for cargo_item in self.state['ship_info']['cargo']:
                if (cargo_item.get('commodity') == commodity and 
                    cargo_item.get('purchase_price') == purchase_price and
                    cargo_item.get('origin_port_id') == port_id):
                    cargo_item['quantity'] += quantity
                    found = True
                    break
            
            if not found:
                self.state['ship_info']['cargo'].append({
                    "commodity": commodity,
                    "quantity": quantity,
                    "purchase_price": purchase_price,
                    "origin_port_id": port_id
                })
        logger.info(f"Cargo updated after buy: {self.state['ship_info']['cargo']}")
        self.save_state()

    def update_cargo_after_sell(self, items_sold, port_id):
        """Updates cargo list after a successful sell and returns total profit."""
        if 'ship_info' not in self.state or self.state['ship_info'] is None or 'cargo' not in self.state['ship_info']:
            return 0

        total_profit = 0
        port_sell_prices = self.state.get("price_cache", {}).get(str(port_id), {}).get("sell", {})

        for item_sold in items_sold:
            commodity_sold = canon_commodity(item_sold.get("commodity"))
            quantity_sold = item_sold.get("quantity")
            sell_price = port_sell_prices.get(commodity_sold)

            if not all([commodity_sold, quantity_sold, sell_price is not None]):
                continue

            # Find the corresponding batches in cargo to calculate profit
            new_cargo_list = []
            quantity_to_remove = quantity_sold
            
            # Create a list of items that don't match the commodity being sold
            other_items = [item for item in self.state['ship_info']['cargo'] if item.get('commodity') != commodity_sold]

            # Sort matching items by purchase price to sell cheapest first (for profit calculation)
            matching_items = sorted([
                item for item in self.state['ship_info']['cargo'] 
                if item.get('commodity') == commodity_sold and item.get('purchase_price') is not None
            ], key=lambda x: x['purchase_price'])

            for cargo_item in matching_items:
                if quantity_to_remove > 0:
                    purchase_price = cargo_item.get("purchase_price")
                    qty_from_this_batch = min(quantity_to_remove, cargo_item.get("quantity", 0))
                    
                    # Log profit based on batch purchase price
                    profit = (sell_price - purchase_price) * qty_from_this_batch
                    total_profit += profit

                    # Decrement the quantity of the batch
                    cargo_item['quantity'] -= qty_from_this_batch
                    quantity_to_remove -= qty_from_this_batch

                if cargo_item['quantity'] > 0:
                    new_cargo_list.append(cargo_item)
            
            # Combine the remaining parts of sold commodity with the other items
            self.state['ship_info']['cargo'] = other_items + new_cargo_list

        logger.info(f"Cargo updated after sell. Profit: {total_profit}. New cargo: {self.state['ship_info']['cargo']}")
        self.save_state()
        return total_profit


    def update_player_info(self, player_data):
        """Updates the player_info and ship_info in the state, preserving detailed cargo."""
        
        # Robustly handle both wrapped {"player": {...}} and flat structures
        p_obj = player_data.get('player')
        if p_obj is None and ('username' in player_data or 'credits' in player_data):
            p_obj = player_data

        if p_obj:
            if self.state['player_info'] is None:
                self.state['player_info'] = {}
            
            # If we already have a player object, merge it to preserve missing fields
            if self.state['player_info'].get('player'):
                # Filter out None values to avoid overwriting valid data
                clean_p_obj = {k: v for k, v in p_obj.items() if v is not None}
                self.state['player_info']['player'].update(clean_p_obj)
            else:
                self.state['player_info']['player'] = p_obj
            
            # Update location from player info
            if 'sector' in p_obj:
                new_sector = p_obj['sector']
                # FIX: Only update if we don't have a location yet. 
                # Ship location is authoritative; Player location might be stale/home.
                if self.state['player_location_sector'] is None:
                    self.state['player_location_sector'] = new_sector
                    self.state['recent_sectors'] = (self.state.get('recent_sectors', []) + [new_sector])[-10:]
                    logger.info(f"Player location initialized to sector {new_sector} (from player_info)")
                elif self.state['player_location_sector'] != new_sector:
                    logger.debug(f"Ignoring player_info sector {new_sector} as we are already at {self.state['player_location_sector']}")

            turns_str = f" Turns remaining: {p_obj.get('turns_remaining')}" if p_obj.get('turns_remaining') is not None else ""
            logger.info(f"Player info updated.{turns_str}")

        if 'ship' in player_data:
            if self.state.get('ship_info') is None:
                self.state['ship_info'] = {}

            # CRITICAL FIX: Trust server cargo if provided, otherwise preserve local
            server_cargo = player_data['ship'].get('cargo')
            local_cargo = (self.state.get('ship_info') or {}).get('cargo', [])
            
            self.state['ship_info'] = player_data['ship']
            
            if server_cargo is not None:
                # FIX: Verify server_cargo is valid (list of dicts). 
                # If server sends ["ORE"] (strings), IGNORE IT to prevent corruption.
                if server_cargo and isinstance(server_cargo, list) and len(server_cargo) > 0 and isinstance(server_cargo[0], str):
                    logger.warning("Ignored simplified cargo list (strings) from server. Preserving local state.")
                    self.state['ship_info']['cargo'] = local_cargo
                else:
                    self.state['ship_info']['cargo'] = server_cargo
            else:
                self.state['ship_info']['cargo'] = local_cargo

            # Update location from ship info (authoritative GPS)
            if 'location' in player_data['ship'] and 'sector_id' in player_data['ship']['location']:
                new_sector = player_data['ship']['location']['sector_id']
                self.state['player_location_sector'] = new_sector
                self.state['recent_sectors'] = (self.state.get('recent_sectors', []) + [new_sector])[-10:]
                logger.info(f"Location updated to sector {new_sector} (Ship GPS)")

        self.save_state()

    # --- Schema Management ---

    def add_schema(self, command_name, schema):

        self.state["command_schemas"][command_name] = schema
        logger.debug(f"Added schema for {command_name}: {json.dumps(schema)}")
        self.save_state()

    def get_schema(self, command_name):
        """Retrieves a command schema from the cache."""
        return self.state.get('command_schemas', {}).get(command_name)

    def add_to_schema_blacklist(self, command_name):
        """Adds a command to the schema blacklist."""
        if command_name not in self.state["schema_blacklist"]:
            self.state["schema_blacklist"].append(command_name)
            logger.info(f"Added '{command_name}' to schema blacklist.")
            self.save_state()

    # --- Pending Command and Blacklist Management ---

    def add_pending_command(self, request_id, command):
        """Adds a sent command to the pending dictionary."""
        self.state["pending_commands"][request_id] = command
        # This state is transient, so we don't save to disk.

    def get_pending_command(self, request_id):
        """Retrieves a pending command by its ID."""
        return self.state.get("pending_commands", {}).get(request_id)

    def remove_pending_command(self, request_id):
        """Removes a command from the pending dictionary."""
        if request_id in self.state.get("pending_commands", {}):
            del self.state["pending_commands"][request_id]

    def add_to_port_trade_blacklist(self, port_id):
        """Adds a port to the trade blacklist."""
        if port_id not in self.state["port_trade_blacklist"]:
            self.state["port_trade_blacklist"].append(port_id)
            logger.info(f"Added port {port_id} to trade blacklist.")
            self.save_state()

    def add_to_warp_blacklist(self, sector_id):
        """Adds a sector to the warp blacklist."""
        if sector_id not in self.state["warp_blacklist"]:
            self.state["warp_blacklist"].append(sector_id)
            logger.info(f"Added sector {sector_id} to warp blacklist.")
            self.save_state()

    def record_trade_failure(self, port_id, command_name, error_code, error_message):
        """Records a trade command failure and updates blacklists as appropriate."""
        logger.warning(f"Trade command '{command_name}' failed at port {port_id} with code {error_code}: {error_message}")
        
        # Add to port_trade_blacklist for specific errors
        if error_code == 1701: # Port is not buying this commodity right now.
            logger.info(f"Adding port {port_id} to trade blacklist due to error 1701.")
            self.add_to_port_trade_blacklist(port_id)
        elif error_code == 1405: # Port is full of this commodity.
            logger.info(f"Adding port {port_id} to trade blacklist due to error 1405.")
            self.add_to_port_trade_blacklist(port_id)
        
        # Call the generic command failure recorder for cooldowns
        self.record_command_failure(command_name)

    def validate_state(self) -> bool:
        """
        Checks the current state for logical inconsistencies.
        Returns True if state is consistent, False otherwise.
        """
        is_consistent = True

        # 1. Cargo Validation
        ship_info = self.state.get("ship_info")
        if ship_info:
            cargo_data = ship_info.get("cargo", [])
            max_holds = ship_info.get("holds", 0)
            current_cargo_sum = 0

            if isinstance(cargo_data, dict):
                # Handle raw server format {commodity: quantity}
                for commodity, quantity in cargo_data.items():
                    if quantity < 0:
                        logger.warning(f"State inconsistency: Negative quantity ({quantity}) for commodity '{commodity}' in cargo.")
                        is_consistent = False
                    current_cargo_sum += quantity
            elif isinstance(cargo_data, list):
                # Handle internal format [{"commodity":..., "quantity":...}]
                for item in cargo_data:
                    if not isinstance(item, dict): continue
                    commodity = item.get("commodity")
                    quantity = item.get("quantity", 0)
                    
                    if quantity < 0:
                        logger.warning(f"State inconsistency: Negative quantity ({quantity}) for commodity '{commodity}' in cargo.")
                        is_consistent = False
                    current_cargo_sum += quantity
            
            if max_holds > 0 and current_cargo_sum > max_holds:
                logger.warning(f"State inconsistency: Cargo ({current_cargo_sum}) exceeds ship holds ({max_holds}).")
                is_consistent = False

        # 2. Credits Validation
        player_info = self.state.get("player_info")
        if player_info:
            player_credits_str = player_info.get("player", {}).get("credits", "0")
            try:
                player_credits = float(player_credits_str)
                if player_credits < 0:
                    logger.warning(f"State inconsistency: Player has negative credits ({player_credits}).")
                    is_consistent = False
            except (ValueError, TypeError):
                logger.warning(f"State inconsistency: Player credits '{player_credits_str}' is not a valid number.")
                is_consistent = False

        bank_balance = self.state.get("bank_balance", 0)
        if bank_balance < 0:
            logger.warning(f"State inconsistency: Bank balance is negative ({bank_balance}).")
            is_consistent = False

        # 3. Location/Sector Data Validation
        player_location_sector = self.state.get("player_location_sector")
        if player_location_sector is not None:
            if not isinstance(player_location_sector, int) or player_location_sector < 1:
                logger.warning(f"State inconsistency: Invalid player_location_sector: {player_location_sector}.")
                is_consistent = False
            
            current_sector_data = self.state.get("sector_data", {}).get(str(player_location_sector))
            if not current_sector_data:
                # RELAXATION: Do not mark as inconsistent if sector_data is missing. 
                # This often happens during transitions (login/warp) before sector.info arrives.
                logger.debug(f"Validation: sector_data for current sector {player_location_sector} is not yet available.")
                # is_consistent = False 
        
        return is_consistent
