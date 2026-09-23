"""
presenters.py — Central response/error presentation for workflows touched in
the "truthful and safe client" slices: movement, subscriptions, and the
settings/navigation/notes/money family.

These functions never mutate client state and never perform RPCs; they only
turn a (typically already-normalised) value into a player-facing message.
Callers decide what (if anything) to do with cached state based on the
returned success flag.
"""
from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple

from money import format_credits_or_dash


def present_move_result(resp: Dict[str, Any]) -> Tuple[bool, str]:
    """
    Describe the outcome of a move.warp response.

    Never mentions the requested destination sector: on success the caller
    already knows it moved and can display the (now-authoritative) sector;
    on failure/refusal we must not imply the player went anywhere, so the
    destination is deliberately omitted here.
    """
    status = (resp or {}).get("status")
    data = (resp or {}).get("data") or {}
    err = (resp or {}).get("error") or {}

    if status == "ok":
        return True, "Move succeeded."

    label = "Move refused" if status == "refused" else "Move failed"
    code = err.get("code")
    if code:
        label += f" (code {code})"
    parts = [label]

    reason = data.get("reason")
    if reason:
        parts.append(f"reason: {reason}")

    msg = err.get("message")
    if msg:
        parts.append(msg)

    return False, " — ".join(parts)


def present_subscription_result(resp: Dict[str, Any], action: str,
                                 topic: Optional[str] = None) -> str:
    """
    Describe the outcome of a subscribe.add/remove/list call.

    `action` is one of "add", "remove", "list" and is only used for wording.
    """
    status = (resp or {}).get("status")
    err = (resp or {}).get("error") or {}

    if status == "ok":
        if action == "add" and topic:
            return f"Subscribed to {topic}."
        if action == "remove" and topic:
            return f"Unsubscribed from {topic}."
        return "OK."

    label = "refused" if status == "refused" else "failed"
    parts = [f"Subscription {action} {label}"]
    code = err.get("code")
    if code:
        parts[0] += f" (code {code})"
    msg = err.get("message")
    if msg:
        parts.append(msg)
    if code == 1407:
        parts.append("This topic is locked and cannot be removed.")

    return " — ".join(parts)


def present_bookmarks(bookmarks: List[Dict[str, Any]]) -> str:
    """Render a normalised bookmark list (see settings_model.normalize_bookmark_list)."""
    if not bookmarks:
        return "No bookmarks set."
    lines = ["Bookmarks:"]
    for b in bookmarks:
        lines.append(f"  {b['name']} -> sector {b['sector_id']}")
    return "\n".join(lines)


def present_avoid_list(sector_ids: List[int]) -> str:
    """Render a normalised avoid list (see settings_model.normalize_avoid_list)."""
    if not sector_ids:
        return "No sectors in avoid list."
    return "Sectors to avoid: " + ", ".join(str(s) for s in sector_ids)


def present_notes(notes: List[Dict[str, Any]]) -> str:
    if not notes:
        return "No notes."
    lines = ["Notes:"]
    for n in notes:
        lines.append(f"  [{n.get('scope')}:{n.get('key')}] {n.get('note')}")
    return "\n".join(lines)


def present_subscriptions(rows: List[Dict[str, Any]]) -> str:
    if not rows:
        return "No active subscriptions."
    lines = ["Subscriptions:"]
    for s in rows:
        lock = " (locked)" if s.get("locked") else ""
        state = "enabled" if s.get("enabled") else "disabled"
        lines.append(f"  {s.get('topic')} — {state}{lock}")
    return "\n".join(lines)


def present_prefs(prefs: Dict[str, Any]) -> str:
    if not prefs:
        return "No preferences set."
    lines = ["Preferences:"]
    for k, v in prefs.items():
        lines.append(f"  {k}: {v}")
    return "\n".join(lines)


def present_settings(model: Dict[str, Any]) -> str:
    """Render the normalised settings model (settings_model.normalize_settings,
    plus a merged-in "notes" list from a separate notes.list call)."""
    sections = [
        "\n=== SETTINGS ===",
        present_prefs(model.get("prefs") or {}),
        present_subscriptions(model.get("subscriptions") or []),
        present_bookmarks(model.get("bookmarks") or []),
        present_avoid_list(model.get("avoid") or []),
        present_notes(model.get("notes") or []),
    ]
    return "\n".join(sections)


def present_money(value, *, suffix: str = " cr") -> str:
    """Format a confirmed credits value for display, degrading to the raw
    value's string form (never a traceback) if it turns out malformed."""
    return format_credits_or_dash(value, suffix=suffix)
