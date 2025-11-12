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
