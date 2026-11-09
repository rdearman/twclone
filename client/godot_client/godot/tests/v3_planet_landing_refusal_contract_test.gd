extends SceneTree

const MainScene = preload("res://Main.tscn")

class FakeRefresh extends RefCounted:
	var calls: Array = []

	func refresh(_force: bool = false, domains: Array = []) -> bool:
		calls.append(domains.duplicate())
		return true

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame

	var fake_refresh := FakeRefresh.new()
	app.refresh_coordinator = fake_refresh
	app._landed_planet_id = 66
	app._landing_surface_ready = true
	app.gameplay_view.enter_planet_workflow(66)

	# A refusal remains authoritative even when a peer labels it with a
	# different operation type and includes diagnostic data that looks like a
	# successful landing. The client must not claim the requested planet.
	await app._finish_command_reply({
		"status": "refused",
		"type": "planet.info",
		"error": {
			"message": "Planetary defenses refused landing.",
			"data": {"planet_id": 77, "landing_allowed": true},
		},
	}, {
		"label": "Land on planet",
		"command": "planet.land",
		"data": {"planet_id": 77},
		"mutating": true,
	})

	_check(fake_refresh.calls.size() == 1, "landing refusal did not reconcile authoritative state")
	if fake_refresh.calls.size() == 1:
		_check(fake_refresh.calls[0] == ["player", "ship", "sector"], "landing refusal reconciled the wrong domains")
	_check(app._landed_planet_id == 66, "refusal diagnostic data replaced the confirmed planet")
	_check(app._pending_landing_surface_id == 0, "landing refusal retained a pending transition")
	_check(app.gameplay_view.planet_workflow.visible, "refusal hid the previously confirmed surface")
	_check(app.gameplay_view.notification_label.text.contains("Planetary defenses refused landing"), "refusal reason was not shown")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 planet landing refusal contract tests passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
