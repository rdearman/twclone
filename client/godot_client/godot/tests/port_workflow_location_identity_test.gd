extends SceneTree

class HarnessGameplayView:
	extends "res://GameplayView.gd"
	var notice := ""

	func _ready() -> void:
		# The test exercises enter_port_workflow's location gate, not the visual
		# tree; skip construction of the full gameplay interface.
		pass

	func show_notification(message: String) -> void:
		notice = message

class FakePortWorkflow extends RefCounted:
	var visible := false
	var shown_port: Dictionary = {}
	var shown_sector_id := 0

	func set_player_hold(_ship: Dictionary, _credits: Variant = null) -> void:
		pass

	func show_port(data: Dictionary, sector_id: Variant) -> void:
		shown_port = data
		shown_sector_id = int(sector_id)
		visible = true

class FakeSectorView extends RefCounted:
	var interaction_enabled := true

	func set_interaction_enabled(enabled: bool) -> void:
		interaction_enabled = enabled

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var view = HarnessGameplayView.new()
	view._snapshot = {"ship": {}, "hud": {"credits": 50}}
	var port_workflow := FakePortWorkflow.new()
	var sector_view := FakeSectorView.new()
	view.port_workflow = port_workflow
	view.sector_view = sector_view
	var port := {"port": {"id": 7, "name": "Helix", "commodities": []}}
	for malformed_sector in [true, 17.0, "17", 0, -17]:
		view._snapshot["hud"]["sector_id"] = malformed_sector
		view.enter_port_workflow(port)
		_check(not port_workflow.visible, "malformed sector identity opened an actionable trade surface: %s" % str(malformed_sector))
	view._snapshot["hud"]["sector_id"] = 17
	view.enter_port_workflow(port)
	_check(port_workflow.visible, "valid authoritative sector identity did not open the port workflow")
	_check(port_workflow.shown_sector_id == 17, "port workflow did not receive the confirmed sector ID")
	_check(not sector_view.interaction_enabled, "opening the port did not disable sector interaction")
	view.free()
	if failures.is_empty():
		print("Godot port workflow location identity tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
