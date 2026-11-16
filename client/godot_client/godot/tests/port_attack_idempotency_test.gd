extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeTransport:
	extends RefCounted
	var calls: Array[Dictionary] = []
	func poll(_delta: float) -> void:
		pass
	func drain_events() -> Array:
		return []
	func close(_reason: String = "") -> void:
		pass
	func request(command: String, data: Dictionary, token: String) -> String:
		calls.append({"command": command, "data": data.duplicate(true), "token": token})
		return "port-attack-%d" % calls.size()

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var fake := FakeTransport.new()
	app.transport = fake
	app.auth_session.state = 2 # TwAuthSession.State.AUTHENTICATED
	app.auth_session.session_token = "test-session"
	app.auth_session.player_id = 41
	app.gameplay_view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 16},
		"freshness": {"sector": "fresh"},
	})
	app._on_command_requested("combat.attack.port", {"port_id": 44}, "Attack StarPort", true)
	_check(fake.calls.size() == 1, "confirmed port attack did not reach the transport")
	var first_key := str(fake.calls[0].get("data", {}).get("idempotency_key", "")) if not fake.calls.is_empty() else ""
	_check(first_key.begins_with("godot-") and first_key.length() > 20, "port attack request lacked a random idempotency key")
	# Model a committed attack whose response was lost: the command is no longer
	# in flight, but the request key remains until a definitive server response.
	app._pending_game_commands.clear()
	app.gameplay_view.set_command_busy(false)
	app._on_command_requested("combat.attack.port", {"port_id": 44}, "Retry StarPort attack", true)
	_check(fake.calls.size() == 2, "uncertain attack could not be explicitly retried")
	if fake.calls.size() >= 2:
		_check(str(fake.calls[1].get("data", {}).get("idempotency_key", "")) == first_key,
			"retry after an ambiguous reply generated a new idempotency key")
	app._pending_game_commands.clear()
	app.gameplay_view.set_command_busy(false)
	app._on_command_requested("combat.attack.port", {"port_id": 45}, "Attack another StarPort", true)
	if fake.calls.size() >= 3:
		_check(str(fake.calls[2].get("data", {}).get("idempotency_key", "")) != first_key,
			"a different target reused the previous port's idempotency key")
	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot port-attack idempotency checks: %d passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
