"""Bot-side estimates for V3 turn-consuming commands.

These estimates do not alter server rules. They reserve turns so an AI player
can keep a configured budget for higher priority goals.
"""

TURN_COSTS = {
    "move.warp": 1,
    "move.transwarp": 1,
    "sector.scan": 1,
    "move.scan": 1,
    "move.scan": 1,
    "planet.land": 1,
    "trade.buy": 1,
    "trade.sell": 1,
    "port.rob": 1,
    "trade.jettison": 1,
    "combat.flee": 1,
    "combat.attack.port": 1,
}


def command_turn_cost(command: str) -> int:
    """Return a conservative known cost; unknown/read-only commands cost zero."""
    return TURN_COSTS.get(str(command or ""), 0)


def command_fits_turn_budget(command, turns_remaining, reserve=0, priority=False):
    """Never send a turn-costing action without at least its required turns."""
    try:
        turns = max(0, int(turns_remaining))
        keep = max(0, int(reserve))
    except (TypeError, ValueError):
        return False
    cost = command_turn_cost(command)
    if cost and turns < cost:
        return False
    return not cost or bool(priority) or turns - cost >= keep


def turn_refresh_due(last_poll, now, interval=30):
    """Whether the bot should request an authoritative reset-state refresh."""
    try:
        return float(now) - float(last_poll or 0) >= max(1.0, float(interval))
    except (TypeError, ValueError):
        return True


def session_heartbeat_interval(config):
    """Return a client heartbeat interval safely below the server frame deadline.

    The server uses its request deadline while waiting for the next complete
    line-framed request.  A client heartbeat must arrive before that deadline;
    it is not merely an authentication-session refresh interval.
    """
    config = config or {}
    try:
        requested = float(config.get("session_heartbeat_interval_seconds", 4.0))
    except (TypeError, ValueError):
        requested = 4.0
    requested = max(1.0, requested)
    try:
        timeout_ms = float(config.get("server_request_timeout_ms", 0) or 0)
    except (TypeError, ValueError):
        timeout_ms = 0
    if timeout_ms > 0:
        # Leave headroom for scheduling jitter and inference contention.
        requested = min(requested, max(1.0, timeout_ms / 1000.0 * 0.4))
    return requested


def goal_turn_cost(goal: str, state: dict, path_finder=None) -> int:
    """Estimate cost of one goal; a route is charged by its known hop count."""
    if not isinstance(goal, str) or ":" not in goal:
        return 0
    kind, target = (part.strip().lower() for part in goal.split(":", 1))
    if kind == "goto":
        try:
            destination = int(target)
            current = int(state.get("player_location_sector"))
        except (TypeError, ValueError):
            return 1
        if current == destination:
            return 0
        path = path_finder(current, destination) if callable(path_finder) else None
        if isinstance(path, list) and len(path) > 1:
            return len(path) - 1
        return 1
    if kind in {"buy", "sell", "scan"}:
        return 1
    if kind == "planet" and target == "land":
        return 1
    if kind == "combat" and target == "attack":
        return 1
    return 0


def affordable_plan(plan, state, turns_remaining, reserve, priority_prefixes=(), path_finder=None):
    """Keep the longest plan that fits above reserve; priority goals may use it."""
    try:
        turns = max(0, int(turns_remaining))
        reserve = max(0, int(reserve))
    except (TypeError, ValueError):
        return list(plan or [])
    selected = []
    spent = 0
    for goal in plan or []:
        cost = goal_turn_cost(goal, state, path_finder)
        priority = bool(priority_prefixes) and str(goal).startswith(tuple(priority_prefixes))
        available = turns if priority else max(0, turns - reserve)
        if spent + cost > available:
            break
        selected.append(goal)
        spent += cost
    return selected
