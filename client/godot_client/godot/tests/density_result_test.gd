extends SceneTree

const CommandMenu = preload("res://CommandMenu.gd")
var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var menu := CommandMenu.new()
	root.add_child(menu)
	await process_frame
	var text := menu._format_density_result({"sectors": [21, 21, 213, 100, 808, 0]})
	_check(text.contains("Sector 21 · Density: 21"), "valid integer density row was not formatted")
	_check(text.contains("Sector 213 · Density: 100"), "density output lost sector 213 or its measurement label")
	_check(text.contains("Sector 808 · Density: 0"), "zero density was not preserved")
	_check(not text.contains("unavailable"), "valid density rows were rendered as unavailable")

	var malformed := menu._format_density_result({"sectors": [21.0, 21, 213, 100]})
	_check(malformed.contains("No density values were returned by the server."), "float sector ID was coerced into authoritative visibility")
	_check(not malformed.contains("Sector 21"), "malformed density row leaked into the result")
	menu.queue_free()
	if failures.is_empty():
		print("Godot density result tests: 5 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
