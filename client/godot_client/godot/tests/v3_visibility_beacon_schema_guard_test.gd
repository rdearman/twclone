extends SceneTree

const CommandMenuScript = preload("res://CommandMenu.gd")

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	var menu = CommandMenuScript.new()
	var rendered := menu._format_scan_result({
		"status": "ok",
		"data": {
			"sector_id": 12,
			"beacon": {"secret": "schema-mismatched payload"},
		},
	})
	_check(rendered.contains("Beacon: unavailable"), "malformed beacon was not rendered as unavailable")
	_check(not rendered.contains("schema-mismatched payload"), "malformed beacon payload was stringified into scan output")
	menu.free()
	if failures.is_empty():
		print("Godot V3 visibility beacon schema guard test: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
