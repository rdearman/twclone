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
        return f"[{label}] {data.get('message') or etype}"
    if category == "combat":
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
