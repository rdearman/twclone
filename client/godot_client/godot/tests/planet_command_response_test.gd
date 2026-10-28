extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeTransport extends RefCounted:
	var calls: Array[Dictionary] = []
	var next_id := 1

	func request(command: String, data: Dictionary, session_token: String = "") -> String:
		calls.append({"command": command, "data": data, "session_token": session_token})
		var request_id := "test-%d" % next_id
		next_id += 1
		return request_id

	func poll(_delta: float = 0.0) -> void:
		pass

	func drain_events() -> Array:
		return []

	func close(_reason: String = "Client disconnected.") -> void:
		pass

class FakeRefresh extends RefCounted:
	func refresh(_force: bool = false, _domains: Array = []) -> bool:
		return true

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	app.auth_session.handle_response({
		"status": "ok", "type": "auth.session",
		"data": {"session": "test-session", "player_id": 3},
	})
	var fake_transport := FakeTransport.new()
	app.transport = fake_transport
	app.refresh_coordinator = FakeRefresh.new()
	app._landed_planet_id = 77
	app.gameplay_view.enter_planet_workflow(77)
	fake_transport.calls.clear()

	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Insufficient equipment."}},
		{"label": "Build citadel", "command": "citadel.build", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("Insufficient equipment"), "citadel resource refusal was not shown")
	_check(fake_transport.calls.is_empty(), "refused construction triggered a refresh or another mutation")

	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "You do not control this planet."}},
		{"label": "Assign planet workers", "command": "planet.colonists.allocate", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("You do not control this planet"), "invalid-ownership refusal was not shown")
	_check(fake_transport.calls.is_empty(), "refused workforce allocation triggered a refresh or another mutation")

	await app._finish_command_reply(
		{"status": "error", "error": {"message": "Database temporarily unavailable."}},
		{"label": "Deposit to planet", "command": "planet.deposit", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("Database temporarily unavailable"), "server error was not shown with its reason")
	_check(fake_transport.calls.is_empty(), "failed stock transfer triggered a refresh or mutation retry")

	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Insufficient ship fighter capacity."}},
		{"label": "Planet fighter transfer", "command": "planet.fighters.transfer", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("Insufficient ship fighter capacity"), "fighter transfer capacity refusal was not shown")
	_check(fake_transport.calls.is_empty(), "rejected fighter transfer triggered a refresh or second mutation")

	await app._finish_command_reply(
		{"status": "ok", "type": "citadel.upgrade_started", "data": {"target_level": 2, "days_to_complete": 4}},
		{"label": "Upgrade citadel", "command": "citadel.upgrade", "mutating": true},
	)
	_check(app.gameplay_view.notification_label.text.contains("Upgrade citadel complete"), "successful citadel mutation was not acknowledged")
	_check(app._landed_planet_id == 77, "successful citadel action lost landed-planet context")
	_check(fake_transport.calls.size() == 2, "successful planet mutation did not request fresh planet details")
	if fake_transport.calls.size() == 2:
		_check(fake_transport.calls[0]["command"] == "planet.info", "success refresh did not request authoritative planet information")
		_check(fake_transport.calls[1]["command"] == "planet.colonists.get", "success refresh did not request colonist/cargo information")
		_check(fake_transport.calls.all(func(call: Dictionary) -> bool: return not str(call["command"]).begins_with("citadel.")), "success refresh repeated the citadel mutation")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot planet command response tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
