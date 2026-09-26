extends SceneTree

const CommandMenu = preload("res://CommandMenu.gd")
var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var menu := CommandMenu.new()
	root.add_child(menu)
	await process_frame
	var text := menu._format_density_result({"sectors": [21.0, 21.0, 213.0, 100.0, 808, 0, 1793, null]})
	_check(text.contains("Sector 21 · Density: 21"), "integral float sector ID or density was not formatted as an integer")
	_check(text.contains("Sector 213 · Density: 100"), "density output lost sector 213 or its measurement label")
	_check(text.contains("Sector 808 · Density: 0"), "density output lost zero density")
	_check(text.contains("Sector 1793 · Density: unavailable"), "missing density was fabricated or hidden")
	_check(not text.contains("21.0"), "integral sector identifier retained a decimal suffix")
	menu.queue_free()
	if failures.is_empty():
		print("Godot density result tests: 4 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
