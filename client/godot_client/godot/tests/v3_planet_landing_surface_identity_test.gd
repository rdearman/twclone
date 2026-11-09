extends SceneTree

const CommandMenuScene = preload("res://CommandMenu.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var menu = CommandMenuScene.new()
	root.add_child(menu)
	await process_frame

	menu._snapshot = {"ship": {"onplanet": 77}, "hud": {"sector_id": 20}}
	menu._selection = {"kind": "planet", "data": {"planet_id": 88}}
	menu._active_category = "PLANET"
	menu._render_actions()

	_check(_action_button(menu, "Launch from planet") == null, "different selected planet exposed launch")
	_check(_action_button(menu, "Planet colonist status") == null, "different selected planet exposed colonist status")
	_check(_action_button(menu, "Build planet citadel") == null, "different selected planet exposed citadel build")
	var unavailable := _action_button(menu, "Planet surface actions unavailable · different landed planet")
	_check(unavailable != null and unavailable.disabled, "different selected planet lacked an unavailable surface notice")

	menu._selection = {"kind": "planet", "data": {"planet_id": 77}}
	menu._render_actions()
	_check(_action_button(menu, "Launch from planet") != null, "matching landed planet hid launch")
	_check(_action_button(menu, "Planet colonist status") != null, "matching landed planet hid colonist status")
	_check(_action_button(menu, "Build planet citadel") != null, "matching landed planet hid citadel build")

	menu.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 planet landing surface identity tests passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _action_button(menu, label: String) -> Button:
	for child in menu._action_list.get_children():
		if child is Button and (child as Button).text == label:
			return child
	return null

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
