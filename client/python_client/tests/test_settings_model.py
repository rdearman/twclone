"""
Tests for settings_model.py: UI-independent normalisation of the settings/
navigation/notes family, against the confirmed server response shapes (see
src/server_players.c cmd_nav_bookmark_list, cmd_nav_avoid_list,
cmd_player_get_settings, cmd_player_get_notes, cmd_player_get_prefs).
"""
import pytest

from settings_model import (
    NormalizationError,
    normalize_prefs,
    normalize_bookmark_list,
    normalize_avoid_list,
    normalize_notes_list,
    normalize_subscription_rows,
    normalize_settings,
)


def test_normalize_bookmark_list_confirmed_items_shape():
    resp = {"status": "ok", "data": {"items": [
        {"name": "home", "sector_id": 1},
        {"name": "market", "sector_id": 42},
    ]}}
    assert normalize_bookmark_list(resp) == [
        {"name": "home", "sector_id": 1},
        {"name": "market", "sector_id": 42},
    ]


def test_normalize_bookmark_list_missing_is_empty():
    assert normalize_bookmark_list({"status": "ok", "data": {}}) == []


def test_normalize_avoid_list_confirmed_items_shape():
    resp = {"status": "ok", "data": {"items": [5, 10, 15]}}
    assert normalize_avoid_list(resp) == [5, 10, 15]


def test_normalize_avoid_list_missing_is_empty():
    assert normalize_avoid_list({"status": "ok", "data": {}}) == []


def test_normalize_settings_aggregate_with_all_collections():
    resp = {
        "status": "ok",
        "type": "player.settings_v1",
        "data": {
            "prefs": [
                {"key": "ui.clock_24h", "type": "bool", "value": "true"},
                {"key": "ui.locale", "type": "string", "value": "en-GB"},
            ],
            "bookmarks": [{"name": "home", "sector_id": 1}],
            "avoid": [7, 8],
            "subscriptions": [
                {"topic": "news.global", "locked": False, "enabled": True,
                 "delivery": "push", "filter": None},
            ],
        },
    }
    model = normalize_settings(resp)
    assert model["prefs"] == {"ui.clock_24h": "true", "ui.locale": "en-GB"}
    assert model["bookmarks"] == [{"name": "home", "sector_id": 1}]
    assert model["avoid"] == [7, 8]
    assert model["subscriptions"] == [
        {"topic": "news.global", "locked": False, "enabled": True,
         "delivery": "push", "filter": None},
    ]


def test_normalize_settings_aggregate_with_omitted_optional_collections():
    # Only prefs present; bookmarks/avoid/subscriptions omitted entirely.
    resp = {"status": "ok", "data": {"prefs": {"ui.locale": "en-GB"}}}
    model = normalize_settings(resp)
    assert model["prefs"] == {"ui.locale": "en-GB"}
    assert model["bookmarks"] == []
    assert model["avoid"] == []
    assert model["subscriptions"] == []


def test_normalize_settings_malformed_response_raises_controlled_error():
    # data present but is fundamentally the wrong shape (not an object).
    resp = {"status": "ok", "data": "oops"}
    with pytest.raises(NormalizationError):
        normalize_settings(resp)


def test_normalize_settings_malformed_collection_raises_controlled_error():
    # bookmarks confirmed to be an array; a string here is malformed.
    resp = {"status": "ok", "data": {"bookmarks": "not-a-list"}}
    with pytest.raises(NormalizationError):
        normalize_settings(resp)


def test_normalize_prefs_object_shape_from_get_prefs():
    # player.get_prefs sends prefs as an object, not an array.
    resp = {"status": "ok", "data": {"prefs": {"ui.clock_24h": "true"}}}
    assert normalize_prefs(resp) == {"ui.clock_24h": "true"}


def test_normalize_prefs_array_shape_from_settings_aggregate():
    resp = {"status": "ok", "data": {"prefs": [
        {"key": "ui.clock_24h", "type": "bool", "value": "false"},
    ]}}
    assert normalize_prefs(resp) == {"ui.clock_24h": "false"}


def test_normalize_notes_list_confirmed_shape():
    resp = {"status": "ok", "data": {"notes": [
        {"scope": "sector", "key": "42", "note": "danger"},
    ]}}
    assert normalize_notes_list(resp) == [
        {"scope": "sector", "key": "42", "note": "danger"},
    ]


def test_normalize_subscription_rows_shared_shape():
    rows = normalize_subscription_rows([
        {"topic": "mail.new", "locked": True, "enabled": True, "delivery": "push"},
    ])
    assert rows == [
        {"topic": "mail.new", "locked": True, "enabled": True,
         "delivery": "push", "filter": None},
    ]
