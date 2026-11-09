extends SceneTree

const MainScene = preload("res://Main.tscn")
const ClientState = preload("res://ClientState.gd")

class FakeRefresh extends RefCounted:
	var calls: Array = []

	func accepts_request(_request_id: String) -> bool:
		return false

	func refresh(_force: bool = false, domains: Array = []) -> bool:
		calls.append({"force": _force, "domains": domains})
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
	var refresh := FakeRefresh.new()
	app.refresh_coordinator = refresh
	var known_sector := ClientState.normalize_sector({
		"sector_id": 12, "name": "Known sector",
		"ports": [{"id": 8, "name": "Known port"}],
		"celestial_objects": [], "ships_present": [],
	})
	app.gameplay_view.set_snapshot({
		"authenticated": true, "disconnected": false,
		"hud": {"sector_id": 12},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": known_sector,
	})

	await app._finish_command_reply({
		"status": "refused",
		"error": {
			"message": "Density is unavailable for a remote sector",
			"data": {"sector_id": 999, "density": 42},
		},
	}, {
		"label": "Sector density scan", "command": "sector.scan.density", "mutating": false,
	})

	_check(app.gameplay_view._snapshot["sector"]["sector_id"] == 12,
		"refused density scan replaced the confirmed sector")
	_check(not str(app.gameplay_view.information_text.text).contains("42"),
		"refused density payload was rendered as authoritative")
	_check(app.gameplay_view.notification_label.text.contains("Density is unavailable for a remote sector"),
		"density refusal reason was not shown")
	_check(refresh.calls.size() == 1,
		"refused density scan did not request an authoritative sector refresh")
	if refresh.calls.size() == 1:
		_check(refresh.calls[0]["domains"] == ["sector"],
			"density refusal refreshed unrelated state domains")

	# A success-shaped envelope from another command is just as unsafe as a
	# refusal payload. It must not be rendered as density data or replace the
	# confirmed sector cache.
	refresh.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "sector.scan",
		"data": {"sectors": [{"sector_id": 999, "density": 77}]},
	}, {
		"label": "Sector density scan", "command": "sector.scan.density", "mutating": false,
	})
	_check(app.gameplay_view._snapshot["sector"]["sector_id"] == 12,
		"mismatched density response replaced the confirmed sector")
	_check(not str(app.gameplay_view.information_text.text).contains("77"),
		"mismatched density response rendered an untrusted payload")
	_check(refresh.calls.size() == 1 and refresh.calls[0]["domains"] == ["sector"],
		"mismatched density response did not refresh only the confirmed sector")

	# Detailed scan refusals must use the same fail-closed sector refresh path.
	refresh.calls.clear()
	await app._finish_command_reply({
		"status": "refused",
		"error": {"message": "Detailed scan requires the current sector."},
		"data": {"sector_id": 999, "name": "Untrusted remote sector"},
	}, {
		"label": "Detailed scan", "command": "move.scan", "mutating": false,
	})
	_check(refresh.calls.size() == 1,
		"refused detailed scan did not request an authoritative sector refresh")
	if refresh.calls.size() == 1:
		_check(refresh.calls[0]["domains"] == ["sector"],
			"detailed scan refusal refreshed unrelated state domains")
	_check(not str(app.gameplay_view.information_text.text).contains("Untrusted remote sector"),
		"refused detailed scan rendered untrusted payload data")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 visibility density refusal tests: 11 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
