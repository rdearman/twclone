extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame

	for malformed_id in [null, true, 42.0, "42", 0, -1]:
		_check(app._authoritative_positive_id(malformed_id) == 0,
			"malformed warp destination %s became a server sector ID" % str(malformed_id))
	_check(app._authoritative_positive_id(42) == 42,
		"valid integer warp destination was rejected")

	app._on_warp_requested(true)
	_check(app.gameplay_view.notification_label.text.contains("no valid server sector ID"),
		"malformed warp destination did not remain unavailable")
	_check(app._pending_game_commands.is_empty(),
		"malformed warp destination created a pending command")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 warp request ID guard tests: 9 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
