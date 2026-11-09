extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []
var requests: Array[Dictionary] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	app.gameplay_view.command_requested.connect(_capture_request)

	# A normal sector-object action must forward the server-provided integer ID
	# and must not substitute a display/legacy alias or any local planet data.
	app.gameplay_view._add_object_actions("planet", {
		"data": {"planet_id": 77, "id": 9001, "name": "Authoritative target"}
	})
	var action: Button
	for child in app.gameplay_view.context_action_list.get_children():
		if child is Button and child.text == "LAND ON PLANET":
			action = child
			break
	_check(is_instance_valid(action), "valid authoritative planet action was not rendered")
	if is_instance_valid(action):
		action.pressed.emit()

	_check(requests.size() == 1, "valid authoritative planet action did not emit exactly one request")
	if requests.size() == 1:
		_check(requests[0]["command"] == "planet.land", "planet action emitted the wrong command")
		_check(requests[0]["data"] == {"planet_id": 77}, "planet action did not send only the authoritative planet ID")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 planet landing authoritative-ID test passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _capture_request(command: String, data: Dictionary, _label: String, _mutating: bool) -> void:
	requests.append({"command": command, "data": data})

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
