"""
Tests for events.py (EventLog) and its integration with Context.drain_events.

Requirements covered:
13. Event counters increase and clear correctly.
14. Recognised events render without raw JSON.
15. Unknown events are safe in normal mode and inspectable in debug mode.
"""
from events import EventLog, render_event_text, targets_player_ship_destruction
from state import Context


def test_event_counters_increase_and_clear_on_read():
    log = EventLog(maxlen=10)
    assert log.unread_count == 0

    log.ingest([{"seq": 1, "ts": 0, "category": "chat", "type": "chat.broadcast", "data": {}}])
    assert log.unread_count == 1

    log.ingest([
        {"seq": 2, "ts": 0, "category": "system", "type": "system.notice", "data": {}},
        {"seq": 3, "ts": 0, "category": "nav", "type": "nav.beacon", "data": {}},
    ])
    assert log.unread_count == 3

    log.mark_read()
    assert log.unread_count == 0
    # Reading clears the counter, not the history.
    assert len(log) == 3
    assert len(log.recent(50)) == 3


def test_recognised_events_render_without_raw_json():
    ev = {"category": "chat", "type": "chat.broadcast", "data": {"sender_name": "Nova", "message": "hi all"}}
    text = render_event_text(ev, debug=False)
    assert "{" not in text and "}" not in text
    assert "Nova" in text and "hi all" in text


def test_system_notice_renders_title_and_body():
    ev = {"category": "system", "type": "system.notice", "data": {"title": "Maintenance", "body": "5 minutes"}}
    text = render_event_text(ev, debug=False)
    assert "Maintenance" in text
    assert "5 minutes" in text


def test_sector_notice_renders_known_player_actions_and_details():
    ev = {
        "category": "nav",
        "type": "sector.notice",
        "data": {
            "subtype": "fighters_deployed",
            "sector_id": 42,
            "details": {"count": 12, "offense": 50},
        },
    }
    text = render_event_text(ev)
    assert "fighters deployed in sector 42 (12)" in text
    assert "{'subtype'" not in text


def test_sector_notice_unknown_subtype_remains_safe_and_located():
    ev = {"category": "nav", "type": "sector.notice", "data": {
        "subtype": "future_change", "sector_id": 9, "details": {"secret": "not shown"},
    }}
    text = render_event_text(ev)
    assert "Sector notice in sector 9" in text
    assert "not shown" not in text


def test_combat_hit_renders_authoritative_damage_and_remaining_hp():
    ev = {"category": "combat", "type": "combat.hit", "data": {
        "v": 1, "attacker_id": 12, "defender_id": 99, "weapon": "laser_mk2",
        "damage": 42, "defender_hp_after": 58, "sector_id": 278,
    }}

    text = render_event_text(ev)

    assert text == "[Combat] laser_mk2 hit · 42 damage · target HP 58 · sector 278"
    assert "attacker_id" not in text and "defender_id" not in text


def test_planet_attack_event_renders_canonical_authoritative_facts_only():
    event = {"category": "combat", "type": "player.attack_planet.v1", "data": {
        "planet_id": 77, "result": "win", "ship_loss": 25, "planet_loss": 20,
    }}
    assert render_event_text(event) == (
        "[Combat] Planet attack · planet 77 · result: win · ship fighters lost: 25 · planet fighters lost: 20"
    )
    malformed = {"category": "combat", "type": "player.attack_planet.v1", "data": {
        "planet_id": "77", "result": 1, "ship_loss": -1, "planet_loss": "20",
    }}
    assert render_event_text(malformed) == "[Combat] Planet attack"


def test_planet_capture_event_renders_canonical_server_fact():
    event = {"category": "combat", "type": "player.capture_planet.v1", "data": {
        "planet_id": 77, "previous_owner": 9,
    }}
    assert render_event_text(event) == "[Combat] Planet captured · planet 77 · previous owner 9"


