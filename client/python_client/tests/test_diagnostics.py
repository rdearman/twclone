from diagnostics import DiagnosticBuffer, make_exchange, sanitize


def test_nested_secrets_and_personal_data_are_removed_before_buffering():
    planted = {
        "request_id": "r-1", "command": "player.my_info", "request": {
            "sector_id": 8, "password": "pw-NeverStore", "nested": [
                {"session_token": "tok-NeverStore", "quantity": 2},
                {"message": "private chat NeverStore"},
            ],
        },
        "response": {"data": {"player_id": 4, "note": "email me test@example.org at 10.2.3.4 and 2001:db8::2 /home/alice/secret"}},
        "stack_trace": "token=abc123 /Users/alice/dev/twclone/client.py",
        "unknown": "NeverStore",
    }
    buffer = DiagnosticBuffer()
    buffer.append(planted)
    serialized = str(buffer.recent())
    for secret in ("pw-NeverStore", "tok-NeverStore", "NeverStore", "test@example.org", "10.2.3.4", "2001:db8::2", "/home/alice", "/Users/alice"):
        assert secret not in serialized
    assert buffer.recent()[0]["request"]["sector_id"] == 8
    assert "unknown" not in buffer.recent()[0]


def test_auth_exchange_captures_only_safe_metadata():
    entry = make_exchange(
        {"id": "a1", "command": "auth.login", "data": {"username": "pilot", "password": "pw"}},
        {"type": "auth.session", "status": "ok", "data": {"session": "token"}}, 12,
    )
    assert "request" not in entry and "response" not in entry
    assert entry["command"] == "auth.login"
    assert entry["response_type"] == "auth.session"
    assert entry["timeout"] is False


def test_missing_response_is_recorded_as_timeout_without_exception_text():
    entry = make_exchange({"id": "r1", "command": "sector.info", "data": {}}, None, 15000)
    assert entry["timeout"] is True
    assert "exception" not in entry


def test_ring_buffer_is_bounded_and_exchange_limit_is_fixed():
    buffer = DiagnosticBuffer(2)
    for i in range(3):
        buffer.append({"request_id": str(i), "command": "sector.info"})
    assert [x["request_id"] for x in buffer.recent(10)] == ["1", "2"]
    try:
        buffer.recent(5)
    except ValueError:
        pass
    else:
        assert False, "unsupported exchange count accepted"


def test_unknown_and_sensitive_nested_fields_are_omitted():
    cleaned = sanitize({"data": {"player_id": 2, "session_id": "secret", "email": "a@b.test", "opaque": "x"}})
    assert cleaned == {"data": {"player_id": 2}}
