import json
import os
import time
import logging
from collections import deque

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
            "port_info_by_sector": {},  # Caches for port.info
            "price_cache": {},          # Caches for trade.quote
            "bank_balance": 0,
            "command_retry_info": {},   # Tracks failures/cooldowns
            "strategy_plan": [],        # NEW: For multi-stage plans
            "trade_successful": False,  # For bandit reward,
            "q_table": {},              # Persist learning
            "n_table": {}               # Persist learning
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
        """Updates the cache for a specific sector."""
        if 'sector_data' not in self.state:
            self.state['sector_data'] = {}
        self.state['sector_data'][str(sector_id)] = data
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
