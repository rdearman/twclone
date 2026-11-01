"""
events.py — Client-side event log for unsolicited server events.

`Conn` (protocol.py) queues raw classified events without rendering them.
`EventLog` owns the drained, presentation-ready history: a bounded in-memory
buffer plus an "unread" counter that is cleared by *reading* the log, never
by the mere act of receiving more events.

This module has no dependency on menus or RPC — it only turns queued event
dicts into stable, player-facing text and tracks read/unread state.
"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from typing import Any, Dict, List


def targets_player_ship_destruction(event: Dict[str, Any], player_id: int) -> bool:
    """Return true only for a server event naming this session's victim.

    The event is a reconciliation trigger, not a death-result contract.  The
    subsequent authoritative player/ship/sector reads provide the escape-pod,
    cargo, credits, and location facts.
    """
    if type(player_id) is not int or player_id <= 0 or not isinstance(event, dict):
        return False
    event_type = event.get("type")
    if event_type not in ("ship.destroyed", "combat.ship_destroyed"):
        return False
    data = event.get("data")
    if not isinstance(data, dict):
        return False
    victim_key = "player_id" if event_type == "ship.destroyed" else "victim_player_id"
    victim_id = data.get(victim_key)
    return type(victim_id) is int and victim_id == player_id

# Player-facing renderers per category. Keep these terse and free of raw
# JSON/implementation field names; unknown events get a safe, compact,
# generic representation instead of a JSON dump.
_CATEGORY_LABELS = {
    "system": "Notice",
    "chat": "Comms",
    "nav": "Navigation",
    "combat": "Combat",
    "trade": "Trade",
    "connection": "Connection",
    "unknown": "Event",
}


def render_event_text(ev: Dict[str, Any], debug: bool = False) -> str:
    """
    Render one queued event as a single player-facing line. Recognised
    categories get a short, human-readable summary; unknown events get a
    safe compact tag in normal mode, and their raw type/data in debug mode
    only (never during normal play).
    """
    category = ev.get("category", "unknown")
    etype = ev.get("type") or "event"
    data = ev.get("data") or {}
    label = _CATEGORY_LABELS.get(category, "Event")

    if category == "system":
        title = data.get("title") or "System Notice"
        body = data.get("body") or ""
        return f"[{label}] {title}: {body}".rstrip(": ")
    if category == "chat":
        sender = data.get("sender_name") or data.get("from") or data.get("username")
        text = data.get("message") or data.get("body") or data.get("subject") or ""
        if sender:
            return f"[{label}] {sender}: {text}"
        return f"[{label}] {text or etype}"
    if category == "nav":
        if etype == "sector.notice":
            subtype = data.get("subtype")
            labels = {
                "docked": "Ship docked at a port",
                "beacon_set": "Sector beacon set",
                "beacon_destroyed": "Sector beacon destroyed",
                "fighters_deployed": "Sector fighters deployed",
                "mine_swept": "Sector mines swept",
            }
            summary = labels.get(subtype, "Sector notice")
            sector_id = data.get("sector_id")
            if isinstance(sector_id, int) and not isinstance(sector_id, bool) and sector_id > 0:
                summary += f" in sector {sector_id}"
            details = data.get("details")
            if isinstance(details, dict):
                if subtype == "docked" and isinstance(details.get("port_id"), int):
                    summary += f" (port {details['port_id']})"
                elif subtype == "beacon_set" and isinstance(details.get("beacon"), str):
                    summary += f": {details['beacon']}"
                elif subtype == "fighters_deployed" and isinstance(details.get("count"), int):
                    summary += f" ({details['count']})"
                elif subtype == "mine_swept" and isinstance(details.get("mines_removed"), int):
                    summary += f" ({details['mines_removed']} removed)"
            return f"[{label}] {summary}"
        return f"[{label}] {data.get('message') or etype}"
    if category == "combat":
        if etype == "player.attack_planet.v1":
            summary = "Planet attack"
            planet_id = data.get("planet_id")
            result = data.get("result")
            ship_loss = data.get("ship_loss")
            planet_loss = data.get("planet_loss")
            if isinstance(planet_id, int) and not isinstance(planet_id, bool) and planet_id > 0:
                summary += f" · planet {planet_id}"
            if isinstance(result, str) and result.strip():
                summary += f" · result: {result.strip()}"
            if isinstance(ship_loss, int) and not isinstance(ship_loss, bool) and ship_loss >= 0:
                summary += f" · ship fighters lost: {ship_loss}"
            if isinstance(planet_loss, int) and not isinstance(planet_loss, bool) and planet_loss >= 0:
                summary += f" · planet fighters lost: {planet_loss}"
            return f"[{label}] {summary}"
        if etype == "player.capture_planet.v1":
            summary = "Planet captured"
            planet_id = data.get("planet_id")
            previous_owner = data.get("previous_owner")
            if isinstance(planet_id, int) and not isinstance(planet_id, bool) and planet_id > 0:
                summary += f" · planet {planet_id}"
            if isinstance(previous_owner, int) and not isinstance(previous_owner, bool) and previous_owner > 0:
                summary += f" · previous owner {previous_owner}"
            return f"[{label}] {summary}"
        if etype == "combat.hit":
            weapon = data.get("weapon")
            summary = f"{weapon} hit" if isinstance(weapon, str) and weapon.strip() else "Ship hit"
            damage = data.get("damage")
            hp_after = data.get("defender_hp_after")
            sector_id = data.get("sector_id")
            if isinstance(damage, int) and not isinstance(damage, bool):
                summary += f" · {damage} damage"
            if isinstance(hp_after, int) and not isinstance(hp_after, bool):
                summary += f" · target HP {hp_after}"
            if isinstance(sector_id, int) and not isinstance(sector_id, bool) and sector_id > 0:
                summary += f" · sector {sector_id}"
            return f"[{label}] {summary}"
        if etype in {"combat.hit.fighters", "combat.hit.quasar", "combat.hit.environment",
                     "combat.limpet.attached", "combat.planet_landing_defense.v1"}:
            # These are durable server defense events. Present only fields
            # supplied by the event; do not derive destruction/depletion or
            # substitute a client-side combat result.
            if etype == "combat.planet_landing_defense.v1":
                result = data.get("result")
                summary = "Planetary defense"
                if isinstance(result, str) and result.strip():
                    summary += f" · {result.strip()}"
                # The server event is durable and contains the post-resolution
                # counts. Display those facts without deriving capture,
                # depletion, or destruction on the client.
                for key, text in (
                    ("attacker_fighters_lost", "attacker fighters lost"),
                    ("attacker_fighters_remaining", "attacker fighters remaining"),
                    ("planet_fighters_lost", "planet fighters lost"),
                    ("planet_fighters_remaining", "planet fighters remaining"),
                    ("citadel_shields_absorbed", "citadel shields absorbed"),
                    ("citadel_shields_remaining", "citadel shields remaining"),
                ):
                    value = data.get(key)
                    if isinstance(value, int) and not isinstance(value, bool):
                        summary += f" · {text}: {value}"
            elif etype == "combat.hit.fighters":
                summary = "Defensive fighters engaged"
                if isinstance(data.get("fighters_engaged"), int) and not isinstance(data["fighters_engaged"], bool):
                    summary += f" · {data['fighters_engaged']} engaged"
            elif etype == "combat.hit.quasar":
                summary = str(data.get("source") or "Atmospheric defense")
            elif etype == "combat.limpet.attached":
                summary = "Limpet attached"
            else:
                hazard = data.get("hazard_type", data.get("type"))
                summary = f"Environmental hazard · {hazard}" if isinstance(hazard, str) and hazard.strip() else "Environmental hazard"
            for key, text in (("damage", "damage"), ("shields_lost", "shields lost"),
                              ("fighters_lost", "fighters lost"), ("hull_lost", "hull lost")):
                value = data.get(key)
                if isinstance(value, int) and not isinstance(value, bool):
                    summary += f" · {text}: {value}"
            if data.get("destroyed") is True:
                summary += " · ship destroyed"
            return f"[{label}] {summary}"
        return f"[{label}] {data.get('message') or etype}"
    if category == "trade":
        return f"[{label}] {data.get('message') or etype}"
    if category == "connection":
        return f"[{label}] {data.get('reason') or etype}"

    # Unknown category: safe compact representation only. Full data is
    # retained on the entry for debug-mode inspection but never dumped here.
    if debug:
        return f"[{label}:unknown] {etype} {data!r}"
    return f"[{label}:unknown] {etype}"


@dataclass
class EventLog:
    """
    Bounded recent history of drained events, with a read/unread boundary.

    Reading the log (`mark_read`) clears the *unread counter*, not the
    underlying history — the recent entries remain inspectable afterwards.
    """
    maxlen: int = 500
    _entries: deque = field(default_factory=lambda: deque(maxlen=500))
    _unread: int = 0

    def __post_init__(self):
        # Recreate the deque with the requested maxlen (dataclass default
        # factory above uses a fixed literal for typing simplicity).
        self._entries = deque(self._entries, maxlen=self.maxlen)

    def ingest(self, drained: List[Dict[str, Any]]) -> int:
        """Append newly drained events (oldest first); returns count added."""
        for ev in drained:
            self._entries.append(ev)
        self._unread += len(drained)
        return len(drained)

    @property
    def unread_count(self) -> int:
        return self._unread

    def mark_read(self) -> None:
        self._unread = 0

    def recent(self, limit: int = 50) -> List[Dict[str, Any]]:
        items = list(self._entries)[-limit:]
        return items

    def __len__(self) -> int:
        return len(self._entries)
