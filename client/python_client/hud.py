"""
hud.py — Persistent HUD state model, authoritative extraction, and rendering.

This module is the single place that turns authoritative server responses
into HUD field values (per the "centralise response-to-state updates" rule)
and turns a `HudState` into player-facing text. It has no RPC dependency: it
only reads response dicts handed to it by client.py after a call succeeds.

Field provenance (confirmed from server handlers, not draft docs):

  * sector_id / sector_name  <- move.describe_sector ("move.result"-style
                                normalize_sector() shape already used by state.py)
  * ship_id / ship_name      <- ship.status ("ship.status", data.ship)
  * turns_remaining          <- player.my_info ("player.info", data.player.turns_remaining)
  * credits                  <- player.my_info ("player.info", data.player.credits,
                                a decimal string "N.00" parsed via money.parse_credits)
  * cargo_used / cargo_total <- ship.status: cargo_total = data.ship.holds
                                (confirmed total capacity, see ships table CHECK
                                constraint in sql/pg/000_tables.sql); cargo_used =
                                sum(item["qty"] for item in data.ship.cargo) — safe
                                to derive locally because the server enforces the
                                holds >= used invariant via a DB CHECK constraint.
  * fighters / shields       <- ship.status (data.ship.fighters / data.ship.shields)
  * unread_mail              <- mail.inbox (count of data.items lacking "read_at" in
                                the fetched page only — an approximation bounded by
                                pagination, NOT a verified total; mail.inbox does not
                                mark messages read, so this is safe to poll)
  * unread_notices           <- notice.list (count of data.items lacking "seen_at";
                                notice.list does not mark notices seen, so this is
                                safe to poll; notice.ack is the only mutator)
  * unread_news              <- auth.login ONLY ("auth.session", data.unread_news_count).
                                CONFIRMED GAP: no other implemented command exposes an
                                unread news count without side effects — news.get_feed
                                marks all news as read as part of its own execution
                                (repo_news_update_last_read). This field is therefore a
                                login-time snapshot and is never refreshed mid-session.

Connection state (ONLINE/STALE/OFFLINE) is derived, not stored redundantly:
`Conn.connected` is the source of truth for OFFLINE; STALE is derived by
comparing `hud.last_refresh_ts` against a staleness threshold. RECONNECTING
is part of the presentation vocabulary but is never reached in this slice —
no automatic-reconnect logic exists yet; it is reserved for a future slice.
"""
from __future__ import annotations

import time
from dataclasses import dataclass, replace
from typing import Any, Dict, List, Optional

from money import parse_credits, format_credits, MoneyError

STALE_AFTER_SECONDS = 30.0

UNAVAILABLE = "\u2014"  # em dash, per spec: use "—" for unavailable values


@dataclass(frozen=True)
class HudState:
    """
    Every field is `None` when unknown/not yet supplied by the server — never
    coerced to 0. A field holding an int (including 0) means the server
    reported that exact value. `last_refresh_ts` is None until the first
    successful hydration.
    """
    sector_id: Optional[int] = None
    sector_name: Optional[str] = None
    ship_id: Optional[int] = None
    ship_name: Optional[str] = None
    turns_remaining: Optional[int] = None
    credits: Optional[int] = None
    cargo_used: Optional[int] = None
    cargo_total: Optional[int] = None
    fighters: Optional[int] = None
    shields: Optional[int] = None
    unread_mail: Optional[int] = None
    unread_notices: Optional[int] = None
    unread_news: Optional[int] = None
    last_refresh_ts: Optional[float] = None


def _is_ok(resp: Optional[Dict[str, Any]]) -> bool:
    return bool(resp) and resp.get("status") == "ok"


def extract_player_fields(resp: Dict[str, Any]) -> Dict[str, Any]:
    """player.my_info ("player.info") -> {turns_remaining, credits}."""
    if not _is_ok(resp):
        return {}
    player = ((resp.get("data") or {}).get("player")) or {}
    out: Dict[str, Any] = {}
    if isinstance(player.get("turns_remaining"), int):
        out["turns_remaining"] = player["turns_remaining"]
    if "credits" in player:
        try:
            out["credits"] = parse_credits(player["credits"])
        except MoneyError:
            pass  # malformed money -> field stays unknown, not invented
    return out


def extract_ship_fields(resp: Dict[str, Any]) -> Dict[str, Any]:
    """ship.status -> {ship_id, ship_name, fighters, shields, cargo_used, cargo_total}."""
    if not _is_ok(resp):
        return {}
    ship = (resp.get("data") or {}).get("ship") or {}
    out: Dict[str, Any] = {}
    if isinstance(ship.get("id"), int):
        out["ship_id"] = ship["id"]
    if isinstance(ship.get("name"), str):
        out["ship_name"] = ship["name"]
    if isinstance(ship.get("fighters"), int):
        out["fighters"] = ship["fighters"]
    if isinstance(ship.get("shields"), int):
        out["shields"] = ship["shields"]
    if isinstance(ship.get("holds"), int):
        out["cargo_total"] = ship["holds"]
    cargo = ship.get("cargo")
    if isinstance(cargo, list):
        used = 0
        for item in cargo:
            qty = item.get("qty") if isinstance(item, dict) else None
            if isinstance(qty, int):
                used += qty
        out["cargo_used"] = used
    return out


def extract_sector_fields(normalized_sector: Dict[str, Any]) -> Dict[str, Any]:
    """From protocol.normalize_sector()'s already-normalized shape."""
    out: Dict[str, Any] = {}
    if normalized_sector.get("id") is not None:
        out["sector_id"] = normalized_sector.get("id")
    if normalized_sector.get("name") is not None:
        out["sector_name"] = normalized_sector.get("name")
    return out


