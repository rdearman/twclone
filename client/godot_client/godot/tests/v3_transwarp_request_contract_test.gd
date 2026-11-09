extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame

	# The dispatcher must fail closed before authentication/transport for a
	# malformed request ID; it must not invent a destination or response shape.
	app._on_command_requested("move.transwarp", {"to_sector_id": "42"}, "Transwarp", true)
	_check(app.gameplay_view.notification_label.text.contains("no valid server sector ID"), "string transwarp ID was sent instead of refused locally")
	app._on_command_requested("move.transwarp", {"to_sector_id": 0}, "Transwarp", true)
	_check(app.gameplay_view.notification_label.text.contains("no valid server sector ID"), "non-positive transwarp ID was sent instead of refused locally")

	# The dispatcher must retain the same authoritative freshness boundary as the
	# menu, even when a caller bypasses the disabled action button.
	app.gameplay_view._snapshot = {"freshness": {"sector": "stale"}}
	app._on_command_requested("move.transwarp", {"to_sector_id": 42}, "Transwarp", true)
	_check(app.gameplay_view.notification_label.text.contains("refresh the confirmed sector"), "stale transwarp request bypassed the authoritative sector freshness gate")

	# Freshness is server-owned metadata. A malformed value must fail closed just
	# like an explicitly stale snapshot; it must never authorize a movement RPC.
	app.gameplay_view._snapshot = {"freshness": {"sector": true}, "hud": {"sector_id": 17}}
	app._on_command_requested("move.transwarp", {"to_sector_id": 42}, "Transwarp", true)
	_check(app.gameplay_view.notification_label.text.contains("refresh the confirmed sector"), "malformed transwarp freshness metadata bypassed the authoritative sector gate")

	# A fresh marker without the server's current sector identity is not enough
	# to establish the movement origin. The dispatcher must fail closed just as
	# it does for a stale marker, rather than sending a request from unknown UI
	# state.
	app.gameplay_view._snapshot = {"freshness": {"sector": "fresh"}, "hud": {"sector_id": "42"}}
	app._on_command_requested("move.transwarp", {"to_sector_id": 42}, "Transwarp", true)
	_check(app.gameplay_view.notification_label.text.contains("refresh the confirmed sector"), "fresh transwarp snapshot with a malformed origin ID bypassed the authoritative origin gate")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 transwarp request contract tests: 4 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
