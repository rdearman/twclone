extends SceneTree

const MainScene = preload("res://Main.tscn")

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
	app.gameplay_view.set_snapshot({
		"authenticated": true, "disconnected": false,
		"hud": {"sector_id": 7},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": {"sector_id": 7, "ships_present": []},
	})
	var operation := {
		"label": "Inspect ship", "command": "ship.inspect", "mutating": false,
		"data": {"ship_id": 88.0},
	}
	await app._finish_command_reply({
		"status": "ok", "type": "ship.inspect",
		"data": {"ship": {"ship_id": 88, "name": "Wrongly typed target"}},
	}, operation)
	assert app.gameplay_view.notification_label.text.contains("omitted a valid ship ID")
	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "data": {"ship": {"ship_id": 88, "name": "Missing schema type"}},
	}, operation)
	assert app.gameplay_view.notification_label.text.contains("unexpected response type")
	assert app.refresh_coordinator.calls.size() == 1
	app.queue_free()
	await process_frame
	quit(0)
