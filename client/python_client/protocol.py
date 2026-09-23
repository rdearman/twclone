"""
protocol.py — Server connection and envelope helpers for the TWClone Python client.

Contains the socket-level RPC connection (`Conn`) and small, pure helpers for
interpreting the shape of server response envelopes (`get_data`,
`extract_current_sector`, `normalize_sector`). These are protocol-facing and
have no dependency on menus, handlers, or Context/session state.

`Conn` never renders server content: unsolicited frames (events, broadcasts,
notices) are classified and pushed onto a bounded, thread-safe `EventQueue`
(`Conn.events`) instead of being printed. Presentation code (client.py /
hud.py) is responsible for draining that queue at safe UI boundaries.
"""
from __future__ import annotations

import json
import socket
import threading
import time
from collections import deque
from typing import Any, Dict, List, Optional

# Event categories used to classify unsolicited server frames. This is a
# closed, presentation-agnostic vocabulary; unrecognised type prefixes map to
# "unknown" but are still retained (never discarded outright).
EVENT_CATEGORIES = (
    "system", "chat", "nav", "combat", "trade", "connection", "unknown",
)

_CATEGORY_PREFIXES = (
    ("system.notice", "system"),
    ("system.", "system"),
    ("chat.", "chat"),
    ("comm.", "chat"),
    ("mail.", "chat"),
    ("move.", "nav"),
    ("nav.", "nav"),
    ("sector.", "nav"),
    ("combat.", "combat"),
    ("attack.", "combat"),
    ("trade.", "trade"),
    ("market.", "trade"),
    ("connection.", "connection"),
    ("client.", "connection"),
)


def classify_event_type(event_type: Optional[str]) -> str:
    """Map a raw server/client event `type` string to a stable category."""
    if not event_type:
        return "unknown"
    for prefix, category in _CATEGORY_PREFIXES:
        if event_type.startswith(prefix):
            return category
    return "unknown"


class EventQueue:
    """
    Thread-safe, bounded FIFO queue of unsolicited events.

    Overflow policy: when the queue is full, the oldest entry is dropped to
    make room for the newest one (drop-oldest), and `dropped` is incremented
    so callers can surface "N events were dropped" if they wish. Order of the
    entries that remain is always preserved (oldest first).
    """

    def __init__(self, maxlen: int = 200):
        self._dq: deque = deque()
        self._maxlen = maxlen
        self._lock = threading.Lock()
        self._dropped = 0
        self._seq = 0

    def push(self, category: str, event_type: str, data: Dict[str, Any]) -> Dict[str, Any]:
        with self._lock:
            self._seq += 1
            ev = {
                "seq": self._seq,
                "ts": time.time(),
                "category": category,
                "type": event_type,
                "data": data,
            }
            self._dq.append(ev)
            while len(self._dq) > self._maxlen:
                self._dq.popleft()
                self._dropped += 1
            return ev

    def drain(self) -> List[Dict[str, Any]]:
        """Remove and return all currently queued events, oldest first."""
        with self._lock:
            items = list(self._dq)
            self._dq.clear()
            return items

    def __len__(self) -> int:
        with self._lock:
            return len(self._dq)

    @property
    def dropped(self) -> int:
        with self._lock:
            return self._dropped


