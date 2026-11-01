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
    ("tavern.", "chat"),
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

_SENSITIVE_KEYS = {
    "password", "passwd", "old_password", "new_password", "confirm_password",
    "token", "session", "session_token", "secret", "authorization",
}


def _redact_sensitive(value: Any) -> Any:
    """Return a log-safe copy of a protocol value, recursively masking secrets."""
    if isinstance(value, dict):
        return {
            key: "<redacted>" if str(key).lower() in _SENSITIVE_KEYS else _redact_sensitive(item)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [_redact_sensitive(item) for item in value]
    return value


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

        # The first (and only) disconnect reason recorded for this
        # connection. Always a short, player-safe label — never raw
        # exception text, socket internals, or protocol frame content.
        # Remains None until a disconnect is detected.
        self.disconnect_reason: Optional[str] = None

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
            safe_line = json.dumps(_redact_sensitive(obj), separators=(",", ":")) + "\n"
            print(f"[DBG] << {safe_line!r}")
        try:
            self.sock.sendall(line.encode("utf-8"))
        except OSError as exc:
            # Normalise every socket-layer failure into ConnectionError here;
            # callers (client.py) must never need to inspect OSError/EOF
            # variants themselves.
            self._mark_disconnected("Connection lost while sending data.")
            raise ConnectionError("Connection lost.") from exc

    def recv(self) -> Dict[str, Any]:
        self.sock.settimeout(15.0)
        try:
            line = self._r.readline()
        except OSError as exc:
            self._mark_disconnected("Connection lost while receiving data.")
            raise ConnectionError("Connection lost.") from exc
        if not line:
            self._mark_disconnected("Server closed the connection.")
            raise ConnectionError("Server closed connection.")
        response = json.loads(line)
        if self.debug:
            safe_line = json.dumps(_redact_sensitive(response), separators=(",", ":")) + "\n"
            print(f"[DBG] >> {safe_line!r}")
        return response

    def _mark_disconnected(self, reason: str) -> None:
        """
        Record a detected disconnect exactly once and queue exactly one
        "connection.lost" event.

        `reason` must already be a short, player-safe label (no raw
        exception text, no socket internals, no credentials/tokens, no
        protocol frame content) — this is the only place that constructs
        the stored reason, so callers never need to sanitise it themselves.
        Idempotent: once `self.connected` is False, further calls (from
        repeated failed send/recv attempts on an already-dead socket) are a
        deliberate no-op, so the *first* reason and event are the ones that
        are ever recorded, never duplicated or overwritten.
        """
        if self.connected:
            self.connected = False
            self.disconnect_reason = reason
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

                if t in ("system.notice", "system.notice_v1"):
                    # de-dup on notice id
                    nid = d.get("notice_id", d.get("id"))
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
        if k in d:
            # IDs are server-owned JSON integers.  In Python, bool is an int
            # subclass; accepting it would turn malformed visibility data
            # into sector 1 and could send pathfinding or refresh requests to
            # an invented location.  An explicit malformed primary field must
            # also stop fallback to a contradictory legacy alias.
            v = d[k]
            return v if type(v) is int and v > 0 else None
    sess = d.get("session") or {}
    if isinstance(sess, dict) and "current_sector" in sess:
        v = sess["current_sector"]
        return v if type(v) is int and v > 0 else None
    # player.my_info nested style
    pl = d.get("player") or {}
    if isinstance(pl, dict):
        for k in ("sector", "sector_id"):
            if k in pl:
                v = pl[k]
                return v if type(v) is int and v > 0 else None
    ship = d.get("ship") or {}
    if isinstance(ship, dict) and isinstance(ship.get("location"), dict):
        location = ship["location"]
        if "sector_id" in location:
            v = location["sector_id"]
            return v if type(v) is int and v > 0 else None
    return None


def _authoritative_positive_id(value: Any) -> Optional[int]:
    """Return only a JSON integer ID; never coerce aliases or booleans."""
    if type(value) is int and value > 0:
        return value
    return None


def _first_authoritative_id(source: Dict[str, Any], keys: tuple[str, ...]) -> Optional[int]:
    """Use the first declared ID field, preserving an invalid primary field."""
    for key in keys:
        if key in source:
            return _authoritative_positive_id(source[key])
    return None


def _authoritative_adjacent_ids(value: Any) -> Optional[list[int]]:
    """Keep only server-owned positive integer neighbor IDs."""
    if not isinstance(value, list):
        return None
    return [item for item in value if _authoritative_positive_id(item) is not None]


def normalize_sector(d: Dict[str, Any]) -> Dict[str, Any]:
    """Convert server sector data to the simplified shape used by v3 flags/menus."""
    # ``data`` is transport input, not a trusted object.  A malformed or
    # schema-mismatched visibility response must not crash a refresh caller
    # (or become a reason to replace the confirmed sector cache).  Callers
    # decide whether an empty normalized record is acceptable.
    if not isinstance(d, dict):
        return {}
    nested_sector = d.get("sector") if isinstance(d.get("sector"), dict) else {}
    nested_port = d.get("port") if isinstance(d.get("port"), dict) else {}
    # Do not let an invalid authoritative sector_id fall through to a
    # contradictory legacy alias. The refresh caller uses this identity to
    # correlate visibility data with the confirmed current location.
    if "sector_id" in d:
        sid = _authoritative_positive_id(d["sector_id"])
    else:
        sid = _first_authoritative_id(nested_sector, ("id",))
        if sid is None and "id" in d:
            sid = _authoritative_positive_id(d["id"])
        if sid is None:
            sid = _first_authoritative_id(nested_port, ("sector_id", "sector", "number"))

    # Only default to "Sector X" if we have an ID; otherwise keep None so merge skips it
    raw_name = d.get("name") if "name" in d else nested_sector.get("name")
    # Visibility data is server-owned display state.  Do not let a
    # schema-mismatched name (for example an object or integer) enter the
    # confirmed cache and later be stringified by the menu layer.  Preserve
    # the canonical name when it is a non-empty string, otherwise use the
    # existing ID-derived display label.
    name = raw_name if isinstance(raw_name, str) and raw_name else None
    if name is None and sid is not None:
        name = f"Sector {sid}"

    adjacent_source = None
    for key in ("adjacent_sectors", "adjacent", "adjacent_sectors_info", "neighbors"):
        if key in d:
            adjacent_source = d[key]
            break
    adj = _authoritative_adjacent_ids(adjacent_source)

    # Port presence is public sector information; port class and trade mode
    # are only available from an in-sector port.info response. Older server
    # payloads may still include type/class, so intentionally drop those keys.
    port_obj = None
    ports = d.get("ports")
    ports_known = "ports" in d or "port" in d
    if isinstance(ports, list):
        # A non-empty array is visibility evidence only when every row has
        # the authoritative object shape.  Accepting the first row alone
        # would let a later malformed row turn a schema-mismatched response
        # into a cached port disclosure.
        if ports and all(isinstance(row, dict) for row in ports):
            p0 = ports[0]
            port_obj = {"present": True}
            port_id = _first_authoritative_id(p0, ("port_id", "id"))
            if port_id is not None:
                port_obj["id"] = port_id
            if isinstance(p0.get("name"), str) and p0["name"]:
                port_obj["name"] = p0["name"]
        elif ports:
            port_obj = {"present": None}
        else:
            port_obj = {"present": False}
    elif isinstance(d.get("port"), dict):
        source_port = d["port"]
        # A port object implies presence only when the server omitted the
        # optional flag.  If the flag is supplied, it must be a JSON boolean;
        # coercing values such as "false" would turn malformed visibility
        # metadata into an actionable port disclosure.
        raw_present = source_port.get("present", True)
        port_obj = {"present": raw_present if type(raw_present) is bool else None}
        port_id = _first_authoritative_id(source_port, ("id", "port_id"))
        if port_id is not None:
            port_obj["id"] = port_id
        if isinstance(source_port.get("name"), str) and source_port["name"]:
            port_obj["name"] = source_port["name"]
    elif ports_known:
        port_obj = {"present": False}

    planet_keys = ("celestial_objects", "planets")
    ship_keys = ("ships_present", "ships")
    planets_known = any(key in d for key in planet_keys)
    ships_known = any(key in d for key in ship_keys) or (
        isinstance(d.get("entities"), dict) and "ships" in d["entities"])
    planets = d.get("celestial_objects", d.get("planets"))
    ships = d.get("ships_present", d.get("ships"))
    if ships is None and isinstance(d.get("entities"), dict):
        ships = d["entities"].get("ships")
    beacon = d.get("beacon") or d.get("beacon_text")

    return {
        "id": sid, "name": name,
        "adjacent": adj,
        "port": port_obj,
        "planets": planets,
        "ships": ships,
        "beacon": beacon,
        "counts": d.get("counts"),
        "knowledge": {
            "adjacent": any(key in d for key in ("adjacent_sectors", "adjacent", "adjacent_sectors_info", "neighbors")),
            "ports": ports_known,
            "planets": planets_known,
            "ships": ships_known,
        },
    }
