extends SceneTree

const AuthSession = preload("res://AuthSession.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	for malformed_id in [null, true, 3.0, "3", 0, -1]:
		var auth := AuthSession.new()
		var accepted := auth.handle_response({
			"status": "ok",
			"type": "auth.session",
			"data": {"session": "test-session", "player_id": malformed_id},
		})
		_check(not accepted, "malformed player_id %s authenticated" % str(malformed_id))
		_check(not auth.is_authenticated(), "malformed player_id %s left the session authenticated" % str(malformed_id))

	var valid := AuthSession.new()
	_check(valid.handle_response({
		"status": "ok",
		"type": "auth.session",
		"data": {"session": "test-session", "player_id": 3},
	}), "valid player identity was rejected")
	_check(valid.is_authenticated() and valid.player_id == 3, "valid player identity was not retained")

	if failures.is_empty():
		print("Godot V3 planet landing session identity tests passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
