- [DONE] Remove `ai_player.log`.
- [DONE] Fix `_can_buy` logic in `planner.py`.
- [DONE] Add `move.warp` as a fallback in `exploit` stage in `planner.py`.
- [DONE] Add `jsonschema` to `requirements.txt`.
- [DONE] Fix Schema Cache Shape vs. Validator Mismatch in `main.py` and `planner.py`.
- [DONE] Include `system.cmd_list` in cooldown logic by refining its precondition in `planner.py`.
- [DONE] Fix Credits Updates: Event/Type Mismatch in `main.py`.
- [DONE] Fix Robust Offline Protocol Parser in `parse_protocol.py`.
- [DONE] Re-fix Bootstrap Exit Criteria to ensure all schemas are fetched (`schemas_to_fetch` is empty).
- [DONE] Re-fix Bootstrap Schema Discovery:
    a. [DONE] `main.py` - `StateManager.__init__`: Add `self.state["schemas_to_fetch"] = []`.
    b. [DONE] `main.py` - `process_responses`: Modify to populate `schemas_to_fetch` when `system.cmd_list` response is received.
    c. [DONE] `planner.py` - `_build_action_catalogue`: Modify `bootstrap` stage to correctly sequence schema discovery (`auth.login`, `system.cmd_list`, then `system.describe_schema` for items in `schemas_to_fetch`).
    d. [DONE] `planner.py` - New method `_describe_next_schema_payload`.
- [DONE] Update `main.py` (`StateManager.__init__`, `process_responses`) and `planner.py` (`_can_get_trade_quote`, `_trade_quote_payload`, `_buy_payload`, `_sell_payload`) to consistently use `sector_data` for port information and cargo holds calculation.
- [DONE] Remove `bank.info`, `bank.deposit`, `bank.withdraw` related actions and preconditions from planner due to server not supporting explicit bank balance queries.
- [DONE] **Verify Phase 1.1:** User to run the AI player again to confirm:
    a. Successful bootstrap completion (all schemas fetched, `schemas_to_fetch` is empty).
    b. Transition to `survey` stage.
    c. No `system.describe_schema` loop.
    d. `cached_schemas` and `normalized_commands` are correctly populated.
    e. `trade.quote` executed successfully.
    f. `price_cache` is populated.
    g. Transition to `exploit` stage.
    h. AI attempts `trade.buy`/`trade.sell` (if possible given 0 initial credits/cargo) or `move.warp`.
- [DONE] Proceed with Phase 2: Enhanced Learning & Decision Making.
    a. [DONE] Implement Intrinsic Rewards in `main.py`.
    b. [DONE] Implement Contextual Bandit Keys in `bandit_policy.py` and `main.py`.
    c. [DONE] Implement Persistent Learning/Q-table Storage.
- [DONE] Proceed with Phase 3: Advanced Trading & Exploration Strategies.
    a. [DONE] Implement Strategic Trade Route Planning (incorporating remembering trade prices at ports).
    b. [DONE] Add Survey explore fallback.
    c. [DONE] Better Price Cache & Value Function.
    d. [DONE] Adaptive Exploration Strategy.
- [DONE] Proceed with Phase 4: Robustness & Debugging.
    a. [DONE] Enforce Single In-Flight at the Sender.
    b. [DONE] Three-Frame Repro in Bug Reports.
    c. [DONE] Dynamic Cooldowns based on Server Rate Limits.
    d. [DONE] Error Handling and Retries with Backoff.
        i. [DONE] Add `command_retry_info = {}` to `StateManager.__init__` in `main.py`.
        ii. [DONE] Ensure `command_retry_info` is loaded correctly in `StateManager.load` in `main.py`.
        iii. [DONE] Modify `process_responses` in `main.py` to update `command_retry_info` on command failure (increment failures, calculate `next_retry_time` with exponential backoff) and reset on success.
        iv. [DONE] Modify `planner.py`'s `get_next_command` to respect `command_retry_info["next_retry_time"]` when filtering candidates.
    e. [DONE] More Granular State Management for Preconditions.
    f. [DONE] LLM Prompt Engineering for Better Command Generation.
    g. [DONE] Normalise Command Names Early and Everywhere.
- [DONE] Fix `AttributeError: 'FiniteStatePlanner' object has no attribute 'cooldown_seconds'`.
- [DONE] Fix `ship_info_fresh` and `sector_info_fresh` not being boolean values.