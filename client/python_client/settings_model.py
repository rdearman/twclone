"""UI-independent normalisation for the settings/navigation/notes family.

The confirmed server response shapes (per implemented handlers in
``src/server_players.c``) are:

* ``player.get_settings``  -> ``player.settings_v1`` with
  ``data = {prefs: [...], bookmarks: [...], avoid: [...], subscriptions: [...]}``
  (``prefs`` here is an *array* of ``{key, type, value}`` rows).
* ``player.get_prefs``      -> ``player.prefs`` with
  ``data = {prefs: {...}}`` (``prefs`` here is an *object*, key -> value).
* ``nav.bookmark.list``     -> ``data = {items: [{name, sector_id}, ...]}``.
* ``nav.avoid.list``        -> ``data = {items: [sector_id, ...]}``.
* ``notes.list``            -> ``data = {notes: [{scope, key, note}, ...]}``.
* ``subscribe.list``        -> ``data = {topics: [{topic, locked, enabled,
  delivery, filter}, ...]}``.

These shapes disagree with each other (``items`` vs ``bookmarks`` vs
``sectors``, prefs-as-array vs prefs-as-object). This module is the single
place that knows about those differences; everything downstream (menu
handlers, presenters) consumes one stable model:

* prefs       -> ``dict[str, Any]`` (key -> value)
* bookmarks   -> ``list[{"name": str, "sector_id": int}]``
* avoid       -> ``list[int]``
* notes       -> ``list[{"scope": str, "key": str, "note": str}]``
* subscriptions -> ``list[{"topic": str, "locked": bool, "enabled": bool,
  "delivery": str|None, "filter": Any}]``

Missing optional collections normalise to an empty collection. A
response whose confirmed field is present but is a fundamentally wrong
shape (e.g. a string where a list was expected) raises
:class:`NormalizationError`, which callers should catch and present as a
controlled error rather than letting a traceback reach the player.
"""

from typing import Any, Dict, List, Optional

from protocol import get_data


class NormalizationError(ValueError):
    """Raised when a successful response's data does not match any
    confirmed shape for the field being normalised."""


def _data_of(resp_or_data: Any) -> Dict[str, Any]:
    """Accept either a full RPC envelope or an already-extracted data dict."""
    if isinstance(resp_or_data, dict) and "data" in resp_or_data:
        data = get_data(resp_or_data)
    else:
        data = resp_or_data
    if data is None:
        return {}
    if not isinstance(data, dict):
        raise NormalizationError("response data is not an object")
    return data


def normalize_prefs(resp_or_data: Any) -> Dict[str, Any]:
    """Normalise ``prefs`` from either shape into a plain ``{key: value}`` dict."""
    data = _data_of(resp_or_data)
    raw = data.get("prefs")
    return _prefs_value(raw)


def _prefs_value(raw: Any) -> Dict[str, Any]:
    if raw is None:
        return {}
    if isinstance(raw, dict):
        return dict(raw)
    if isinstance(raw, list):
        out: Dict[str, Any] = {}
        for row in raw:
            if isinstance(row, dict) and isinstance(row.get("key"), str):
                out[row["key"]] = row.get("value")
        return out
    raise NormalizationError("prefs field is neither an object nor an array")


def normalize_bookmark_list(resp_or_data: Any) -> List[Dict[str, Any]]:
    """Normalise ``nav.bookmark.list`` (``items``) with the ``bookmarks``
    compatibility alias used by the ``player.get_settings`` aggregate."""
    data = _data_of(resp_or_data)
    raw = data.get("items")
    if raw is None:
        raw = data.get("bookmarks")
    return _bookmarks_value(raw)


def _bookmarks_value(raw: Any) -> List[Dict[str, Any]]:
    if raw is None:
        return []
    if not isinstance(raw, list):
        raise NormalizationError("bookmarks field is not an array")
    out = []
    for row in raw:
        if not isinstance(row, dict):
            continue
        name = row.get("name")
        sector_id = row.get("sector_id")
        if isinstance(name, str) and isinstance(sector_id, int):
            out.append({"name": name, "sector_id": sector_id})
    return out


def normalize_avoid_list(resp_or_data: Any) -> List[int]:
    """Normalise ``nav.avoid.list`` (``items``) with the ``avoid``/``sectors``
    compatibility aliases used elsewhere in the codebase."""
    data = _data_of(resp_or_data)
    raw = data.get("items")
    if raw is None:
        raw = data.get("avoid")
    if raw is None:
        raw = data.get("sectors")
    return _avoid_value(raw)


def _avoid_value(raw: Any) -> List[int]:
    if raw is None:
        return []
    if not isinstance(raw, list):
        raise NormalizationError("avoid field is not an array")
    return [v for v in raw if isinstance(v, int) and not isinstance(v, bool)]


def normalize_notes_list(resp_or_data: Any) -> List[Dict[str, Any]]:
    data = _data_of(resp_or_data)
    return _notes_value(data.get("notes"))


def _notes_value(raw: Any) -> List[Dict[str, Any]]:
    if raw is None:
        return []
    if not isinstance(raw, list):
        raise NormalizationError("notes field is not an array")
    out = []
    for row in raw:
        if isinstance(row, dict):
            out.append({
                "scope": row.get("scope"),
                "key": row.get("key"),
                "note": row.get("note"),
            })
    return out


def normalize_subscription_rows(raw: Any) -> List[Dict[str, Any]]:
    """Normalise a list of subscription rows, shared by ``subscribe.list``
    (``data.topics``) and the ``player.get_settings`` aggregate
    (``data.subscriptions``); both use the same row shape."""
    if raw is None:
        return []
    if not isinstance(raw, list):
        raise NormalizationError("subscriptions field is not an array")
    out = []
    for row in raw:
        if not isinstance(row, dict):
            continue
        out.append({
            "topic": row.get("topic"),
            "locked": bool(row.get("locked")),
            "enabled": bool(row.get("enabled")),
            "delivery": row.get("delivery"),
            "filter": row.get("filter"),
        })
    return out


def normalize_settings(resp_or_data: Any) -> Dict[str, Any]:
    """Normalise the ``player.get_settings`` aggregate response.

    Note: the confirmed ``player.settings_v1`` payload does not include
    notes; callers that want notes alongside settings must fetch
    ``notes.list`` separately and merge it in (this mirrors what the
    server actually implements rather than inventing an aggregate field).
    """
    data = _data_of(resp_or_data)
    return {
        "prefs": _prefs_value(data.get("prefs")),
        "bookmarks": _bookmarks_value(data.get("bookmarks")),
        "avoid": _avoid_value(data.get("avoid")),
        "subscriptions": normalize_subscription_rows(data.get("subscriptions")),
    }
