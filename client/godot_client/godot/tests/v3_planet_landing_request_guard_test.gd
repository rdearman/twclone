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

	for malformed_id in [null, true, 77.0, "77"]:
		requests.clear()
		var before := app.gameplay_view.context_action_list.get_child_count()
		app.gameplay_view._add_object_actions("planet", {"data": {"planet_id": malformed_id, "id": 9001}})
		var added := app.gameplay_view.context_action_list.get_child_count() - before
		_check(added == 2, "malformed planet ID %s did not render both unavailable actions" % str(malformed_id))
		for index in range(before, before + added):
			var action: Button = app.gameplay_view.context_action_list.get_child(index)
			_check(action.disabled, "malformed planet ID %s exposed an enabled action" % str(malformed_id))
			_check(action.has_meta("unavailable_reason"), "malformed planet ID %s omitted its unavailable reason" % str(malformed_id))
			action.pressed.emit()
		_check(requests.is_empty(), "malformed planet ID %s emitted a server request" % str(malformed_id))

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 planet landing request guard tests passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _capture_request(command: String, data: Dictionary, _label: String, _mutating: bool) -> void:
	requests.append({"command": command, "data": data})

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
