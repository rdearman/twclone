extends SceneTree

const Protocol = preload("res://Protocol.gd")

func _initialize() -> void:
	var split := Protocol.move_terminal_outcome({
		"data": {"ship_destroyed": false},
		"error": {"data": {"ship_remains_in_sector": false}},
	})
	assert(split == {"ship_destroyed": false, "ship_remains_in_sector": false})

	# A malformed success-shaped value must not become a terminal decision.
	var strict := Protocol.move_terminal_outcome({
		"data": {"ship_destroyed": 1, "ship_remains_in_sector": "false"},
		"error": {"data": {"ship_destroyed": true}},
	})
	assert(strict == {"ship_destroyed": true})

	# Explicit data wins for the same field, matching the display contract.
	var precedence := Protocol.move_terminal_outcome({
		"data": {"ship_destroyed": false},
		"error": {"data": {"ship_destroyed": true}},
	})
	assert(precedence == {"ship_destroyed": false})
	quit(0)
