extends SceneTree

const CommandMenu = preload("res://CommandMenu.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var menu := CommandMenu.new()
	root.add_child(menu)
	await process_frame

	var scan := menu._format_scan_result({
		"status": "ok",
		"data": {"sector_id": "12", "id": 12, "name": "Contradictory"},
	})
	_check(scan.contains("authoritative sector ID unavailable"),
		"scan presenter fell through from malformed explicit sector_id to legacy id")
	_check(not scan.contains("Contradictory"),
		"scan presenter rendered a payload with contradictory identity fields")

	var density := menu._format_density_result({
		"status": "ok",
		"data": {"sector_id": "12", "id": 12, "density": 4},
	})
	_check(density.contains("No density values were returned by the server."),
		"density presenter accepted a legacy id after malformed explicit sector_id")
	_check(not density.contains("Sector 12"),
		"density presenter rendered a contradictory identity payload")

	menu.queue_free()
	await process_frame
	if failures.is_empty():
		print("V3 visibility explicit-ID precedence tests: 4 checks passed")
		quit(0)
	for failure in failures:
		push_error(failure)
	quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
