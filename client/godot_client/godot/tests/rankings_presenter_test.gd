extends SceneTree

const CommandMenu = preload("res://CommandMenu.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var menu := CommandMenu.new()
	_check(menu.format_result("Player rankings", {"rankings": [
		{"player_id": 7, "name": "Nova", "experience": 42, "alignment": 10},
		{"player_id": 8, "name": "Rook", "experience": 18, "alignment": -3},
	]}) == "1. Nova · XP 42 · alignment 10\n2. Rook · XP 18 · alignment -3", "server ranking order and values were not presented")
	_check(menu.format_result("Player rankings", {"rankings": []}) == "No player rankings are available yet.", "empty authoritative rankings were not reported as empty")
	_check(menu.format_result("Player rankings", {"rankings": [null, "spoofed", {"player_id": 0, "name": "Fake", "experience": 99, "alignment": 1}]}) == "No readable player rankings were returned.", "malformed ranking rows were presented as authoritative")
	_check(menu.format_result("Player rankings", {}) == "Player rankings are unavailable: the server returned no readable rankings.", "missing ranking collection was presented as success")
	menu.free()
	if failures.is_empty():
		print("Godot rankings presenter tests: 4 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
