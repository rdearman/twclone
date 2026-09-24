extends SceneTree

const GameplayView = preload("res://GameplayView.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var view = GameplayView.new()
	root.add_child(view)
	await process_frame
	var commands = view.command_menu
	_check(commands is PanelContainer, "general commands are not a docked panel")
	_check(commands.get_parent() == view.gameplay_body and view.gameplay_body.get_child(0) == commands, "command panel is not persistently placed on the left")
	view.set_snapshot({
		"authenticated": true,
		"disconnected": false,
		"hud": {"sector_id": 17},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": {"id": 17, "adjacent_sector_ids": [18], "ports": [], "planets": [], "ships": []},
	})
	var activations: Array[int] = []
	view.warp_activated.connect(func(destination: int) -> void: activations.append(destination))
	view._on_warp_selected("warp:18", 18)
	_check(activations.is_empty(), "single click moved instead of selecting the adjacent sector")
	var double_click := InputEventMouseButton.new()
	double_click.button_index = MOUSE_BUTTON_LEFT
	double_click.pressed = true
	double_click.double_click = true
	view._on_warp_row_gui_input(double_click, 18)
	_check(activations == [18], "double-click did not activate the selected warp destination")
	view.set_snapshot({
		"authenticated": true,
		"disconnected": true,
		"hud": {"sector_id": 17},
		"freshness": {"sector": "disconnected", "overall": "disconnected"},
		"sector": {"id": 17, "adjacent_sector_ids": [18], "ports": [], "planets": [], "ships": []},
	})
	view.request_warp_now(18)
	_check(activations == [18], "stale/disconnected state allowed a warp")
	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot gameplay view tests: 5 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
