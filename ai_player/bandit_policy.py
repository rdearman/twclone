import random
import math
import logging

logger = logging.getLogger(__name__)

class BanditPolicy:
    def __init__(self, epsilon=0.1, q_table=None, n_table=None):
        self.epsilon = epsilon  # Exploration-exploitation trade-off
        # Q-values (estimated rewards) - now nested by context
        self.q_table = q_table if q_table is not None else {}  
        # N-values (number of times action taken) - now nested by context
        self.n_table = n_table if n_table is not None else {}  

    def choose_action(self, actions: list[str], context_key: str) -> str:
        """
        Chooses an action from the given list of actions using an epsilon-greedy policy,
        within a specific context.
        
        Args:
            actions: A list of available action identifiers (strings).
            context_key: A string representing the current context.
        
        Returns:
            The chosen action identifier.
        """
        if not actions:
            return None

        # Get or initialize Q and N tables for the current context
        context_q_table = self.q_table.setdefault(context_key, {})
        context_n_table = self.n_table.setdefault(context_key, {})

        # Initialize Q and N for new actions within this context
        for action in actions:
            if action not in context_q_table:
                context_q_table[action] = 0.0
            if action not in context_n_table:
                context_n_table[action] = 0

        # Epsilon-greedy exploration
        if random.random() < self.epsilon:
            # Explore: choose a random action
            chosen_action = random.choice(actions)
            logger.debug(f"Bandit: Exploring in context '{context_key}', chose {chosen_action}")
            return chosen_action
        else:
            # Exploit: choose the action with the highest Q-value
            # Break ties randomly
            max_q_value = -float('inf')
            best_actions = []

            for action in actions:
                q_value = context_q_table.get(action, 0.0)
                if q_value > max_q_value:
                    max_q_value = q_value
                    best_actions = [action]
                elif q_value == max_q_value:
                    best_actions.append(action)
            
            chosen_action = random.choice(best_actions)
            logger.debug(f"Bandit: Exploiting in context '{context_key}', chose {chosen_action} (Q={max_q_value})")
            return chosen_action

    def update_q_value(self, action: str, reward: float, context_key: str):
        """
        Updates the Q-value for a chosen action based on the received reward,
        within a specific context. Uses an incremental update rule.
        
        Args:
            action: The action that was taken.
            reward: The reward received for taking the action.
            context_key: A string representing the current context.
        """
        # Get or initialize Q and N tables for the current context
        context_q_table = self.q_table.setdefault(context_key, {})
        context_n_table = self.n_table.setdefault(context_key, {})

        if action not in context_n_table:
            context_n_table[action] = 0
        if action not in context_q_table:
            context_q_table[action] = 0.0

        context_n_table[action] += 1
        alpha = 1.0 / context_n_table[action]  # Learning rate (1/N)
        context_q_table[action] += alpha * (reward - context_q_table[action])
        logger.debug(f"Bandit: Updated Q for {action} in context '{context_key}' to {context_q_table[action]} (N={context_n_table[action]}) with reward {reward}")

    def get_tables(self):
        return self.q_table, self.n_table

    def set_tables(self, q_table, n_table):
        self.q_table = q_table
        self.n_table = n_table

def make_context_key(state: dict, config: dict) -> str:
    """Generates a context key for the bandit policy based on the current state and config."""
    stage = state.get("stage", "bootstrap")
    sector_id = state.get("player_location_sector", 1)
    
    sector_data = state.get("sector_data", {}).get(str(sector_id), {})
    sector_class = sector_data.get("class", "unknown_class")

    port_type = "no_port"
    port_id_str = ""
    if sector_data.get("ports"):
        port_type = "port_present"
        # If there are ports, try to get the ID of the first one
        first_port = sector_data["ports"][0]
        if first_port and first_port.get("id"):
            port_id_str = f"-port:{first_port['id']}"

    ship_info = state.get("ship_info", {})
    holds = ship_info.get("holds", 0)
    cargo_total = sum(ship_info.get("cargo", {}).values()) if ship_info.get("cargo") else 0
    holds_full = "full" if cargo_total >= holds and holds > 0 else "not_full"

    credits = state.get("current_credits", 0)
    credits_bucket = "low"
    if credits > 10000:
        credits_bucket = "medium"
    if credits > 100000:
        credits_bucket = "high"

    qa_mode = config.get("qa_mode", False) # Use the passed config

    # New context elements
    can_sell_any = "can_sell" if _can_sell_any(state, config) else "cannot_sell"
    can_buy_any = "can_buy" if _can_buy_any(state, config) else "cannot_buy"
    has_bank_balance = "has_bank_balance" if state.get("bank_balance", 0) > 0 else "no_bank_balance"
    has_pending_commands = "pending_cmds" if len(state.get("pending_commands", {})) > 0 else "no_pending_cmds"

    return (
        f"stage:{stage}-sector_class:{sector_class}-port_type:{port_type}{port_id_str}-"
        f"holds_full:{holds_full}-credits:{credits_bucket}-qa_mode:{qa_mode}-"
        f"can_sell_any:{can_sell_any}-can_buy_any:{can_buy_any}-has_bank_balance:{has_bank_balance}-"
        f"has_pending_commands:{has_pending_commands}"
    )

