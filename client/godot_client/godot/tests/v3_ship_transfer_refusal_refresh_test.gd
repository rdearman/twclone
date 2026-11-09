extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeRefresh extends RefCounted:
	var calls: Array = []

	func refresh(_force: bool = false, domains: Array = []) -> bool:
		calls.append(domains.duplicate())
		return true

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var refresh := FakeRefresh.new()
	app.refresh_coordinator = refresh
	app.auth_session.player_id = 11

	await app._finish_command_reply({
		"status": "refused", "type": "error",
		"error": {"message": "Ship transfer is not available."}
	}, {"label": "Transfer ship", "command": "ship.transfer", "mutating": true,
		"data": {"ship_id": 7, "target_player_id": 42}})

	_check(app.gameplay_view.notification_label.text.contains("Ship transfer is not available."), "transfer refusal was not surfaced")
	_check(refresh.calls.size() == 1 and refresh.calls[0] == ["player", "ship", "sector"], "transfer refusal did not reconcile ownership and sector state")
	_check(not app.gameplay_view.notification_label.text.contains("confirmed"), "transfer refusal was presented as confirmed")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 ship transfer refusal refresh tests: 3 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
