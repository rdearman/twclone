"""
state.py — Client/session state for the TWClone Python client.

`Context` holds everything about the current session that survives across
menu renders: the connection, the menu stack, the last-known (authoritative)
sector description, player/capability info, and a free-form `state` dict used
by handlers for transient flags (e.g. corp membership, docked-port access).
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

from protocol import Conn
from events import EventLog
from hud import HudState


@dataclass
class Context:
    conn: Conn
    menus: Dict[str, Any]
    menu_stack: List[str] = field(default_factory=lambda: ["MAIN"])
    last_sector_desc: Dict[str, Any] = field(default_factory=dict)
    player_info: Dict[str, Any] = field(default_factory=dict)
    state: Dict[str, Any] = field(default_factory=dict)
    capabilities: Dict[str, Any] = field(default_factory=dict)
    hud: HudState = field(default_factory=HudState)
    event_log: EventLog = field(default_factory=EventLog)

    @property
    def current_menu(self) -> str:
        return self.menu_stack[-1] if self.menu_stack else "MAIN"

    @property
    def current_sector_id(self) -> Optional[int]:
        return self.last_sector_desc.get("id")

    def push(self, menu_id: str):
        self.menu_stack.append(menu_id)

    def pop(self):
        if len(self.menu_stack) > 1:
            self.menu_stack.pop()

    def drain_events(self) -> int:
        """
        Move any events queued on the transport connection into the
        long-lived event log. Safe to call at UI boundaries only (never
        while a prompt is mid-read). Returns the number of events ingested.
        """
        drained = self.conn.events.drain()
        if not drained:
            return 0
        return self.event_log.ingest(drained)

    @property
    def activity_count(self) -> Optional[int]:
        """
        Compact combined "unread activity" indicator for the HUD: mail +
        notices (both safely pollable) plus unread queued/logged events.
        Unread news is a login-time snapshot only (see hud.py) and is not
        continuously refreshable, so it is not folded into this live count.
        Returns None (unavailable) only if nothing is known yet.
        """
        parts = [self.hud.unread_mail, self.hud.unread_notices]
        known = [p for p in parts if p is not None]
        total = sum(known) + self.event_log.unread_count
        if not known and self.event_log.unread_count == 0:
            return None
        return total
