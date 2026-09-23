"""
Tests for client.run_session()'s and client.menu_on_enter()'s disconnect
contract (the "graceful mid-session disconnect" slice).

`run_session` is the extracted body of the interactive menu loop that used
to live inline in `main()`. These tests drive it with monkeypatched
render_menu/read_choice/handle_choice rather than a real menus.json tree,
so the loop mechanics can be tested in isolation from menu content.
"""
import sys

import pytest

import client


class _StubCtx:
    """Minimal stand-in exposing just what run_session touches."""

    def __init__(self):
        self.drain_calls = 0

    def drain_events(self):
        self.drain_calls += 1
        return 0


def test_run_session_returns_exit_ok_on_normal_quit(monkeypatch):
    ctx = _StubCtx()
    monkeypatch.setattr(client, "render_menu", lambda c: None)
    monkeypatch.setattr(client, "read_choice", lambda: "q")
    monkeypatch.setattr(client, "handle_choice", lambda c, choice: sys.exit(0))

    assert client.run_session(ctx) == client.EXIT_OK


def test_run_session_returns_connection_lost_and_prints_one_message(monkeypatch, capsys):
    ctx = _StubCtx()
    monkeypatch.setattr(client, "render_menu", lambda c: None)
    monkeypatch.setattr(client, "read_choice", lambda: "x")

    def fake_handle_choice(c, choice):
        raise ConnectionError("Connection lost.")

    monkeypatch.setattr(client, "handle_choice", fake_handle_choice)

    result = client.run_session(ctx)
    assert result == client.EXIT_CONNECTION_LOST

    out = capsys.readouterr().out
    assert "Traceback" not in out
    lines = [ln for ln in out.splitlines() if ln.strip()]
    # Exactly one player-facing disconnect line, never more.
    assert len(lines) == 1
    assert "lost" in lines[0].lower()


def test_run_session_disconnect_message_not_preceded_by_event_drain_notice(monkeypatch, capsys):
    """
    The queued `connection.lost` event that caused the disconnect must not
    also be surfaced via the normal "N new events" notice right before the
    terminal message — it stays queued/unread for later inspection instead
    of being reported twice.
    """
    ctx = _StubCtx()
    monkeypatch.setattr(client, "render_menu", lambda c: None)
    monkeypatch.setattr(client, "read_choice", lambda: "x")
    monkeypatch.setattr(
        client, "handle_choice",
        lambda c, choice: (_ for _ in ()).throw(ConnectionError("Connection lost.")),
    )

    client.run_session(ctx)

    out = capsys.readouterr().out
    assert "new event" not in out
    # Only the single drain_events() call that runs before handle_choice on
    # this iteration — no extra drain/notify cycle after the exception.
    assert ctx.drain_calls == 1


def test_run_session_propagates_keyboard_interrupt(monkeypatch):
    """run_session must only special-case SystemExit/ConnectionError."""
    ctx = _StubCtx()
    monkeypatch.setattr(client, "render_menu", lambda c: None)
    monkeypatch.setattr(client, "read_choice", lambda: "x")

    def fake_handle_choice(c, choice):
        raise KeyboardInterrupt()

    monkeypatch.setattr(client, "handle_choice", fake_handle_choice)

    with pytest.raises(KeyboardInterrupt):
        client.run_session(ctx)


def test_menu_on_enter_reraises_connection_error():
    def bad_hook(ctx):
        raise ConnectionError("Connection lost.")

    client.__dict__["_test_bad_hook_connection"] = bad_hook
    try:
        with pytest.raises(ConnectionError):
            client.menu_on_enter(object(), {"on_enter": {"pycall": "_test_bad_hook_connection"}})
    finally:
        del client.__dict__["_test_bad_hook_connection"]


def test_menu_on_enter_still_suppresses_other_prefetch_failures():
    """
    Documented technical debt: non-connection prefetch failures remain
    non-fatal for this slice (only ConnectionError is special-cased).
    """
    def bad_hook(ctx):
        raise ValueError("boom")

    client.__dict__["_test_bad_hook_value"] = bad_hook
    try:
        client.menu_on_enter(object(), {"on_enter": {"pycall": "_test_bad_hook_value"}})
    finally:
        del client.__dict__["_test_bad_hook_value"]
