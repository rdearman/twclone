extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame

	app.gameplay_view._build_contents({
		"planets": [
			{"id": "77", "planet_id": 88, "name": "Authoritative alias"},
			{"id": 77, "planet_id": 88, "name": "Conflicting legacy alias"},
			{"id": true, "planet_id": 0, "name": "Malformed planet"},
		],
	})

	_check(app.gameplay_view._rows.has("planet:88"), "valid authoritative planet alias did not create a selectable row")
	_check(not app.gameplay_view._rows.has("planet:77"), "malformed string ID became a selectable row key")
	_check(app.gameplay_view._rows.size() == 1, "legacy or malformed planet metadata created an actionable row")
	app.gameplay_view.sector_view.compose({
		"planets": [{"id": 77, "planet_id": 88, "name": "Conflicting legacy alias"}],
	}, "")
	_check(app.gameplay_view.sector_view.current_objects[0]["key"] == "planet:88", "sector artwork keyed the planet by a legacy ID")
	var found_display_only := false
	for child in app.gameplay_view.contents_list.get_children():
		if child is HBoxContainer:
			for nested in child.get_children():
				if nested is Label and str(nested.text).contains("Malformed planet"):
					found_display_only = true
	_check(found_display_only, "malformed planet metadata was hidden instead of shown as unavailable")

	var filtered: Dictionary = app.gameplay_view._sector_for_display({
		"ships": [
			{"id": 11, "name": "Player ship"},
			{"id": 12, "name": "Other ship"},
		],
		"ships_present": [
			{"ship_id": 11, "name": "Player ship"},
			{"ship_id": 12, "name": "Other ship"},
		],
	}, {"ship_id": 11})
	_check(filtered["ships"].size() == 1 and filtered["ships"][0]["id"] == 12, "active player ship remained in viewport ships")
	_check(filtered["ships_present"].size() == 1 and filtered["ships_present"][0]["ship_id"] == 12, "active player ship remained in visible ship metadata")
	var unconfirmed: Dictionary = app.gameplay_view._sector_for_display({"ships": [{"id": 11}]}, {})
	_check(unconfirmed["ships"].size() == 1, "unconfirmed active ship ID incorrectly hid a sector ship")

	if failures.is_empty():
		print("Godot V3 visibility entity identity tests: %d checks passed" % checks)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)
	quit(0)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
