extends SceneTree

const Protocol = preload("res://Protocol.gd")

func _initialize() -> void:
	var destroyed := Protocol.move_terminal_outcome({
		"data": {"hazards": [{"hazard_type": "minefield", "hull_lost": 100, "destroyed": true}]}
	})
	assert(destroyed.get("ship_destroyed", false) == true)

	var displaced := Protocol.move_terminal_outcome({
		"error": {"data": {"hazards": [{"hazard_type": "limpet", "damage": 4, "ship_remains_in_sector": false}]}}
	})
	assert(displaced.get("ship_remains_in_sector", true) == false)

	var damage_only := Protocol.move_terminal_outcome({
		"data": {"hazards": [{"hazard_type": "nebula", "hull_lost": 4}]}
	})
	assert(damage_only.is_empty())
	quit(0)
