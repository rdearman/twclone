extends SceneTree

const GameplayView = preload("res://GameplayView.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var view := GameplayView.new()
	root.add_child(view)
	await process_frame

	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 17.0},
		"sector": {"id": 17},
	})
	_check(view._current_sector_id() == 0, "float HUD sector ID became a local-note key")

	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": "17"},
		"sector": {"id": 17},
	})
	_check(view._current_sector_id() == 0, "string HUD sector ID became a local-note key")

	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 17},
		"sector": {"id": 17},
	})
	_check(view._current_sector_id() == 17, "authoritative integer HUD sector ID was rejected")

	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 visibility sector identity tests: 3 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
