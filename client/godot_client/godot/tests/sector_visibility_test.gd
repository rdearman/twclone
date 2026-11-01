extends SceneTree

const ClientState = preload("res://ClientState.gd")
const CommandMenu = preload("res://CommandMenu.gd")
const GameplayView = preload("res://GameplayView.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var state := ClientState.new()
	state.mark_authenticated()
	_check(state.begin_refresh(1, ["sector"]), "sector refresh did not start")
	_check(state.accept_sector({"sector_id": 12, "name": "Confirmed Sector", "ports": [{"id": 8}]}, 1), "authoritative sector response was not accepted")
	var confirmed_sector := state.sector.duplicate(true)
	_check(not state.apply_response("sector.info", {
		"status": "error",
		"error": {"message": "sector disclosure refused"},
		"data": {"sector_id": 99, "name": "Untrusted Sector", "ports": [{"id": 777}]},
	}), "refused sector response was treated as authoritative")
	_check(state.sector == confirmed_sector, "refused sector response replaced confirmed visibility state")
	_check(state.sector.get("id", 0) == 12 and not state.sector.get("ports", []).is_empty(), "confirmed sector identity was not retained after refusal")
	_check(state.apply_response("sector.info", {
		"status": "ok", "type": "sector.info",
		"data": {"sector_id": 13, "name": "Next Sector", "ports": []},
	}), "valid authoritative sector response was not accepted")
	_check(state.sector.get("id", 0) == 13 and state.sector.get("name", "") == "Next Sector", "valid sector refresh did not replace stale confirmed state")

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
	var malformed_normalized_port: Dictionary = ClientState.normalize_sector({
		"sector_id": 12, "port": {"present": "false", "id": 8, "name": "Untrusted Port"},
	})
	_check(malformed_normalized_port["ports"].is_empty() and malformed_normalized_port["knowledge"]["ports"], "schema-mismatched port presence became actionable normalized state")
	var undisclosed: Dictionary = ClientState.normalize_sector({"sector_id": 13})
	_check(not undisclosed["knowledge"]["ports"] and not undisclosed["knowledge"]["adjacent"], "missing sector fields were treated as known-empty")
	var malformed_name: Dictionary = ClientState.normalize_sector({"sector_id": 14, "name": {"remote": "untrusted"}})
	_check(malformed_name.get("id", 0) == 14 and malformed_name.get("name", "") == "Sector 14", "malformed sector name did not preserve the authoritative ID-derived display label")

	var menu = CommandMenu.new()
	root.add_child(menu)
	await process_frame
	var scan := menu._format_scan_result({"data": {
		"sector_id": 12, "ports": [{"id": 8, "type": 9}], "counts": {"ships": 0},
	}})
	_check(scan.contains("Port: present") and scan.contains("class/details not disclosed"), "port scan did not show presence while withholding class")
	_check(scan.contains("Ships: 0") and scan.contains("Planets: unknown"), "scan counts conflated missing values with zero")
	_check(not scan.contains("Type 9"), "scan renderer inferred a port class")
	var delayed_scan := menu._format_scan_result({"data": {
		"sector_id": 99, "name": "Untrusted remote sector", "counts": {"ships": 77},
	}})
	_check(delayed_scan.contains("does not match the confirmed location") and not delayed_scan.contains("Untrusted remote sector"), "scan presenter disclosed a sector different from the confirmed HUD location")
	var empty_scan := menu._format_scan_result({"data": {"sector_id": 12, "ports": []}})
	_check(empty_scan.contains("Port: none") and empty_scan.contains("Ships: unknown"), "known-empty port or missing count was rendered incorrectly")
	var delayed_density := menu._format_density_result({"data": {"sector_id": 99, "density": 88}})
	_check(not delayed_density.contains("Sector 99") and delayed_density.contains("No density values were returned"), "density presenter disclosed a single-sector result for a different confirmed location")
	var missing_scan := menu._format_scan_result({"data": {"sector_id": 12}})
	_check(missing_scan.contains("Port: not reported") and missing_scan.contains("Adjacent: not reported"), "missing scan fields were rendered as known-empty")
	var malformed_port_scan := menu._format_scan_result({"data": {"sector_id": 12, "port": {"present": "false"}}})
	_check(malformed_port_scan.contains("Port: not reported") and not malformed_port_scan.contains("Port: present"), "schema-mismatched port presence was coerced into authoritative visibility")
	var malformed_port_rows := menu._format_scan_result({"data": {"sector_id": 12, "ports": ["untrusted row"]}})
	_check(malformed_port_rows.contains("Port: not reported") and not malformed_port_rows.contains("Port: present"), "malformed port rows were coerced into authoritative visibility")
	var missing_id_scan := menu._format_scan_result({"data": {"name": "Untrusted sector", "port": {"present": true}}})
	_check(missing_id_scan.contains("authoritative sector ID unavailable") and not missing_id_scan.contains("Untrusted sector"), "scan without an authoritative sector ID was rendered")
	var malformed_name_scan := menu._format_scan_result({"data": {"sector_id": 12, "name": {"remote": "untrusted"}}})
	_check(malformed_name_scan.contains("Sector 12 · Unknown") and not malformed_name_scan.contains("untrusted"), "schema-mismatched scan name was stringified as authoritative visibility")
	var malformed_scan := menu._format_scan_result({"data": {
		"sector_id": 12, "adjacent_sectors": [13.0, true, {"to_sector": "secret"}, {"to_sector": 14}],
		"counts": {"ships": 4.0, "planets": true},
	}})
	_check(malformed_scan.contains("Adjacent: 14") and not malformed_scan.contains("13") and not malformed_scan.contains("secret"), "malformed adjacent IDs were rendered as authoritative")
	_check(malformed_scan.contains("Ships: unknown") and malformed_scan.contains("Planets: unknown"), "schema-mismatched scan counts were coerced into authoritative values")
	var refused_scan := menu._format_scan_result({
		"status": "refused",
		"error": {"message": "Detailed scan requires the current sector."},
		"data": {"sector_id": 999, "name": "Untrusted remote sector", "counts": {"ships": 77}},
	})
	_check(refused_scan.contains("Detailed scan requires the current sector"), "scan refusal reason was not presented")
	_check(not refused_scan.contains("Untrusted remote sector") and not refused_scan.contains("77"), "scan refusal rendered untrusted payload data")
	var refused_density := menu._format_density_result({
		"status": "refused",
		"error": {"message": "Density is unavailable for a remote sector."},
		"data": {"sectors": [{"sector_id": 999, "density": 88}]},
	})
	_check(refused_density.contains("Density is unavailable for a remote sector"), "density refusal reason was not presented")
	_check(not refused_density.contains("999") and not refused_density.contains("88"), "density refusal rendered untrusted payload data")

	var main = load("res://Main.gd").new()
	_check(main._density_has_authoritative_ids({"sectors": [{"sector_id": 12, "density": 3}]}), "density row with a server sector ID was rejected")
	_check(main._density_has_authoritative_ids({"sectors": [12, 3, 13, 0]}), "compact density rows with server IDs were rejected")
	_check(not main._density_has_authoritative_ids({"sectors": [{"density": 3}]}), "density row without a server sector ID was accepted")
	_check(not main._density_has_authoritative_ids({"sectors": [3, 12, 0]}), "malformed compact density payload was accepted")
	var safe_density := menu._format_density_result({"data": {
		"sectors": [{"sector_id": true, "density": 9}, {"sector_id": 14, "density": "secret"}],
	}})
	_check(not safe_density.contains("secret") and not safe_density.contains("true"), "density presenter rendered malformed authoritative fields")
	_check(safe_density.contains("No density values were returned by the server."), "density presenter did not refuse malformed rows")
	var mixed_density := menu._format_density_result({"data": {
		"sectors": [{"sector_id": 12, "density": 3}, {"sector_id": 13, "density": "secret"}],
	}})
	_check(not mixed_density.contains("Sector 12"), "density presenter rendered a valid row from a mixed malformed response")
	_check(mixed_density.contains("No density values were returned by the server."), "mixed malformed density rows were not rejected")
	var mixed_compact_density := menu._format_density_result({"data": {
		"sectors": [12, 3, 13, "secret"],
	}})
	_check(not mixed_compact_density.contains("Sector 12"), "compact density presenter rendered a valid pair from a mixed malformed response")
	_check(mixed_compact_density.contains("No density values were returned by the server."), "mixed malformed compact density pairs were not rejected")
	var normalized_ids := ClientState.normalize_sector({
		"sector_id": 12,
		"ports": [{"id": 8.0, "name": "Untrusted port"}],
		"planets": [{"planet_id": 77.0, "name": "Untrusted planet"}],
		"ships": [{"ship_id": 19.0, "name": "Untrusted ship"}],
		"adjacent_sectors": [{"to_sector": 13.0}],
	})
	_check(normalized_ids.get("ports", [])[0].get("id", 0) == 0, "float port ID was coerced into an actionable ID")
	_check(normalized_ids.get("planets", [])[0].get("planet_id", 0) == 0, "float planet ID was coerced into an actionable ID")
	_check(normalized_ids.get("ships", [])[0].get("ship_id", 0) == 0, "float ship ID was coerced into an actionable ID")
	_check(normalized_ids.get("adjacent_sector_ids", []).is_empty(), "float adjacent sector ID was treated as authoritative")

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
	_check(view._authoritative_object_id({"planet_id": null, "id": 19}, ["planet_id", "id"]) == 19, "planet view did not fall back to a valid server planet ID")
	_check(view._authoritative_object_id({"planet_id": 0, "id": 20}, ["planet_id", "id"]) == 20, "planet view did not reject a non-positive primary ID")
	_check(view._authoritative_object_id({"planet_id": null, "id": 0}, ["planet_id", "id"]) == 0, "planet view invented an ID when the server supplied none")
	_check(view._authoritative_object_id({"planet_id": "77", "id": 20}, ["planet_id", "id"]) == 0, "planet view fell back after an explicit malformed ID")
	menu.queue_free()
	main.queue_free()
	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot sector visibility tests: 21 passed")
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