# Helper functions for _can_sell_any and _can_buy_any, mirroring planner logic
def _can_sell_any(state: dict, config: dict) -> bool:
    ship_info = state.get("ship_info")
    if not ship_info:
        return False
    cargo = ship_info.get("cargo", {})
    if not any(v > 0 for v in cargo.values()):
        return False
    sector = state.get("player_location_sector")
    price_cache = state.get("price_cache", {})
    sector_cache = price_cache.get(str(sector), {})
    for port_id, port_prices in sector_cache.items():
        for commodity, prices in port_prices.items():
            if cargo.get(commodity, 0) > 0 and prices.get("sell") is not None and prices.get("sell") > 0:
                return True
    return False

def _can_buy_any(state: dict, config: dict) -> bool:
    ship = state.get("ship_info")
    if not ship:
        return False
    holds = ship.get("holds", 0)
    cargo = ship.get("cargo", {})
    current_cargo = sum(cargo.values())
    free_holds = holds - current_cargo
    if free_holds <= 0:
        return False
    current_credits = state.get("current_credits", 0.0)
    if current_credits <= 0:
        return False
    sector = state.get("player_location_sector")
    price_cache = state.get("price_cache", {})
    sector_cache = price_cache.get(str(sector), {})
    for port_id, port_prices in sector_cache.items():
        for commodity, prices in port_prices.items():
            if prices.get("buy") is not None and prices.get("buy") > 0:
                return True
    return False

    # --- UCB1 (Upper Confidence Bound 1) - Future Enhancement ---
    def choose_action_ucb1(self, actions: list[str], context_key: str, total_plays: int) -> str:
        """
        Chooses an action using the UCB1 algorithm within a specific context.
        Requires total_plays (sum of all N-values for the context) for the exploration term.
        
        Args:
            actions: A list of available action identifiers (strings).
            context_key: A string representing the current context.
            total_plays: The total number of times any action has been taken in this context.
        
        Returns:
            The chosen action identifier.
        """
        if not actions:
            return None

        context_q_table = self.q_table.setdefault(context_key, {})
        context_n_table = self.n_table.setdefault(context_key, {})

        # Initialize Q and N for new actions within this context
        for action in actions:
            if action not in context_q_table:
                context_q_table[action] = 0.0
            if action not in context_n_table:
                context_n_table[action] = 0

        # Play each action once if not played yet
        for action in actions:
            if context_n_table[action] == 0:
                logger.debug(f"Bandit UCB1: Playing unplayed action {action} in context '{context_key}'")
                return action

        # Calculate UCB1 values
        ucb_values = {}
        for action in actions:
            exploitation_term = context_q_table[action]
            exploration_term = math.sqrt(2 * math.log(total_plays) / context_n_table[action])
            ucb_values[action] = exploitation_term + exploration_term
        
        # Choose action with highest UCB1 value
        max_ucb_value = -float('inf')
        best_actions = []
        for action, ucb_value in ucb_values.items():
            if ucb_value > max_ucb_value:
                max_ucb_value = ucb_value
                best_actions.append(action)
        
        chosen_action = random.choice(best_actions)
        logger.debug(f"Bandit UCB1: Chose {chosen_action} in context '{context_key}' (UCB={max_ucb_value})")
        return chosen_action
