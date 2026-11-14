extends SceneTree

const Transport = preload("res://ProtocolTransport.gd")

var failures := 0

func _initialize() -> void:
	var transport = Transport.new()
	for index in range(101):
		transport._record_request("req-%03d" % index, "sector.info", {
			"sector_id": index,
			"session_token": "NeverCaptureSessionToken",
			"email": "captain@example.test",
			"opaque": "NeverCaptureUnknownField",
		})
	var recent: Array = transport.diagnostic_snapshot(100)
	_check(recent.size() == 100, "diagnostic buffer did not enforce the 100 exchange limit")
	_check(recent[0].get("request_id") == "req-001", "diagnostic buffer did not evict the oldest exchange")
	var output := JSON.stringify(recent)
	_check(not output.contains("NeverCaptureSessionToken"), "session token appeared in diagnostics")
	_check(not output.contains("captain@example.test"), "email appeared in diagnostics")
	_check(not output.contains("NeverCaptureUnknownField"), "unknown field appeared in diagnostics")
	transport._record_request("auth-1", "auth.login", {"username": "captain", "passwd": "NeverCapturePassword"})
	var auth_entry: Dictionary = transport.diagnostic_snapshot(10).back()
	_check(auth_entry.get("request") == {"omitted": true}, "authentication payload was not omitted")
	if failures == 0:
		print("feedback_diagnostics_test: PASS")
	else:
		push_error("feedback_diagnostics_test: %d failure(s)" % failures)
	quit(0 if failures == 0 else 1)

func _check(condition: bool, message: String) -> void:
	if condition:
		return
	failures += 1
	push_error(message)
