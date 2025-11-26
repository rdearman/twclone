import json
import os
import time
import logging


logger = logging.getLogger(__name__)

class StateManager:
    def __init__(self, state_file, config):
        self.state_file = state_file
        self.config = config
        self.state = {
            "session_id": None,
            "player_info": None,
            "ship_info": {"cargo": []},
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
                    self.state["port_trade_blacklist"] = [] # NEW: Clear port blacklist on load
                    self.state["warp_blacklist"] = [] # NEW: Clear warp blacklist on load
                    
                    # --- ADD THESE LINES TO CLEAR CACHES ---
                    logger.warning("Clearing cached world data to force re-exploration.")
                    self.state["sector_data"] = {}
                    self.state["port_info_by_sector"] = {}
                    self.state["price_cache"] = {}
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

    # --- Command Retry & Cooldown Logic ---

    def record_command_sent(self, command_name):
        """Updates the cooldown for a command that was just sent."""
        info = self.state["command_retry_info"].get(command_name, {})
        cooldown_sec = self.config.get("default_cooldown", 5)
        
        info["next_retry_time"] = time.time() + cooldown_sec
        self.state["command_retry_info"][command_name] = info
        # No need to save state here, it'll be saved on response

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

    # --- State Updaters ---

    def update_sector_data(self, sector_id, data):
        """Updates the cache for a specific sector and marks it as explored in the universe_map."""
        if 'sector_data' not in self.state:
            self.state['sector_data'] = {}
        self.state['sector_data'][str(sector_id)] = data

        if 'universe_map' not in self.state:
            self.state['universe_map'] = {}
        
        if str(sector_id) not in self.state['universe_map']:
            self.state['universe_map'][str(sector_id)] = {}
        
        self.state['universe_map'][str(sector_id)]['is_explored'] = True
        self.state['universe_map'][str(sector_id)]['has_port'] = data.get('has_port', False)
        # We can add more cached info here later if needed

        self.save_state()

    def update_port_info(self, sector_id, port_data):
        """Updates the cache for a specific port, keyed by sector ID."""
        if 'port_info_by_sector' not in self.state:
            self.state['port_info_by_sector'] = {}
        # We only care about one port per sector for this bot
        self.state['port_info_by_sector'][str(sector_id)] = {
            "port_id": port_data.get("id"),
            "name": port_data.get("name"),
            "class": port_data.get("class"),
            "commodities": port_data.get("commodities", []) # Store commodities
        }
        self.save_state()

    def update_price_cache(self, quote_data):
        """Updates the price cache from a trade.quote response."""
        port_id = str(quote_data.get("port_id"))
        if not port_id:
            return

        if port_id not in self.state["price_cache"]:
            self.state["price_cache"][port_id] = {"buy": {}, "sell": {}}
            
        commodity = quote_data.get("commodity")
        if not commodity:
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

        price_info = self.state.get("price_cache", {}).get(str(port_id), {}).get("buy", {})
        
        for item in items_bought:
            commodity = item.get("commodity")
            quantity = item.get("quantity")
            purchase_price = item.get("unit_price") # GET UNIT_PRICE FROM THE RECEIPT ITEM DIRECTLY!

            if not all([commodity, quantity, purchase_price is not None]):
                logger.warning(f"Could not log purchase for {item}, missing data (commodity, quantity, or unit_price).")
                continue

            # Check if we already have a batch of this commodity at the same price
            found = False
            for cargo_item in self.state['ship_info']['cargo']:
                if cargo_item.get('commodity') == commodity and cargo_item.get('purchase_price') == purchase_price:
                    cargo_item['quantity'] += quantity
                    found = True
                    break
            
            if not found:
                self.state['ship_info']['cargo'].append({
                    "commodity": commodity,
                    "quantity": quantity,
                    "purchase_price": purchase_price
                })
        logger.info(f"Cargo updated after buy: {self.state['ship_info']['cargo']}")
        self.save_state()

    def update_cargo_after_sell(self, items_sold, port_id):
        """Updates cargo list after a successful sell and returns total profit."""
        if 'ship_info' not in self.state or 'cargo' not in self.state['ship_info']:
            return 0

        total_profit = 0
        port_sell_prices = self.state.get("price_cache", {}).get(str(port_id), {}).get("sell", {})

        for item_sold in items_sold:
            commodity_sold = item_sold.get("commodity")
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
        if 'player' in player_data:
            if self.state['player_info'] is None:
                self.state['player_info'] = {}
            self.state['player_info']['player'] = player_data['player']
            logger.info(f"Player info updated. Turns remaining: {player_data['player'].get('turns_remaining')}")

        if 'ship' in player_data:
            if self.state.get('ship_info') is None:
                self.state['ship_info'] = {}

            # Preserve the new cargo structure
            existing_cargo = self.state['ship_info'].get('cargo', [])
            
            # Update ship_info with new data, then restore cargo
            self.state['ship_info'] = player_data['ship']
            self.state['ship_info']['cargo'] = existing_cargo

            # NEW: Always update player_location_sector from the authoritative ship data
            if 'location' in player_data['ship'] and 'sector_id' in player_data['ship']['location']:
                new_sector = player_data['ship']['location']['sector_id']
                self.state['player_location_sector'] = new_sector
                # Append to recent sectors, keeping only the last 10
                self.state['recent_sectors'] = (self.state.get('recent_sectors', []) + [new_sector])[-10:]
                logger.info(f"Player location updated to sector {new_sector} (from ship_info)")

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

    def validate_state(self) -> bool:
        """
        Checks the current state for logical inconsistencies.
        Returns True if state is consistent, False otherwise.
        """
        is_consistent = True

        # 1. Cargo Validation
        ship_info = self.state.get("ship_info")
        if ship_info:
            cargo_list = ship_info.get("cargo", [])
            max_holds = ship_info.get("holds", 0)
            current_cargo_sum = 0

            for item in cargo_list:
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
                logger.warning(f"State inconsistency: No sector_data for current player sector {player_location_sector}.")
                is_consistent = False
        
        return is_consistent
