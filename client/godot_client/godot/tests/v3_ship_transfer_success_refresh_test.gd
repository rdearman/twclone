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
		"status": "ok", "type": "ship.transfer.response",
		"data": {"ship_id": 7, "from_player_id": 11, "target_player_id": 42}
	}, {"label": "Transfer ship", "command": "ship.transfer", "mutating": true,
		"data": {"ship_id": 7, "target_player_id": 42}})

	_check(app.gameplay_view.notification_label.text.contains("receipt accepted"), "typed transfer receipt was not acknowledged")
	_check(refresh.calls.size() == 1 and refresh.calls[0] == ["player", "ship", "sector"], "accepted transfer did not reconcile affected authoritative domains")
	_check(app._ship_roster_refresh_after_command, "accepted transfer did not retain the owned-roster refresh marker")
	_check(not app.gameplay_view.notification_label.text.contains("not confirmed"), "valid transfer receipt was presented as unconfirmed")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 ship transfer success refresh tests: 4 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
