extends SceneTree

const MainScene = preload("res://Main.tscn")
const ClientState = preload("res://ClientState.gd")

class FakeRefresh extends RefCounted:
	var calls: Array = []

	func accepts_request(_request_id: String) -> bool:
		return false

	func refresh(_force: bool = false, _domains: Array = []) -> bool:
		calls.append({"force": _force, "domains": _domains})
		return true

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	app.auth_session.handle_response({
		"status": "ok", "type": "auth.session",
		"data": {"session": "test-session", "player_id": 3},
	})
	app.refresh_coordinator = FakeRefresh.new()
	var known_sector := ClientState.normalize_sector({
		"sector_id": 12, "name": "Known sector",
		"ports": [{"id": 8, "name": "Known port"}],
		"celestial_objects": [], "ships_present": [],
	})
	app.gameplay_view.set_snapshot({
		"authenticated": true, "disconnected": false,
		"hud": {"sector_id": 12},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": known_sector,
	})
	var refused_scan := {
		"status": "refused",
		"error": {"message": "Detailed scan requires the current sector."},
		"data": {"sector_id": 999, "name": "Untrusted remote sector"},
	}
	await app._finish_command_reply(refused_scan, {
		"label": "Detailed current-sector scan",
		"command": "sector.scan",
		"mutating": false,
	})
	_check(app.gameplay_view._snapshot["sector"]["sector_id"] == 12, "refused scan replaced the confirmed sector with response payload data")
	_check(not str(app.gameplay_view.notification_label.text).contains("Untrusted remote sector"), "refused scan displayed untrusted response data")
	_check(app.gameplay_view.notification_label.text.contains("Detailed scan requires the current sector"), "scan refusal reason was not shown")
	_check(app.refresh_coordinator.calls.size() == 1, "refused scan did not request an authoritative sector refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"], "refused scan refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	app.gameplay_view.show_information("Current sector", "Confirmed visibility")
	await app._finish_command_reply({
		"status": "ok", "type": "sector.scan",
		"data": {"sector_id": "12", "id": 12, "name": "Malformed explicit identity"},
	}, {
		"label": "Detailed current-sector scan", "command": "sector.scan", "mutating": false,
	})
	_check(not app.gameplay_view.information_text.text.contains("Malformed explicit identity"), "scan fell back to a legacy ID after malformed explicit sector_id")
	_check(app.gameplay_view.notification_label.text.contains("omitted a valid sector ID"), "malformed explicit sector_id was not refused")
	_check(app.refresh_coordinator.calls.size() == 1, "malformed explicit sector_id did not request an authoritative refresh")


	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "refused",
		"error": {"message": "Sector details are unavailable from this location."},
		"data": {"sector_id": 999, "name": "Untrusted remote sector"},
	}, {
		"label": "Describe sector", "command": "move.describe_sector", "mutating": false,
	})
	_check(app.gameplay_view._snapshot["sector"]["sector_id"] == 12, "describe-sector refusal replaced the confirmed sector")
	_check(not str(app.gameplay_view.notification_label.text).contains("Untrusted remote sector"), "describe-sector refusal displayed untrusted response data")
	_check(app.gameplay_view.notification_label.text.contains("Sector details are unavailable from this location"), "describe-sector refusal reason was not shown")
	_check(app.refresh_coordinator.calls.size() == 1, "describe-sector refusal did not request an authoritative sector refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"], "describe-sector refusal refreshed the wrong state domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "sector.scan",
		"data": {"sector_id": 999, "name": "Contradictory remote sector", "counts": {"ships": 4}},
	}, {
		"label": "Detailed current-sector scan", "command": "sector.scan", "mutating": false,
	})
	_check(app.gameplay_view._snapshot["sector"]["sector_id"] == 12, "scan for another sector replaced the confirmed sector")
	_check(not str(app.gameplay_view.notification_label.text).contains("Contradictory remote sector"), "contradictory scan payload was rendered")
	_check(app.gameplay_view.notification_label.text.contains("confirmed location is sector 12"), "contradictory scan was not explained")
	_check(app.refresh_coordinator.calls.size() == 1, "contradictory scan did not request a current-sector refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"], "contradictory scan refreshed unrelated state domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "sector.scan",
		"data": {"name": "Unidentified remote sector", "counts": {"ships": 9}},
	}, {
		"label": "Detailed current-sector scan", "command": "sector.scan", "mutating": false,
	})
	_check(not str(app.gameplay_view.notification_label.text).contains("Unidentified remote sector"), "scan without an authoritative ID was rendered")
	_check(app.gameplay_view.notification_label.text.contains("omitted a valid sector ID"), "missing scan ID was not explained")
	_check(app.refresh_coordinator.calls.size() == 1, "missing scan ID did not request an authoritative sector refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"], "missing scan ID refreshed unrelated state domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "sector.info",
		"data": {"sector_id": 12, "name": "Wrong operation type", "counts": {"ships": 9}},
	}, {
		"label": "Detailed current-sector scan", "command": "sector.scan", "mutating": false,
	})
	_check(not app.gameplay_view.information_text.text.contains("Wrong operation type"), "scan with a sector.info response type was rendered")
	_check(app.gameplay_view.notification_label.text.contains("response type did not match"), "mismatched scan response type was not explained")
	_check(app.refresh_coordinator.calls.size() == 1 and app.refresh_coordinator.calls[0]["domains"] == ["sector"], "mismatched scan response type did not refresh the confirmed sector")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": null,
		"data": {"sector_id": 12, "name": "Untrusted untyped sector", "counts": {"ships": 1}},
	}, {
		"label": "Detailed current-sector scan", "command": "sector.scan", "mutating": false,
	})
	_check(not app.gameplay_view.information_text.text.contains("Untrusted untyped sector"), "explicit null scan type was rendered")
	_check(app.gameplay_view.information_text.text.contains("Confirmed visibility"), "untyped scan replaced confirmed visibility")
	_check(app.gameplay_view.notification_label.text.contains("response type did not match"), "explicit null scan type was not refused")
	_check(app.refresh_coordinator.calls.size() == 1 and app.refresh_coordinator.calls[0]["domains"] == ["sector"], "explicit null scan type did not refresh the confirmed sector")

	app.refresh_coordinator.calls.clear()
	app.gameplay_view.set_snapshot({
		"authenticated": true, "disconnected": false,
		"hud": {"sector_id": "12"},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": known_sector,
	})
	await app._finish_command_reply({
		"status": "ok", "type": "sector.scan",
		"data": {"sector_id": 12, "name": "Cached float identity", "counts": {"ships": 1}},
	}, {
		"label": "Detailed current-sector scan", "command": "sector.scan", "mutating": false,
	})
	_check(not app.gameplay_view.information_text.text.contains("Cached float identity"), "scan matched a malformed floating-point cached sector ID")
	_check(app.gameplay_view.notification_label.text.contains("omitted a valid sector ID"), "malformed cached sector ID was not refused")
	_check(app.refresh_coordinator.calls.size() == 1 and app.refresh_coordinator.calls[0]["domains"] == ["sector"], "malformed cached sector ID did not refresh the confirmed sector")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "sector.scan",
		"data": {"sector_id": "12", "name": "Schema-mismatched sector", "counts": {"ships": 9}},
	}, {
		"label": "Detailed current-sector scan", "command": "sector.scan", "mutating": false,
	})
	_check(app.gameplay_view._snapshot["sector"]["sector_id"] == 12, "floating-point sector ID replaced the confirmed sector")
	_check(not str(app.gameplay_view.notification_label.text).contains("Schema-mismatched sector"), "floating-point sector ID was rendered as authoritative")
	_check(app.gameplay_view.notification_label.text.contains("omitted a valid sector ID"), "floating-point sector ID was not rejected as malformed")
	_check(app.refresh_coordinator.calls.size() == 1, "floating-point sector ID did not request an authoritative refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"], "floating-point sector ID refreshed unrelated state domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "sector.density.scan",
		"data": [21, 21, 213, 100, 808, 0],
	}, {
		"label": "Sector density scan", "command": "sector.scan.density", "mutating": false,
	})
	_check(app.gameplay_view.information_text.text.contains("Sector 21 · Density: 21"), "compact density rows were rejected by current-sector identity validation")
	_check(app.gameplay_view.information_text.text.contains("Sector 213 · Density: 100"), "compact density rows were not preserved for the density presenter")
	_check(app.refresh_coordinator.calls.is_empty(), "valid multi-sector density scan triggered an unnecessary location refresh")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "sector.density.scan",
		"data": {"sectors": []},
	}, {
		"label": "Sector density scan", "command": "sector.scan.density", "mutating": false,
	})
	_check(app.gameplay_view.information_text.text.contains("No density values were returned by the server."), "empty authoritative density result was not presented as a valid no-row result")
	_check(app.refresh_coordinator.calls.is_empty(), "empty authoritative density result triggered an unnecessary location refresh")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "sector.density.scan",
		"data": {"sectors": [{"sector_id": 21, "density": "not-a-density"}]},
	}, {
		"label": "Sector density scan", "command": "sector.scan.density", "mutating": false,
	})
	_check(not app.gameplay_view.information_text.text.contains("not-a-density"), "malformed density value was rendered as authoritative")
	_check(app.gameplay_view.notification_label.text.contains("no usable authoritative density rows"), "malformed density value was not refused")
	_check(app.refresh_coordinator.calls.size() == 1, "malformed density value did not request an authoritative refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"], "malformed density value refreshed unrelated state domains")

	app.refresh_coordinator.calls.clear()
	app._pending_game_commands["scan-timeout"] = {
		"kind": "command", "command": "sector.scan", "label": "Detailed current-sector scan",
		"mutating": false,
	}
	app._on_request_timed_out("scan-timeout", "sector.scan")
	_check(app.gameplay_view.notification_label.text.contains("refreshing confirmed sector state"), "scan timeout did not explain the authoritative refresh")
	_check(not app._pending_game_commands.has("scan-timeout"), "scan timeout remained pending")
	_check(app.refresh_coordinator.calls.size() == 1, "scan timeout did not request an authoritative sector refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"], "scan timeout refreshed unrelated state domains")

	app.refresh_coordinator.calls.clear()
	app.gameplay_view.enter_port_workflow({"port": {"id": 8, "name": "Known port"}})
	await app._finish_command_reply({
		"status": "refused",
		"error": {"message": "Port information is available only in its sector"},
	}, {
		"label": "Open port", "command": "port.info", "mutating": false,
	})
	_check(not app.gameplay_view.port_workflow.visible, "refused port.info left stale port workflow visible")
	_check(app.refresh_coordinator.calls.size() == 1, "refused port.info did not request an authoritative sector refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"], "refused port.info refreshed unrelated state domains")

	app.refresh_coordinator.calls.clear()
	app.gameplay_view.enter_port_workflow({"port": {"id": 8, "name": "Known port"}})
	app._pending_game_commands["port-refresh"] = {"kind": "port_refresh", "command": "port.info"}
	app._on_transport_reply("port-refresh", {
		"status": "refused",
		"error": {"message": "Port information is available only in its sector"},
	}, "port.info")
	_check(not app.gameplay_view.port_workflow.visible, "refused port refresh left stale port workflow visible")
	_check(app.refresh_coordinator.calls.size() == 1, "refused port refresh did not request an authoritative sector refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"], "refused port refresh refreshed unrelated state domains")

	app.refresh_coordinator.calls.clear()
	app.gameplay_view.set_snapshot({
		"authenticated": true, "disconnected": false,
		"hud": {"sector_id": 12},
		"freshness": {"sector": "fresh", "overall": "fresh"},
		"sector": known_sector,
	})
	await app._finish_command_reply({
		"status": "ok", "type": "trade.port_info",
		"data": {"port": {"id": 99, "name": "Untrusted port"}, "commodities": []},
	}, {
		"label": "Open port", "command": "port.info", "mutating": false,
	})
	_check(not app.gameplay_view.port_workflow.visible, "mismatched port.info opened a port workflow")
	_check(not str(app.gameplay_view.notification_label.text).contains("Untrusted port"), "mismatched port.info displayed untrusted port data")
	_check(app.gameplay_view.notification_label.text.contains("confirmed port is 8"), "mismatched port.info was not explained")
	_check(app.refresh_coordinator.calls.size() == 1, "mismatched port.info did not request a sector refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"], "mismatched port.info refreshed the wrong domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "sector.info",
		"data": {"port": {"id": 8, "name": "Wrong response type"}, "commodities": []},
	}, {
		"label": "Open port", "command": "port.info", "mutating": false,
	})
	_check(not app.gameplay_view.port_workflow.visible, "wrong port response type opened a port workflow")
	_check(not str(app.gameplay_view.notification_label.text).contains("Wrong response type"), "wrong port response type displayed untrusted data")
	_check(app.gameplay_view.notification_label.text.contains("unexpected port response type"), "wrong port response type was not explained")
	_check(app.refresh_coordinator.calls.size() == 1, "wrong port response type did not request a sector refresh")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": null,
		"data": {"port": {"id": 8, "name": "Known port", "commodities": []}},
	}, {
		"label": "Open port", "command": "port.info", "mutating": false,
	})
	_check(app.gameplay_view.port_workflow.visible, "explicit null port response type was rejected despite the optional envelope contract")
	_check(app.refresh_coordinator.calls.is_empty(), "valid null-type port.info triggered an unnecessary sector refresh")
	app.gameplay_view.port_workflow.close_for_location_change()

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "port.info",
		"data": {"port": {"name": "Unidentified port"}, "commodities": []},
	}, {
		"label": "Open port", "command": "port.info", "mutating": false,
	})
	_check(not app.gameplay_view.port_workflow.visible, "port.info without an ID opened a port workflow")
	_check(not str(app.gameplay_view.notification_label.text).contains("Unidentified port"), "port.info without an ID displayed untrusted port data")
	_check(app.gameplay_view.notification_label.text.contains("omitted a valid port ID"), "port.info without an ID was not explained")
	_check(app.refresh_coordinator.calls.size() == 1, "port.info without an ID did not request a sector refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"], "port.info without an ID refreshed the wrong domains")

	app.refresh_coordinator.calls.clear()
	await app._finish_command_reply({
		"status": "ok", "type": "ship.inspect.result",
		"data": {"ship": {"ship_id": 88.0, "name": "Schema-mismatched ship"}},
	}, {
		"label": "Inspect ship", "command": "ship.inspect", "mutating": false,
		"data": {"ship_id": 88},
	})
	_check(not str(app.gameplay_view.notification_label.text).contains("Schema-mismatched ship"),
		"floating-point ship ID was rendered as authoritative inspection data")
	_check(app.gameplay_view.notification_label.text.contains("omitted a valid ship ID"),
		"floating-point ship ID was not rejected as malformed")
	_check(app.refresh_coordinator.calls.size() == 1,
		"malformed ship inspection did not request an authoritative sector refresh")
	if app.refresh_coordinator.calls.size() == 1:
		_check(app.refresh_coordinator.calls[0]["domains"] == ["sector"],
			"malformed ship inspection refreshed unrelated state domains")

	app.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot visibility command response tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)