class Conn:
    def __init__(self, sock: socket.socket, debug: bool = False):
        self.sock = sock
        self._seq = 0
        self._r = sock.makefile("r", encoding="utf-8", newline="\n")
        self.debug = debug
        self._seen_notices = set()   # for de-duping system.notice
        self.session_token = None

        # Bounded queue of unsolicited events (notices/broadcasts/etc.).
        # Presentation code drains this at safe UI boundaries; Conn itself
        # never prints/renders their content.
        self.events = EventQueue(maxlen=200)

        # True while the socket is believed usable. Set False on a detected
        # disconnect; a "connection.lost" event is queued at the same time.
        self.connected = True

        # optional: the Context can set this after it's created (for prompt redraws)
        self.ctx = None

    def _next_id(self) -> str:
        self._seq += 1
        return f"cli-{self._seq:04d}"

    def send(self, obj: Dict[str, Any]) -> None:
        if self.session_token:
            if "auth" not in obj:
                obj["auth"] = {}
            obj["auth"]["session"] = self.session_token

        line = json.dumps(obj, separators=(",", ":")) + "\n"
        if self.debug:
            print(f"[DBG] << {line!r}")
        try:
            self.sock.sendall(line.encode("utf-8"))
        except OSError as exc:
            self._mark_disconnected(str(exc) or "Connection lost.")
            raise ConnectionError("Connection lost.") from exc

    def recv(self) -> Dict[str, Any]:
        self.sock.settimeout(15.0)
        try:
            line = self._r.readline()
        except OSError as exc:
            self._mark_disconnected(str(exc) or "Connection lost.")
            raise ConnectionError("Connection lost.") from exc
        if self.debug:
            print(f"[DBG] >> {line!r}")
        if not line:
            self._mark_disconnected("Server closed connection.")
            raise ConnectionError("Server closed connection.")
        return json.loads(line)

    def _mark_disconnected(self, reason: str) -> None:
        """Record a detected disconnect exactly once and queue one event."""
        if self.connected:
            self.connected = False
            self.events.push("connection", "connection.lost", {"reason": reason})

    def rpc(self, command: str, data: dict) -> dict:
        """
        Send an RPC and wait for its reply. While waiting, unsolicited
        event/broadcast frames (no reply_to) are classified and pushed onto
        `self.events` instead of being rendered here — Conn never prints
        server content. Also de-duplicate system.notice by notice id.
        """
        req_id = self._next_id()
        self.send({"id": req_id, "command": command, "data": data})

        while True:
            resp = self.recv()

            # 1) Our RPC reply?
            if resp.get("reply_to") == req_id:
                return resp

            # 2) Typed async event/broadcast (no reply_to): e.g., system.notice
            t = resp.get("type")
            if t and not resp.get("reply_to"):
                d = resp.get("data") or {}

                if t == "system.notice":
                    # de-dup on notice id
                    nid = d.get("id")
                    if nid is not None:
                        if nid in self._seen_notices:
                            continue  # drop duplicate
                        self._seen_notices.add(nid)
                        # optional cap to avoid unbounded growth
                        if len(self._seen_notices) > 256:
                            self._seen_notices.clear()

                self.events.push(classify_event_type(t), t, d)
                continue  # keep waiting for our RPC reply

            # 3) Legacy event envelope (id:'evt' or event field)
            if resp.get("id") == "evt" or resp.get("event"):
                ev_type = resp.get("event") or "event"
                d = resp.get("data") or {}
                self.events.push(classify_event_type(ev_type), ev_type, d)
                continue

            # 4) Async errors without reply_to (rare broadcast errors)
            if resp.get("status") in ("error", "refused") and resp.get("reply_to") is None:
                err = (resp.get("error") or {}).get("message") or "error"
                ev_type = resp.get("type") or f"async.{resp.get('status')}"
                self.events.push(classify_event_type(ev_type), ev_type, {"message": err})
                continue

            # 5) Unknown frame shape: retain it (never silently discard),
            # tagged as "unknown", and keep waiting for our RPC reply.
            self.events.push("unknown", "unknown", resp)


def get_data(resp: Dict[str, Any]) -> Dict[str, Any]:
    return (resp or {}).get("data") or {}


def extract_current_sector(resp: Dict[str, Any]) -> Optional[int]:
    d = get_data(resp)
    for k in ("current_sector", "sector_id"):
        v = d.get(k)
        if isinstance(v, int):
            return v
    sess = d.get("session") or {}
    if isinstance(sess.get("current_sector"), int):
        return sess["current_sector"]
    # player.my_info nested style
    pl = d.get("player") or {}
    ship = d.get("ship") or {}
    if isinstance(ship.get("location"), dict) and isinstance(ship["location"].get("sector_id"), int):
        return ship["location"]["sector_id"]
    return None


def normalize_sector(d: Dict[str, Any]) -> Dict[str, Any]:
    """Convert server sector data to the simplified shape used by v3 flags/menus."""
    sid = d.get("sector_id") or (d.get("sector") or {}).get("id") or d.get("id") \
          or (d.get("port") or {}).get("sector_id") or (d.get("port") or {}).get("sector") \
          or (d.get("port") or {}).get("number")

    # Only default to "Sector X" if we have an ID; otherwise keep None so merge skips it
    name = d.get("name") or (d.get("sector") or {}).get("name")
    if name is None and sid is not None:
        name = f"Sector {sid}"

    adj = d.get("adjacent_sectors") or d.get("adjacent") or d.get("adjacent_sectors_info") or d.get("neighbors")

    # Port/class synthesis
    port_obj = None
    ports = d.get("ports")
    if ports and isinstance(ports, list) and len(ports) > 0:
        port_obj = {}
        p0 = ports[0]
        if p0.get("port_id"): port_obj["id"] = p0["port_id"]
        elif p0.get("id"): port_obj["id"] = p0["id"]

        pcls = p0.get("class") or p0.get("type")
        if isinstance(pcls, int) or (isinstance(pcls, str) and pcls.isdigit()):
            port_obj["class"] = int(pcls)
        if p0.get("name"):
            port_obj["name"] = p0["name"]
    elif isinstance(d.get("port"), dict):
        port_obj = dict(d.get("port"))
        if "id" not in port_obj and d["port"].get("id"):
            port_obj["id"] = d["port"]["id"]
        if "class" not in port_obj:
            pcls = port_obj.get("class") or port_obj.get("type")
            if isinstance(pcls, int) or (isinstance(pcls, str) and pcls.isdigit()):
                port_obj["class"] = int(pcls)

    planets = d.get("celestial_objects") or d.get("planets")
    ships = d.get("ships_present") or d.get("ships") or d.get("entities", {}).get("ships")
    beacon = d.get("beacon") or d.get("beacon_text")

    return {
        "id": sid, "name": name,
        "adjacent": adj,
        "port": port_obj,
        "planets": planets,
        "ships": ships,
        "beacon": beacon,
        "counts": d.get("counts")
    }
