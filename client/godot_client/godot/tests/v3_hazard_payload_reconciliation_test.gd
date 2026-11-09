extends SceneTree

const Protocol = preload("res://Protocol.gd")

func _initialize() -> void:
	var report := Protocol.move_hazard_report({
		"status": "error",
		"data": {"hazards": []},
		"error": {"data": {
			"hazards": [{"hazard_type": "minefield", "hull_lost": 12}],
			"ship_destroyed": true,
		}},
	})
	assert(report.contains("minefield") and report.contains("hull lost: 12"))
	assert(report.contains("Movement outcome · ship destroyed: true"))

	var location := Protocol.move_hazard_report({
		"status": "refused",
		"error": {"data": {
			"ship_remains_in_sector": false,
			"hazards": [{"hazard_type": "atmosphere_quasar", "hull_lost": 100}],
		}},
	})
	assert(location.contains("atmosphere_quasar") and location.contains("hull lost: 100"))
	assert(location.contains("Movement outcome · ship remains in sector: false"))

	var merged := Protocol.move_hazard_report({
		"data": {"ship_destroyed": false},
		"error": {"data": {"ship_remains_in_sector": false}},
	})
	assert(merged.contains("ship destroyed: false"))
	assert(merged.contains("ship remains in sector: false"))

	# An empty error payload must not invent a terminal outcome over a real one.
	var explicit := Protocol.move_hazard_report({
		"data": {"hazards": [], "ship_destroyed": false},
		"error": {"data": {"ship_destroyed": true}},
	})
	assert(explicit == "Movement outcome · ship destroyed: false")
	quit(0)
