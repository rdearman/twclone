extends SceneTree

const PlanetWorkflow = preload("res://PlanetWorkflow.gd")

var failures: Array[String] = []
var captured: Array[Dictionary] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var workflow = PlanetWorkflow.new()
	root.add_child(workflow)
	await process_frame
	workflow.command_requested.connect(func(command: String, data: Dictionary, label: String, mutating: bool) -> void:
		captured.append({"command": command, "data": data, "label": label, "mutating": mutating})
	)
	workflow.open_planet(77)
	_check(captured.size() == 2, "opening the landed-planet view did not request authoritative details")
	_check(captured[0]["command"] == "planet.info" and captured[1]["command"] == "planet.colonists.get", "planet screen requested unexpected operations")
	workflow.set_colonist_state({"planet_colonists": 8, "ship_colonists": 3, "ship_holds_available": 4})
	_check(not workflow._pickup.disabled and not workflow._dropoff.disabled, "valid pickup/dropoff actions remained disabled")
	workflow.set_planet_info({
		"name": "Test World", "population": 40, "fighters": 12, "citadel_level": 1,
		"colonists_unassigned": 10, "colonists_ore": 8, "colonists_org": 6,
		"colonists_eq": 4, "colonists_weapons": 12,
		"max_colonists_ore": 20, "max_colonists_organics": 20,
		"max_colonists_equipment": 20,
		"goods": [{"commodity": "ORE", "quantity": 5, "max_capacity": 50}],
	})
	_check(workflow._info_label.text.to_lower().contains("fighters: 12"), "confirmed planetary fighter count was not displayed")
	_check(int(workflow._labor_weapons.value) == 12, "confirmed weapons workforce was not loaded")
	_check(workflow._labor_status.text.contains("weapons 12"), "weapons assignment was not shown in workforce status")
	workflow._labor_weapons.value = 11
	workflow._save_labor_assignments()
	_check(captured.size() == 3 and captured[2]["command"] == "planet.colonists.allocate", "workforce save did not use the existing allocation command")
	if captured.size() > 2:
		_check(captured[2]["data"].get("weapons") == 11, "weapons worker target was omitted from allocation payload")
	workflow._quantity.value = 4
	workflow._confirm_transfer("pickup")
	var transfer_dialog: ConfirmationDialog = workflow.get_child(workflow.get_child_count() - 1)
	transfer_dialog.confirmed.emit()
	_check(captured.size() == 4 and captured[3]["command"] == "planet.colonists.set", "colonist transfer did not submit after confirmation")
	if captured.size() > 3:
		_check(captured[3]["data"] == {"planet_id": 77, "action": "pickup", "quantity": 4} and captured[3]["mutating"], "colonist transfer payload did not match the implemented handler")
	workflow._launch_now()
	_check(captured.size() == 5 and captured[4]["command"] == "planet.launch" and captured[4]["mutating"], "planet launch action did not use server-confirmed command flow")
	workflow.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot planet workflow tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
