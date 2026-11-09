extends SceneTree

const PlanetWorkflow = preload("res://PlanetWorkflow.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var workflow = PlanetWorkflow.new()
	root.add_child(workflow)
	await process_frame
	workflow.open_planet(77)
	workflow.set_planet_info({
		"planet_id": 77,
		"name": "Incomplete surface",
		"citadel_level": 6,
	})
	_check(not workflow._surface_identity_confirmed, "planet.info without sector ID authorized the surface")
	_check(workflow._citadel_button.disabled, "planet.info without sector ID enabled development actions")

	for malformed_sector_id in [true, 17.0, "17", 0, -1]:
		workflow.set_planet_info({
			"planet_id": 77,
			"sector_id": malformed_sector_id,
			"name": "Untrusted surface",
			"citadel_level": 6,
		})
		_check(workflow._sector_id == 0, "malformed sector ID %s was retained" % str(malformed_sector_id))
		_check(workflow._info_label.text.contains("invalid sector ID"), "malformed sector ID %s was displayed as authoritative" % str(malformed_sector_id))

	workflow.set_planet_info({"planet_id": 77, "sector_id": 17, "name": "Confirmed surface"})
	_check(workflow._sector_id == 17, "valid server sector ID was rejected")
	_check(workflow._info_label.text.contains("Confirmed surface"), "valid planet details were not displayed")

	workflow.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 planet landing sector identity tests passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
