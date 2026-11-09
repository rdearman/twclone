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
	await app._finish_command_reply({
		"status": "ok", "type": "ship.list.response",
		"data": {"ships": [{"ship_id": 7, "name": "Hauler", "active": true}]},
	}, {"label": "Owned ship roster", "command": "ship.list", "mutating": false})
	_check(app.gameplay_view.information_text.text.contains("Hauler"), "valid ship roster was not presented")
	_check(app.gameplay_view.information_text.text.contains("Ship 7") and app.gameplay_view.information_text.text.contains("ACTIVE"), "owned roster did not present authoritative ship ID and active state")
	_check(refresh.calls.is_empty(), "valid read-only ship roster triggered a refresh")
	app.auth_session.player_id = 11
	await app._finish_command_reply({
		"status": "ok", "type": "ship.transfer.response",
		"data": {"ship_id": 7, "from_player_id": 11, "target_player_id": 42},
	}, {"label": "Transfer ship", "command": "ship.transfer", "mutating": true,
		"data": {"ship_id": 7, "target_player_id": 42}})
	_check(app.gameplay_view.notification_label.text.contains("Ship transfer receipt accepted"), "matching transfer receipt was not acknowledged")
	_check(not app.gameplay_view.notification_label.text.contains("is now owned by player 42"), "transfer presented final ownership before roster refresh")
	_check(refresh.calls.size() == 1 and refresh.calls[0] == ["player", "ship", "sector"], "confirmed transfer did not refresh ownership and sector state")
	await app._finish_command_reply({
		"status": "ok", "type": "ship.transfer.response",
		"data": {"ship_id": 7, "from_player_id": 99, "target_player_id": 42},
	}, {"label": "Transfer ship", "command": "ship.transfer", "mutating": true,
		"data": {"ship_id": 7, "target_player_id": 42}})
	_check(app.gameplay_view.notification_label.text.contains("was not confirmed"), "transfer from another owner was presented as confirmed")
	_check(refresh.calls.size() == 2 and refresh.calls[1] == ["player", "ship", "sector"], "mismatched transfer receipt did not refresh ownership and sector state")

	await app._finish_command_reply({
		"status": "ok", "type": "ship.list.response",
		"data": {"ships": [{"ship_id": 7.0, "name": "Untrusted"}]},
	}, {"label": "Owned ship roster", "command": "ship.list", "mutating": false})
	_check(not app.gameplay_view.information_text.text.contains("Untrusted"), "malformed ship ID was presented")
	_check(refresh.calls.size() == 1 and refresh.calls[0] == ["player", "ship"], "malformed roster did not refresh authoritative ship state")

	await app._finish_command_reply({
		"status": "ok", "type": "ship.list.response",
		"data": {"ships": [
			{"ship_id": 7, "name": "Hauler", "active": true},
			{"ship_id": 7, "name": "Duplicate", "active": false},
		]},
	}, {"label": "Owned ship roster", "command": "ship.list", "mutating": false})
	_check(app.gameplay_view.information_text.text.contains("incomplete ship roster"), "duplicate ship IDs were presented")
	_check(refresh.calls.size() == 2 and refresh.calls[1] == ["player", "ship"], "duplicate roster did not refresh authoritative ship state")

	await app._finish_command_reply({
		"status": "ok", "type": "ship.list.response",
		"data": {"ships": [
			{"ship_id": 7, "name": "Hauler", "active": true},
			{"ship_id": 9, "name": "Scout", "active": true},
		]},
	}, {"label": "Owned ship roster", "command": "ship.list", "mutating": false})
	_check(app.gameplay_view.information_text.text.contains("Scout"), "server-provided multiple-active roster was hidden by a client invariant")

	await app._finish_command_reply({
		"status": "ok", "type": "ship.list.response",
		"data": {"ships": []},
	}, {"label": "Owned ship roster", "command": "ship.list", "mutating": false})
	_check(refresh.calls.size() == 2, "valid empty roster triggered an unnecessary refresh")
	_check(app.gameplay_view.information_text.text.contains("Ships · 0 entries"), "empty authoritative roster was not presented")

	await app._finish_command_reply({
		"status": "refused", "error": {"message": "Ship roster is unavailable."},
	}, {"label": "Owned ship roster", "command": "ship.list", "mutating": false})
	_check(refresh.calls.size() == 3 and refresh.calls[2] == ["player", "ship"], "ship roster refusal did not refresh authoritative ship state")
	_check(app.gameplay_view.notification_label.text.contains("Ship roster is unavailable."), "ship roster refusal was not surfaced")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 ship roster tests: 8 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
