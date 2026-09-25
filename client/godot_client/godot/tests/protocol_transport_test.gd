extends SceneTree

const Protocol = preload("res://Protocol.gd")
const RequestManager = preload("res://RequestManager.gd")
const Transport = preload("res://ProtocolTransport.gd")
const AuthSession = preload("res://AuthSession.gd")

var failures: Array = []

func _init() -> void:
	_test_protocol_status_and_redaction()
	_test_request_lifecycle_and_timeout()
	_test_framing_and_correlation()
	_test_events_and_invalid_frames()
	_test_disconnect_and_late_reply()
	_test_authentication_contract()
	_test_structured_results()
	_test_event_classification()
	if failures.is_empty():
		print("Godot protocol tests: 8 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		print("Godot protocol tests: %d failed" % failures.size())
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

func _test_protocol_status_and_redaction() -> void:
	_check(Protocol.response_status({"status": "ok"}) == "ok", "ok status was not recognized")
	_check(Protocol.response_status({"status": "refused", "error": {}}) == "refused", "refusal status was not recognized")
	_check(Protocol.response_status({"type": "error"}) == "error", "error type was not recognized")
	var safe: Dictionary = Protocol.redact({"passwd": "secret", "auth": {"session": "token"}, "value": 3})
	_check(safe["passwd"] == Protocol.REDACTED, "password was not redacted")
	_check(safe["auth"]["session"] == Protocol.REDACTED, "session was not redacted")
	_check(safe["value"] == 3, "non-secret diagnostic data changed")
	var invalid := Protocol.parse_frame("[1, 2, 3]")
	_check(not invalid["ok"], "non-object envelope was accepted")
	_check(not Protocol.parse_frame("{}")["ok"], "empty envelope was accepted")
	_check(not Protocol.diagnostic_json({"password": "secret", "session": "token"}).contains("secret"), "diagnostic leaked a secret")

func _test_request_lifecycle_and_timeout() -> void:
	var manager = RequestManager.new()
	manager.timeout_seconds = 1.0
	_check(manager.next_id() != manager.next_id(), "request identifiers were not unique")
	manager.register("r1", "player.my_info", 1000)
	_check(manager.size() == 1, "request was not registered")
	var resolved := manager.resolve("r1")
	_check(resolved.get("command", "") == "player.my_info", "request did not resolve by ID")
	_check(manager.size() == 0, "resolved request remained pending")
	manager.register("r2", "sector.info", 1000)
	var expired := manager.expire(2001)
	_check(expired.size() == 1 and expired[0]["id"] == "r2", "expired request was not reported")
	_check(manager.size() == 0, "expired request remained pending")

func _test_framing_and_correlation() -> void:
	var transport = Transport.new(15.0, 10)
	var replies: Array = []
	transport.reply_received.connect(func(_id: String, response: Dictionary, _command: String): replies.append(response))
	transport._requests.register("r1", "first", 0)
	transport._requests.register("r2", "second", 0)
	transport._consume_bytes("{\"reply_to\":\"r2\",\"status\":\"ok\",\"type\":\"second.done\"}")
	_check(replies.is_empty(), "partial frame was delivered too early")
	transport._consume_bytes("\n{\"reply_to\":\"r1\",\"status\":\"ok\",\"type\":\"first.done\"}\n")
	_check(replies.size() == 2, "fragmented or multiple frames were not delivered")
	_check(replies[0]["type"] == "second.done" and replies[1]["type"] == "first.done", "out-of-order replies were misordered")
	_check(transport.pending_count() == 0, "completed requests remained pending")

func _test_events_and_invalid_frames() -> void:
	var transport = Transport.new(15.0, 2)
	var errors: Array = []
	var warnings: Array = []
	transport.protocol_error.connect(func(message: String): errors.append(message))
	transport.protocol_warning.connect(func(message: String): warnings.append(message))
	transport._consume_bytes("{bad json}\n")
	transport._consume_bytes("{\"type\":\"system.notice\",\"data\":{\"id\":1}}\n{\"event\":\"mystery.event\",\"data\":{\"value\":2}}\n{\"type\":\"not_a_known_event\",\"data\":{}}\n")
	_check(errors.size() == 1, "malformed JSON did not produce a protocol error")
	_check(transport.drain_events().size() == 2, "valid and unknown events were not queued separately")
	_check(warnings.size() == 1, "uncorrelated unknown frame was treated as an event")
	_check(not errors[0].contains("bad json"), "protocol error leaked raw JSON")

func _test_disconnect_and_late_reply() -> void:
	var transport = Transport.new(15.0, 10)
	var warnings: Array = []
	var disconnects: Array = []
	var failed: Array = []
	transport.protocol_warning.connect(func(message: String): warnings.append(message))
	transport.transport_disconnected.connect(func(reason: String): disconnects.append(reason))
	transport.request_failed.connect(func(id: String, command: String, result: Dictionary): failed.append({"id": id, "command": command, "result": result}))
	transport._requests.register("late", "slow.command", 0)
	var expired: Array = transport._requests.expire(15000)
	_check(expired.size() == 1, "request did not expire")
	transport._consume_bytes("{\"reply_to\":\"late\",\"status\":\"ok\"}\n")
	transport._consume_bytes("{\"reply_to\":\"never\",\"status\":\"ok\"}\n")
	_check(warnings.size() == 2, "late and unknown replies were not rejected")
	transport._connected_announced = true
	transport._requests.register("pending", "pending.command", 0)
	transport._mark_disconnected("Test disconnect.")
	_check(disconnects.size() == 1, "disconnect was not propagated")
	_check(failed.size() == 1 and failed[0]["result"]["kind"] == "disconnected", "pending request failure was not propagated")
	_check(transport.pending_count() == 0, "pending request survived disconnect")

func _test_authentication_contract() -> void:
	var auth = AuthSession.new()
	var login: Dictionary = auth.begin_login(" alice ", "pw")
	_check(login.get("username", "") == "alice", "username was not normalized")
	_check(login.get("passwd", "") == "pw", "auth.login did not use passwd")
	_check(not login.has("password"), "auth.login retained incorrect password field")
	_check(auth.begin_login("alice", "second").is_empty(), "duplicate login was not blocked")
	_check(auth.handle_response({"status": "ok", "type": "auth.session", "data": {"session": "s1", "player_id": 9}}), "valid login was not accepted")
	_check(auth.is_authenticated() and auth.player_id == 9, "authenticated state was not stored")
	auth.invalidate("test")
	_check(not auth.is_authenticated() and auth.session_token.is_empty(), "session was not invalidated")
	var refused = AuthSession.new()
	refused.begin_login("alice", "pw")
	_check(not refused.handle_response({"status": "refused", "error": {"message": "bad credentials"}}), "refused login was accepted")
	_check(not refused.is_authenticated(), "refused login retained a session")
	var missing = AuthSession.new()
	_check(missing.begin_login("", "pw").is_empty(), "missing username was accepted")
	_check(missing.begin_login("alice", "").is_empty(), "missing password was accepted")

func _test_structured_results() -> void:
	var success: Dictionary = Protocol.result_from_response({"status": "ok", "data": {"value": 1}})
	var refusal: Dictionary = Protocol.result_from_response({"status": "refused", "error": {"code": 403, "message": "Denied"}})
	_check(success["kind"] == "ok", "success result kind incorrect")
	_check(refusal["kind"] == "refused" and refusal["message"] == "Denied", "refusal result was not structured")
	_check(Protocol.timeout_result("test")["kind"] == "timeout", "timeout result kind incorrect")
	_check(Protocol.disconnected_result("lost")["kind"] == "disconnected", "disconnect result kind incorrect")
	var request: Dictionary = Protocol.build_request("r1", "auth.login", {"passwd": "secret"}, "session-token")
	_check(request["id"] == "r1" and request["command"] == "auth.login", "request envelope was malformed")
	_check(request["auth"]["session"] == "session-token", "session was not attached to request")

func _test_event_classification() -> void:
	_check(Protocol.classify_event_type("system.notice") == "system", "system event category incorrect")
	_check(Protocol.classify_event_type("chat.message") == "chat", "chat event category incorrect")
	_check(Protocol.classify_event_type("unknown.broadcast") == "unknown", "unknown event category incorrect")
