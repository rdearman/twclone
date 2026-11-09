extends SceneTree

const GameplayView = preload("res://GameplayView.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var view = GameplayView.new()
	root.add_child(view)
	await process_frame

	_check(view._authoritative_object_id({"id": 8}, ["port_id", "id"]) == 8,
		"valid port ID was not accepted")
	_check(view._authoritative_object_id({"id": true}, ["port_id", "id"]) == 0,
		"boolean port ID was treated as authoritative")
	_check(view._authoritative_object_id({"id": 8.0}, ["port_id", "id"]) == 0,
		"float port ID was treated as authoritative")
	_check(view._authoritative_object_id({"id": 0, "port_id": "8"}, ["port_id", "id"]) == 0,
		"string port ID was treated as authoritative")

	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 visibility port ID guard tests: 4 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
