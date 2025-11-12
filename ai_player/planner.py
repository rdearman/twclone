import logging
import time
import random
import json

logger = logging.getLogger(__name__)

class FiniteStatePlanner:
    def __init__(self, state_manager, game_connection, config):
        self.state_manager = state_manager
        self.game_connection = game_connection
        self.config = config
        self.current_stage = self.state_manager.get("stage", "bootstrap")
        self.cooldown_seconds = self.config.get("COOLDOWN_SECONDS", 5)
        self.max_port_info_failures = self.config.get("MAX_PORT_INFO_FAILURES", 3)
        self.action_catalogue = self._build_action_catalogue()

    def _build_action_catalogue(self):
        return {
            "bootstrap": [
                {"command": "auth.login", "precondition": lambda s: not s.get("session_token"), "payload_builder": self._login_payload},
                {"command": "system.describe_schema", "precondition": lambda s: not s.get("cached_schemas"), "payload_builder": self._describe_schema_payload},
                {"command": "system.cmd_list", "precondition": lambda s: s.get("session_token") and not s.get("commands_to_try") and not s.get("pending_commands"), "payload_builder": lambda s: {}},
            ],
            "survey": [
                {"command": "ship.info", "precondition": lambda s: (time.time() - s.get("last_ship_info_request_time", 0)) > self.cooldown_seconds, "payload_builder": lambda s: {}},
                {"command": "sector.info", "precondition": lambda s: (time.time() - s.get("last_sector_info_request_time", 0)) > self.cooldown_seconds, "payload_builder": lambda s: {}},
                {"command": "trade.port_info", "precondition": self._can_get_port_info, "payload_builder": self._port_info_payload},
            ],
            "explore": [
                {"command": "move.warp", "precondition": self._can_warp, "payload_builder": self._warp_payload},
            ],
            "exploit": [
                {"command": "trade.buy", "precondition": self._can_buy, "payload_builder": self._buy_payload},
                {"command": "trade.sell", "precondition": self._can_sell, "payload_builder": self._sell_payload},
                {"command": "bank.deposit", "precondition": self._can_deposit, "payload_builder": self._deposit_payload},
                {"command": "bank.withdraw", "precondition": self._can_withdraw, "payload_builder": self._withdraw_payload},
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

    def _can_warp(self, state):
        # Check if there are adjacent sectors and enough fuel/turns (placeholder for now)
        last_server_response = state.get("last_server_response", {})
        adjacent_sectors = last_server_response.get("data", {}).get("adjacent", [])
        return bool(adjacent_sectors)

    def _warp_payload(self, state):
        current_sector = state.get("player_location_sector", 1)
        last_server_response = state.get("last_server_response", {})
        adjacent_sectors = last_server_response.get("data", {}).get("adjacent", [])
        
        if adjacent_sectors:
            # Pick a random adjacent sector that isn't the current one
            possible_targets = [s for s in adjacent_sectors if s != current_sector]
            if possible_targets:
                return {"sector_id": random.choice(possible_targets)}
            elif adjacent_sectors: # If only current sector is adjacent, or no other options
                return {"sector_id": random.choice(adjacent_sectors)} # Fallback to any adjacent
        return None # Cannot warp

    def _can_get_port_info(self, state):
        current_sector = state.get("player_location_sector", 1)
        last_server_response = state.get("last_server_response", {})
        ports_in_sector = last_server_response.get("data", {}).get("ports", [])
        
        # Check if trade.port_info is broken
        if "trade.port_info" in [item["command"] for item in state.get("broken_commands", [])]:
            return False
        
        # Check for persistent failures in this sector
        current_sector_failures = state.get("port_info_failures_per_sector", {}).get(current_sector, 0)
        if current_sector_failures >= self.max_port_info_failures:
            return False

        # Check if port_info is available and fresh enough
        return bool(ports_in_sector) and \
               ((time.time() - state.get("port_info_fetched_at", 0)) > self.cooldown_seconds or \
                state.get("port_info", {}).get("sector") != current_sector)

    def _port_info_payload(self, state):
        current_sector = state.get("player_location_sector", 1)
        last_server_response = state.get("last_server_response", {})
        ports_in_sector = last_server_response.get("data", {}).get("ports", [])
        if ports_in_sector:
            return {"port_id": ports_in_sector[0].get("id")}
        return None

    def _can_buy(self, state):
        # Placeholder for buy preconditions (credits, cargo space, port info)
        return False

    def _buy_payload(self, state):
        # Placeholder for buy payload
        return {}

    def _can_sell(self, state):
        # Placeholder for sell preconditions (cargo, port info)
        return False

    def _sell_payload(self, state):
        # Placeholder for sell payload
        return {}

    def _can_deposit(self, state):
        # Placeholder for deposit preconditions
        return False

    def _deposit_payload(self, state):
        # Placeholder for deposit payload
        return {}

    def _can_withdraw(self, state):
        # Placeholder for withdraw preconditions
        return False

    def _withdraw_payload(self, state):
        # Placeholder for withdraw payload
        return {}

    def transition_stage(self, new_stage):
        logger.info(f"Transitioning from stage '{self.current_stage}' to '{new_stage}'")
        self.current_stage = new_stage
        self.state_manager.set("stage", new_stage)

    def get_next_command(self, llm_suggestion=None):
        current_state = self.state_manager.get_all()
        
        # Stage Transition Logic
        if self.current_stage == "bootstrap":
            if current_state.get("session_token") and current_state.get("cached_schemas"):
                self.transition_stage("survey")
            else:
                # If not logged in or schemas not cached, try bootstrap actions
                for action_def in self.action_catalogue["bootstrap"]:
                    if action_def["precondition"](current_state):
                        payload = action_def["payload_builder"](current_state)
                        if payload is not None:
                            return {"command": action_def["command"], "data": payload}
                logger.error("Bootstrap: Stuck. Cannot login or cache schemas.")
                return None # Cannot proceed

        if self.current_stage == "survey":
            # Check if basic info is gathered and fresh
            ship_info_fresh = (time.time() - current_state.get("last_ship_info_request_time", 0)) <= self.cooldown_seconds and current_state.get("ship_info")
            sector_info_fresh = (time.time() - current_state.get("last_sector_info_request_time", 0)) <= self.cooldown_seconds and current_state.get("sector_info_fetched_for", {}).get(current_state.get("player_location_sector"))
            
            # If trade.port_info is broken, we can't rely on it for survey completion
            port_info_broken = "trade.port_info" in [item["command"] for item in current_state.get("broken_commands", [])]
            port_info_available = current_state.get("port_info") and current_state.get("port_info", {}).get("sector") == current_state.get("player_location_sector")

            if ship_info_fresh and sector_info_fresh and (port_info_available or port_info_broken):
                self.transition_stage("exploit") # Or explore if no ports/trade opportunities
            
        # For other stages, try LLM suggestion first, then bandit/deterministic
        candidates = [a for a in self.action_catalogue.get(self.current_stage, []) if a["precondition"](current_state)]
        
        if llm_suggestion:
            # Check if LLM suggestion is a valid and allowed action for the current stage
            for action_def in candidates:
                if action_def["command"] == llm_suggestion.get("command"):
                    # For now, we'll trust the LLM's payload if the command is valid
                    # In future, add schema validation here
                    return llm_suggestion # Return LLM's suggestion directly

        # Fallback to deterministic/bandit selection if no valid LLM suggestion or no LLM suggestion
        if candidates:
            # For now, just pick a random valid action. Bandit logic will go here later.
            chosen_action_def = random.choice(candidates)
            payload = chosen_action_def["payload_builder"](current_state)
            if payload is not None:
                return {"command": chosen_action_def["command"], "data": payload}
        
        logger.warning(f"No valid actions found for stage '{self.current_stage}'. Stalling.")
        return None
