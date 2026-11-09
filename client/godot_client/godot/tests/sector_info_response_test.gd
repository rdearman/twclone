extends SceneTree

const MainScene = preload("res://Main.tscn")
const ClientState = preload("res://ClientState.gd")

class FakeRefresh extends RefCounted:
	var calls: Array = []

	func accepts_request(_request_id: String) -> bool:
		return false

	func refresh(_force: bool = false, _domains: Array = []) -> bool:
		calls.append({"force": _force, "domains": _domains})
		return true

var failures: Array[String] = []

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
	app.refresh_coordinator = FakeRefresh.new()
	var known_sector := ClientState.normalize_sector({
		"sector_id": 12, "name": "Known sector",
		"ports": [], "celestial_objects": [], "ships_present": [],
	})
	app.gameplay_view.set_snapshot({
		"authenticated": true, "disconnected": false,
		"hud": {"sector_id": 12},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": known_sector,
	})

	await app._finish_command_reply({
		"status": "ok", "type": "sector.info.result",
		"data": {"sector_id": 999, "name": "Untrusted remote sector"},
	}, {
		"label": "Current sector information", "command": "sector.info", "mutating": false,
	})
	_check(app.gameplay_view._snapshot["sector"]["sector_id"] == 12,
		"mismatched sector.info replaced the confirmed sector")
	_check(not str(app.gameplay_view.notification_label.text).contains("Untrusted remote sector"),
		"mismatched sector.info payload was rendered")
	_check(app.gameplay_view.notification_label.text.contains("confirmed location is sector 12"),
		"mismatched sector.info was not explained")
	_check(app.refresh_coordinator.calls.size() == 1,
		"mismatched sector.info did not request an authoritative refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"],
			"mismatched sector.info refreshed unrelated state domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "sector.info.result",
		"data": {"name": "Unidentified remote sector"},
	}, {
		"label": "Current sector information", "command": "sector.info", "mutating": false,
	})
	_check(not str(app.gameplay_view.notification_label.text).contains("Unidentified remote sector"),
		"sector.info without an authoritative ID was rendered")
	_check(app.gameplay_view.notification_label.text.contains("omitted a valid sector ID"),
		"missing sector.info ID was not explained")
	_check(app.refresh_coordinator.calls.size() == 1,
		"missing sector.info ID did not request an authoritative refresh")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot sector.info response tests: passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
