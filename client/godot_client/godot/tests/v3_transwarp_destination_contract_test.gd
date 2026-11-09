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
	var fake_refresh := FakeRefresh.new()
	app.refresh_coordinator = fake_refresh

	await app._finish_command_reply(
		{"status": "ok", "type": "move.transwarp", "data": {"status": "transwarp_complete"}},
		{"label": "Transwarp", "command": "move.transwarp", "mutating": true, "data": {"to_sector_id": 42}}
	)

	_check(fake_refresh.calls == [["player", "ship", "sector"]], "unconfirmed transwarp did not refresh all movement state")
	_check(app.gameplay_view.notification_label.text.contains("Transwarp was not confirmed"), "unconfirmed transwarp was presented as complete")

	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "move.transwarp", "data": {"current_sector": 42, "status": "transwarp_complete"}},
		{"label": "Transwarp", "command": "move.transwarp", "mutating": true, "data": {"to_sector_id": 42}}
	)
	_check(fake_refresh.calls == [["player", "ship", "sector"]], "noncanonical transwarp destination alias was accepted")
	_check(app.gameplay_view.notification_label.text.contains("Transwarp was not confirmed"), "noncanonical transwarp destination was presented as complete")

	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "move.transwarp", "data": {"sector_id": 42, "status": "transwarp_complete"}},
		{"label": "Transwarp", "command": "move.transwarp", "mutating": true, "data": {"to_sector_id": 42}}
	)
	_check(app.gameplay_view.notification_label.text == "Transwarp confirmed · Sector 42.", "canonical transwarp destination was not presented to the player")
	_check(fake_refresh.calls == [["player", "ship", "sector"]], "confirmed transwarp did not refresh all movement state")

	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "ok", "type": "move.transwarp", "data": {"sector_id": 42}},
		{"label": "Transwarp", "command": "move.transwarp", "mutating": true, "data": {"to_sector_id": 42}}
	)
	_check(fake_refresh.calls == [["player", "ship", "sector"]], "transwarp without canonical completion status did not refresh movement state")
	_check(app.gameplay_view.notification_label.text.contains("Transwarp was not confirmed"), "transwarp without canonical completion status was presented as complete")

	fake_refresh.calls.clear()
	await app._finish_command_reply(
		{"status": "error", "type": "move.transwarp", "error": {
			"message": "Transwarp refused.",
			"data": {"hazards": [{"hazard_type": "untrusted"}]}
		}},
		{"label": "Transwarp", "command": "move.transwarp", "mutating": true, "data": {"to_sector_id": 42}}
	)
	_check(fake_refresh.calls == [["player", "ship", "sector"]], "negative envelope with success type did not refresh movement state")
	_check(app.gameplay_view.notification_label.text.contains("Transwarp response was not recognized"), "negative envelope with success type was accepted")
	_check(not app.gameplay_view.information_text.text.contains("untrusted"), "negative envelope with success type exposed hazard data")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 transwarp destination contract test: 7 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
