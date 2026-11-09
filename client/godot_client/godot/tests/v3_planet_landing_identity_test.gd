extends SceneTree

const ClientState = preload("res://ClientState.gd")
const Refresh = preload("res://AuthoritativeRefresh.gd")

class FakeTransport extends RefCounted:
	var next_id := 1
	var calls: Array[Dictionary] = []

	func request(command: String, _data: Dictionary, _token: String) -> String:
		var request_id := "refresh-%d" % next_id
		next_id += 1
		calls.append({"id": request_id, "command": command})
		return request_id

class FakeAuth extends RefCounted:
	var player_id := 3

	func is_authenticated() -> bool:
		return true

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var transport := FakeTransport.new()
	var state := ClientState.new()
	state.mark_authenticated()
	var refresh := Refresh.new(transport, FakeAuth.new(), state)
	_check(refresh.refresh(false, ["player"]), "player refresh did not start")
	var request_id: String = transport.calls[0]["id"]
	_check(refresh.handle_reply(request_id, {
		"status": "ok", "type": "player.info",
		"data": {"player": {"id": 99, "sector": 77, "turns_remaining": 1}},
	}, "player.my_info"), "mismatched player reply was not processed")
	_check(state.player_availability == ClientState.PARTIAL, "mismatched player reply was marked fresh")
	_check(state.player.is_empty(), "mismatched player reply replaced the confirmed player")

	if failures.is_empty():
		print("Godot V3 planet landing identity tests: 3 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
