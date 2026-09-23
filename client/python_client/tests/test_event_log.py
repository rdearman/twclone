"""
Tests for events.py (EventLog) and its integration with Context.drain_events.

Requirements covered:
13. Event counters increase and clear correctly.
14. Recognised events render without raw JSON.
15. Unknown events are safe in normal mode and inspectable in debug mode.
"""
from events import EventLog, render_event_text
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
