extends SceneTree

const ClientState = preload("res://ClientState.gd")
const Refresh = preload("res://AuthoritativeRefresh.gd")

class FakeTransport extends RefCounted:
	var calls: Array[Dictionary] = []
	var next_id := 1

	func request(command: String, _data: Dictionary, _token: String) -> String:
		var request_id := "visibility-refresh-%d" % next_id
		next_id += 1
		calls.append({"id": request_id, "command": command})
		return request_id

class FakeAuth extends RefCounted:
	func is_authenticated() -> bool:
		return true

var failures: Array[String] = []
var finished := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var transport := FakeTransport.new()
	var state := ClientState.new()
	state.mark_authenticated()
	var refresh := Refresh.new(transport, FakeAuth.new(), state)
	refresh.refresh_finished.connect(func(_snapshot: Dictionary) -> void: finished += 1)
	refresh.refresh_failed.connect(func(_snapshot: Dictionary) -> void: failed += 1)

	_check(refresh.refresh(false, ["sector"]), "sector refresh did not start")
	var omitted_type_id: String = transport.calls[0]["id"]
	_check(refresh.handle_reply(omitted_type_id, {
		"status": "ok",
		"data": {"sector_id": 12, "name": "Confirmed Sector"},
	}, "sector.info"), "refresh with omitted type was not handled")
	_check(finished == 1 and failed == 0, "omitted response type did not produce a fresh sector refresh")
	_check(state.sector.get("id") == 12, "omitted response type did not retain authoritative sector identity")

	_check(refresh.refresh(true, ["sector"]), "empty-type sector refresh did not start")
	var empty_type_id: String = transport.calls[1]["id"]
	_check(refresh.handle_reply(empty_type_id, {
		"status": "ok", "type": "",
		"data": {"sector_id": 12, "name": "Confirmed Again"},
	}, "sector.info"), "refresh with empty type was not handled")
	_check(finished == 2 and failed == 0, "empty response type did not produce a fresh sector refresh")

	_check(refresh.refresh(true, ["sector"]), "mismatch test refresh did not start")
	var mismatch_id: String = transport.calls[2]["id"]
	_check(refresh.handle_reply(mismatch_id, {
		"status": "ok", "type": "player.info",
		"data": {"sector_id": 999, "name": "Untrusted Sector"},
	}, "sector.info"), "explicit mismatch response was not handled")
	_check(finished == 2 and failed == 1, "explicit mismatched type was accepted as a fresh refresh")
	_check(state.sector.get("id") == 12 and state.sector.get("name") == "Confirmed Again",
		"explicit mismatched type replaced the confirmed sector")

	if failures.is_empty():
		print("V3 visibility refresh type compatibility: 9 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