def extract_mail_unread(resp: Dict[str, Any]) -> Dict[str, Any]:
    """mail.inbox -> {unread_mail}. Page-bounded approximation (see module docstring)."""
    if not _is_ok(resp):
        return {}
    items = (resp.get("data") or {}).get("items")
    if not isinstance(items, list):
        return {}
    unread = sum(1 for it in items if isinstance(it, dict) and not it.get("read_at"))
    return {"unread_mail": unread}


def extract_notice_unread(resp: Dict[str, Any]) -> Dict[str, Any]:
    """notice.list -> {unread_notices}."""
    if not _is_ok(resp):
        return {}
    items = (resp.get("data") or {}).get("items")
    if not isinstance(items, list):
        return {}
    unread = sum(1 for it in items if isinstance(it, dict) and not it.get("seen_at"))
    return {"unread_notices": unread}


def extract_login_unread_news(resp: Dict[str, Any]) -> Dict[str, Any]:
    """auth.login ("auth.session") -> {unread_news}. Login-only snapshot."""
    if not _is_ok(resp):
        return {}
    data = resp.get("data") or {}
    if isinstance(data.get("unread_news_count"), int):
        return {"unread_news": data["unread_news_count"]}
    return {}


def merge_hud(hud: HudState, fields: Dict[str, Any], touch_refresh: bool = True) -> HudState:
    """Return a new HudState with only the provided fields updated."""
    if not fields:
        return hud
    updates = dict(fields)
    if touch_refresh:
        updates["last_refresh_ts"] = time.time()
    return replace(hud, **updates)


def hydrate_login(
    hud: HudState,
    login_resp: Dict[str, Any],
    my_info_resp: Optional[Dict[str, Any]] = None,
    ship_resp: Optional[Dict[str, Any]] = None,
    sector_resp: Optional[Dict[str, Any]] = None,
    mail_resp: Optional[Dict[str, Any]] = None,
    notice_resp: Optional[Dict[str, Any]] = None,
) -> HudState:
    """Populate every available HUD field after a successful login."""
    fields: Dict[str, Any] = {}
    fields.update(extract_login_unread_news(login_resp))
    if my_info_resp is not None:
        fields.update(extract_player_fields(my_info_resp))
    if ship_resp is not None:
        fields.update(extract_ship_fields(ship_resp))
    if sector_resp is not None:
        fields.update(extract_sector_fields(sector_resp))
    if mail_resp is not None:
        fields.update(extract_mail_unread(mail_resp))
    if notice_resp is not None:
        fields.update(extract_notice_unread(notice_resp))
    return merge_hud(hud, fields)


# Commands whose successful response can update HUD fields, and the
# extractor used for each. Refused/error responses never reach these
# extractors (callers must check status themselves — see apply_response).
_RESPONSE_EXTRACTORS = {
    "player.my_info": extract_player_fields,
    "ship.status": extract_ship_fields,
    "ship.info": extract_ship_fields,  # deprecated alias of ship.status
    "mail.inbox": extract_mail_unread,
    "notice.list": extract_notice_unread,
}


def apply_response(hud: HudState, command: str, resp: Dict[str, Any]) -> HudState:
    """
    Centralised, command-keyed response -> HUD update.

    Never mutates on a refused/failed response (extractors themselves also
    check status, but this checks first so unknown-command inputs are cheap
    no-ops). This is the single choke point menu handlers should call
    instead of reaching into response dicts themselves.
    """
    if not _is_ok(resp):
        return hud
    extractor = _RESPONSE_EXTRACTORS.get(command)
    if not extractor:
        return hud
    return merge_hud(hud, extractor(resp))


def connection_label(connected: bool, last_refresh_ts: Optional[float], now: Optional[float] = None) -> str:
    """ONLINE / STALE / OFFLINE. RECONNECTING is reserved, unused this slice."""
    if not connected:
        return "OFFLINE"
    if last_refresh_ts is None:
        return "ONLINE"
    now = now if now is not None else time.time()
    if now - last_refresh_ts > STALE_AFTER_SECONDS:
        return "STALE"
    return "ONLINE"


def _fmt(value: Optional[Any]) -> str:
    return UNAVAILABLE if value is None else str(value)


def render_hud_lines(hud: HudState, connected: bool, activity: Optional[int], width: int = 80) -> List[str]:
    """
    Render the compact persistent HUD as one or more lines, wrapping to
    `width` rather than truncating a value. Uses "—" for unavailable fields
    and never depends on colour to distinguish link state.
    """
    link = connection_label(connected, hud.last_refresh_ts)

    credits_text = UNAVAILABLE if hud.credits is None else format_credits(hud.credits)
    cargo_text = (
        f"{hud.cargo_used}/{hud.cargo_total}"
        if hud.cargo_used is not None and hud.cargo_total is not None
        else UNAVAILABLE
    )

    parts_line1 = [
        f"Sector {_fmt(hud.sector_id)}: {_fmt(hud.sector_name)}",
        f"Ship: {_fmt(hud.ship_name)}",
        f"Turns: {_fmt(hud.turns_remaining)}",
        f"Credits: {credits_text}",
    ]
    parts_line2 = [
        f"Holds: {cargo_text}",
        f"Fighters: {_fmt(hud.fighters)}",
        f"Shields: {_fmt(hud.shields)}",
        f"Link: {link}",
        f"Activity: {_fmt(activity)}",
    ]

    def _pack(parts: List[str]) -> List[str]:
        lines: List[str] = []
        cur = ""
        for part in parts:
            candidate = part if not cur else f"{cur} | {part}"
            if len(candidate) > width and cur:
                lines.append(cur)
                cur = part
            else:
                cur = candidate
        if cur:
            lines.append(cur)
        return lines

    return _pack(parts_line1) + _pack(parts_line2)