def test_defense_events_render_authoritative_depletion_without_inference():
    ev = {"category": "combat", "type": "combat.hit.fighters", "data": {
        "damage": 12, "fighters_engaged": 3, "shields_lost": 4,
        "fighters_lost": 2, "hull_lost": 6,
    }}
    assert render_event_text(ev) == (
        "[Combat] Defensive fighters engaged · 3 engaged · damage: 12 · "
        "shields lost: 4 · fighters lost: 2 · hull lost: 6"
    )


def test_persisted_planetary_defense_event_renders_authoritative_counts():
    ev = {"category": "combat", "type": "combat.planet_landing_defense.v1", "data": {
        "planet_id": 42, "result": "repelled", "attacker_fighters_lost": 8,
        "attacker_fighters_remaining": 2, "planet_fighters_lost": 4,
        "planet_fighters_remaining": 16, "citadel_shields_absorbed": 12,
        "citadel_shields_remaining": 0,
    }}
    assert render_event_text(ev) == (
        "[Combat] Planetary defense · repelled · attacker fighters lost: 8 · "
        "attacker fighters remaining: 2 · planet fighters lost: 4 · "
        "planet fighters remaining: 16 · citadel shields absorbed: 12 · "
        "citadel shields remaining: 0"
    )


def test_persisted_limpet_attachment_event_is_presented_without_ids():
    ev = {"category": "combat", "type": "combat.limpet.attached", "data": {
        "target_ship_id": 61, "target_player_id": 9, "asset_owner_id": 4,
        "sector_id": 17,
    }}
    assert render_event_text(ev) == "[Combat] Limpet attached"
    assert "target_ship_id" not in render_event_text(ev)


def test_environment_defense_event_rejects_malformed_measurements():
    ev = {"category": "combat", "type": "combat.hit.environment", "data": {
        "hazard_type": "minefield", "damage": "12", "destroyed": 1,
    }}
    assert render_event_text(ev) == "[Combat] Environmental hazard · minefield"


def test_tavern_round_event_renders_server_message_as_social_event():
    ev = {"category": "chat", "type": "tavern.round.bought", "data": {
        "message": "A round has been bought for everyone!", "sector_id": 17,
    }}
    text = render_event_text(ev)
    assert "A round has been bought for everyone!" in text


def test_unknown_event_is_safe_compact_in_normal_mode():
    ev = {"category": "unknown", "type": "future.thing", "data": {"secret": "raw-value-should-not-leak"}}
    text = render_event_text(ev, debug=False)
    assert "raw-value-should-not-leak" not in text
    assert "future.thing" in text


def test_unknown_event_is_inspectable_in_debug_mode():
    ev = {"category": "unknown", "type": "future.thing", "data": {"secret": "raw-value-visible-in-debug"}}
    text = render_event_text(ev, debug=True)
    assert "raw-value-visible-in-debug" in text


def test_context_drain_events_moves_queue_into_log(ctx_factory, fake_conn):
    ctx = ctx_factory()
    ctx.conn.events.push("chat", "chat.broadcast", {"message": "hi"})
    ctx.conn.events.push("nav", "nav.beacon", {"message": "beacon"})

    n = ctx.drain_events()

    assert n == 2
    assert ctx.event_log.unread_count == 2
    assert len(ctx.conn.events) == 0  # queue is empty after draining
    assert len(ctx.event_log) == 2


def test_context_activity_count_combines_mail_notices_and_event_log(ctx_factory):
    ctx = ctx_factory()
    assert ctx.activity_count is None  # nothing known yet

    ctx.hud = ctx.hud.__class__(unread_mail=2, unread_notices=1)
    assert ctx.activity_count == 3

    ctx.conn.events.push("chat", "chat.broadcast", {})
    ctx.drain_events()
    assert ctx.activity_count == 4


def test_destruction_trigger_requires_authoritative_victim_identity():
    event = {"type": "combat.ship_destroyed", "data": {"victim_player_id": 42}}
    assert targets_player_ship_destruction(event, 42)
    assert not targets_player_ship_destruction(event, 43)
    assert not targets_player_ship_destruction(
        {"type": "combat.hit", "data": {"victim_player_id": 42}}, 42
    )
    assert not targets_player_ship_destruction(
        {"type": "combat.ship_destroyed", "data": {"victim_player_id": "42"}}, 42
    )
