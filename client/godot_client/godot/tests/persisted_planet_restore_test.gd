extends SceneTree

const MainScene = preload("res://Main.tscn")

var checks := 0
var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var positive: Variant = await _make_app(180)
	positive._authenticated_once = true
	positive._restore_persisted_planet_surface(_snapshot())
	_check(positive._landed_planet_id == 9003, "matching fresh player and ship records did not restore the persisted planet ID")
	_check(positive._landing_surface_ready, "restored landing did not wait behind the normal confirmed surface refresh")
	_check(positive.gameplay_view.planet_workflow.visible, "restored landing did not open the real planet workflow scene")
	positive.queue_free()
	await process_frame

	for fixture in [
		{"player": 181, "ship": 1014, "sector": 11, "onplanet": 1, "fresh": true},
		{"player": 180, "ship": 1015, "sector": 11, "onplanet": 1, "fresh": true},
		{"player": 180, "ship": 1014, "sector": 12, "onplanet": 1, "fresh": true},
		{"player": 180, "ship": 1014, "sector": 11, "onplanet": 0, "fresh": true},
		{"player": 180, "ship": 1014, "sector": 11, "onplanet": 1, "fresh": false},
	]:
		var candidate: Variant = await _make_app(180)
		candidate._authenticated_once = true
		var invalid := _snapshot()
		invalid["player"]["id"] = fixture["player"]
		invalid["player"]["ship_id"] = fixture["ship"]
		invalid["player"]["sector_id"] = fixture["sector"]
		invalid["ship"]["id"] = fixture["ship"] if fixture["ship"] == 1014 else 1014
		invalid["ship"]["onplanet"] = fixture["onplanet"]
		if not fixture["fresh"]:
			invalid["freshness"]["sector"] = "stale"
		candidate._restore_persisted_planet_surface(invalid)
		_check(candidate._landed_planet_id == 0 and not candidate.gameplay_view.planet_workflow.visible,
			"mismatched or stale auth/location fields exposed a persisted planet surface: %s" % str(fixture))
		candidate.queue_free()
		await process_frame

	if failures.is_empty():
		print("Persisted planet restore checks: %d passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _make_app(player_id: int) -> Variant:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	app.auth_session.player_id = player_id
	return app

func _snapshot() -> Dictionary:
	return {
		"player": {"id": 180, "ship_id": 1014, "sector_id": 11, "last_planet_id": 9003},
		"ship": {"id": 1014, "onplanet": 1},
		"sector": {"id": 11},
		"freshness": {"player": "fresh", "ship": "fresh", "sector": "fresh"},
	}

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
