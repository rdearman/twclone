extends SceneTree

const MainScene = preload("res://Main.tscn")

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
	app._landed_planet_id = 77
	app._pending_landing_surface_id = 77
	app._command_refresh_pending = true

	await app._on_refresh_finished({
		"freshness": {"player": "fresh", "ship": "fresh"},
		"ship": {"id": 9, "onplanet": 77.5},
	})

	_check(not app.gameplay_view.planet_workflow.visible, "fractional onplanet identity opened the planet surface")
	_check(not app._landing_surface_ready, "fractional onplanet identity marked the surface ready")
	_check(app._pending_landing_surface_id == 0, "malformed landing refresh remained pending")
	_check(app.gameplay_view.notification_label.text.contains("refresh was incomplete"), "malformed landing refresh was not explained")

	app.queue_free()
	await process_frame

	# A complete refresh with the exact server-issued landed-planet ID is the
	# only successful path to an actionable surface.  This guards the positive
	# side of the same contract as the malformed-identity case above: the
	# landing envelope alone must not open planet controls, but a matching
	# player/ship refresh must.
	var confirmed = MainScene.instantiate()
	root.add_child(confirmed)
	await process_frame
	confirmed.auth_session.handle_response({
		"status": "ok", "type": "auth.session",
		"data": {"session": "test-session", "player_id": 3},
	})
	confirmed._landed_planet_id = 77
	confirmed._pending_landing_surface_id = 77
	confirmed._command_refresh_pending = true

	await confirmed._on_refresh_finished({
		"freshness": {"player": "fresh", "ship": "fresh"},
		"player": {"id": 3},
		"ship": {"id": 9, "onplanet": 77},
	})
	_check(confirmed._landing_surface_ready, "matching landed-planet identity did not mark the surface ready")
	_check(confirmed.gameplay_view.planet_workflow.visible, "matching landed-planet identity did not open the surface")
	_check(confirmed._pending_landing_surface_id == 0, "confirmed landing remained pending after refresh")

	confirmed.queue_free()
	await process_frame

	# A player ID with the right numeric value but the wrong JSON type must not
	# bind the refresh to the authenticated session. Otherwise normalization
	# could make a malformed player record authorize the planet surface.
	var malformed_player = MainScene.instantiate()
	root.add_child(malformed_player)
	await process_frame
	malformed_player.auth_session.handle_response({
		"status": "ok", "type": "auth.session",
		"data": {"session": "test-session", "player_id": 3},
	})
	malformed_player._landed_planet_id = 77
	malformed_player._pending_landing_surface_id = 77
	malformed_player._command_refresh_pending = true

	await malformed_player._on_refresh_finished({
		"freshness": {"player": "fresh", "ship": "fresh"},
		"player": {"id": 3.0},
		"ship": {"id": 9, "onplanet": 77},
	})
	_check(not malformed_player._landing_surface_ready, "integral-float player identity authorized the planet surface")
	_check(malformed_player._landed_planet_id == 0, "schema-mismatched player identity was retained")

	malformed_player.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 planet landing identity tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
