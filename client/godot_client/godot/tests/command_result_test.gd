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
	await app._finish_command_reply(
		{"status": "ok", "data": {"success": true}},
		{"label": "Change password", "command": "auth.change_password", "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("Password changed"), "password change success was not acknowledged")
	_check(not app.gameplay_view.notification_label.text.contains("qa-new-ephemeral"), "password change success notification exposed a credential")
	app.gameplay_view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 3},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"ship": {},
		"sector": {},
	})
	app.gameplay_view.enter_port_workflow({"port": {"id": 99, "name": "Previously inspected", "type": 9, "commodities": []}})
	_check(app.gameplay_view.port_workflow.visible, "port refusal fixture did not open the prior port screen")
	await app._finish_command_reply(
		{"status": "refused", "error": {"message": "Port information is available only in its sector"}},
		{"label": "Open port", "command": "port.info", "mutating": false}
	)
	_check(not app.gameplay_view.port_workflow.visible, "refused remote port.info left stale port inventory visible")
	_check(app.gameplay_view.notification_label.text.contains("Port information refused") and app.gameplay_view.notification_label.text.contains("only in its sector"), "port proximity refusal was not explained")
	await app._finish_command_reply(
		{"status": "error", "error": {"code": 1101, "message": "Invalid old password"}},
		{"label": "Change password", "command": "auth.change_password", "mutating": true}
	)
	_check(app.gameplay_view.notification_label.text.contains("current password was not accepted"), "wrong-current-password response was not explained")
	_check(not app.gameplay_view.notification_label.text.contains("Invalid old password"), "password refusal echoed the server's raw credential-related message")
	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot command/password tests: 9 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
