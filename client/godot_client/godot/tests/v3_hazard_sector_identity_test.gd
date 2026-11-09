extends SceneTree

const Protocol = preload("res://Protocol.gd")

func _initialize() -> void:
	var report := Protocol.move_hazard_report({
		"status": "error",
		"error": {"data": {
			"sector_id": 44,
			"ship_destroyed": true,
			"hazards": [{"hazard_type": "minefield", "hull_lost": 100}],
		}},
	})
	assert(report == "Sector hazard · minefield · hull lost: 100\nMovement outcome · reported sector: 44\nMovement outcome · ship destroyed: true")
	quit(0)
