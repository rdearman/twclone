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
	workflow.set_planet_info({"planet_id": 77, "sector_id": 17, "name": "Confirmed World", "citadel_level": 1, "goods": [{"commodity": "ORE", "quantity": 10, "max_capacity": 100}]})
	workflow.set_colonist_state({"planet_id": 77, "planet_colonists": 9, "ship_colonists": 2, "ship_holds_available": 4})
	_check(not workflow._rename_button.disabled, "confirmed planet did not enable its identity-bound rename action")
	_check(not workflow._labor_save.disabled, "confirmed planet did not enable its identity-bound workforce action")
	workflow.set_planet_info({"planet_id": 88, "name": "Wrong World", "citadel_level": 6})
	_check(not workflow._info_label.text.contains("Wrong World"), "mismatched planet.info response replaced the confirmed surface")
	_check(workflow._citadel_button.disabled, "mismatched planet.info response left development controls enabled")
	_check(workflow._rename_button.disabled, "mismatched planet.info response left rename enabled")
	_check(workflow._labor_save.disabled, "mismatched planet.info response left workforce assignment enabled")
	_check(workflow._stock_deposit.disabled and workflow._stock_withdraw.disabled, "mismatched planet.info response left stock transfers enabled")
	_check(workflow._fighter_to_planet.disabled and workflow._fighter_to_ship.disabled, "mismatched planet.info response left fighter transfers enabled")
	workflow.set_colonist_state({"planet_id": 88, "planet_colonists": 9, "ship_colonists": 2, "ship_holds_available": 4})
	_check(workflow._pickup_limit == 0 and workflow._dropoff_limit == 0, "mismatched colonist response enabled transfer state")
	_check(workflow._colonist_label.text.contains("did not identify the confirmed planet"), "mismatched colonist response was not explained")
	workflow.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot V3 planet surface identity tests passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
