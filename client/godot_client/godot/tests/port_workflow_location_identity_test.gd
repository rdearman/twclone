extends SceneTree

const GameplayView = preload("res://GameplayView.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var view = GameplayView.new()
	root.add_child(view)
	await process_frame
	var port = {"port": {"id": 7, "name": "Helix", "commodities": []}}
	for malformed_sector in [true, 17.0, "17", 0, -17]:
		view._snapshot = {"hud": {"sector_id": malformed_sector}}
		view.enter_port_workflow(port)
		_check(not view.port_workflow.visible, "malformed sector identity opened an actionable trade surface: %s" % str(malformed_sector))
	view._snapshot = {"hud": {"sector_id": 17}}
	view.enter_port_workflow(port)
	_check(view.port_workflow.visible, "valid authoritative sector identity did not open the port workflow")
	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot port workflow location identity tests: 6 passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
