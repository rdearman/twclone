extends SceneTree

const ClientState = preload("res://ClientState.gd")
const CommandMenu = preload("res://CommandMenu.gd")
const GameplayView = preload("res://GameplayView.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var sector: Dictionary = ClientState.normalize_sector({
		"sector_id": 12,
		"ports": [{"id": 8, "name": "Quiet Port", "type": 9, "class": 9}],
		"celestial_objects": [],
		"ships_present": [],
	})
	_check(sector["ports"].size() == 1, "known port presence was lost during normalization")
	if not sector["ports"].is_empty():
		_check(not sector["ports"][0].has("type") and not sector["ports"][0].has("class"), "sector data exposed port class/type")
	_check(sector["knowledge"]["ports"] and sector["knowledge"]["planets"] and sector["knowledge"]["ships"], "reported empty collections were marked unknown")
	var undisclosed: Dictionary = ClientState.normalize_sector({"sector_id": 13})
	_check(not undisclosed["knowledge"]["ports"] and not undisclosed["knowledge"]["adjacent"], "missing sector fields were treated as known-empty")

	var menu = CommandMenu.new()
	root.add_child(menu)
	await process_frame
	var scan := menu._format_scan_result({"data": {
		"sector_id": 12, "ports": [{"id": 8, "type": 9}], "counts": {"ships": 0},
	}})
	_check(scan.contains("Port: present") and scan.contains("class/details not disclosed"), "port scan did not show presence while withholding class")
	_check(scan.contains("Ships: 0") and scan.contains("Planets: unknown"), "scan counts conflated missing values with zero")
	_check(not scan.contains("Type 9"), "scan renderer inferred a port class")
	var empty_scan := menu._format_scan_result({"data": {"sector_id": 12, "ports": []}})
	_check(empty_scan.contains("Port: none") and empty_scan.contains("Ships: unknown"), "known-empty port or missing count was rendered incorrectly")
	var missing_scan := menu._format_scan_result({"data": {"sector_id": 12}})
	_check(missing_scan.contains("Port: not reported") and missing_scan.contains("Adjacent: not reported"), "missing scan fields were rendered as known-empty")

	var view = GameplayView.new()
	view.size = Vector2(1200, 800)
	root.add_child(view)
	await process_frame
	view.set_snapshot({
		"authenticated": true, "disconnected": false,
		"hud": {"sector_id": 12}, "freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": sector,
	})
	view.select_object_key("port:8")
	_check(not view.selection_detail.text.contains("Type 9") and not view.selection_detail.text.contains("class 9"), "port selection revealed class/type information")
	var unknown_sector: Dictionary = ClientState.normalize_sector({"sector_id": 13})
	view.set_snapshot({
		"authenticated": true, "disconnected": false,
		"hud": {"sector_id": 13}, "freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": unknown_sector,
	})
	_check(_find_text(view.contents_list, "Not reported by the server"), "undisclosed sector fields were presented as an empty sector")
	menu.queue_free()
	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot sector visibility tests: 11 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _find_text(node: Node, text: String) -> bool:
	if node is Label and (node as Label).text == text:
		return true
	for child in node.get_children():
		if _find_text(child, text):
			return true
	return false

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
