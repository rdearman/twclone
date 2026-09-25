extends SceneTree

const PlanetWorkflow = preload("res://PlanetWorkflow.gd")

var failures: Array[String] = []
var captured: Array[Dictionary] = []

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
	workflow._quantity.value = 4
	workflow._confirm_transfer("pickup")
	var transfer_dialog: ConfirmationDialog = workflow.get_child(workflow.get_child_count() - 1)
	transfer_dialog.confirmed.emit()
	_check(captured.size() == 3 and captured[2]["command"] == "planet.colonists.set", "colonist transfer did not submit after confirmation")
	if captured.size() > 2:
		_check(captured[2]["data"] == {"planet_id": 77, "action": "pickup", "quantity": 4} and captured[2]["mutating"], "colonist transfer payload did not match the implemented handler")
	workflow._launch_dialog.confirmed.emit()
	_check(captured.size() == 4 and captured[3]["command"] == "planet.launch" and captured[3]["mutating"], "planet launch action did not use server-confirmed command flow")
	workflow.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot planet workflow tests: 6 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
