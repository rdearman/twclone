extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	# Regression: a server refusal must reach the player as a readable message,
	# rather than being treated as a successful mutation or silently discarded.
	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Insufficient credits"}},
		{"label": "Ship repair", "command": "ship.repair", "mutating": true}
	)
	_check(app.gameplay_view.notification_label.visible, "server refusal did not display a notification")
	_check(app.gameplay_view.notification_label.text == "Ship repair refused · Insufficient credits", "server refusal was not shown with command context and server reason")
	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot command refusal tests: 2 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
