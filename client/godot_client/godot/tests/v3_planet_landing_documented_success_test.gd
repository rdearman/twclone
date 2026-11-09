extends SceneTree

func _init() -> void:
	# The C handler's typed response keeps the authoritative ID under data;
	# Main.gd must not add a root-level response variant.
	var source := FileAccess.get_file_as_string("res://Main.gd")
	if source.contains('response.get("planet_id", 0)'):
		push_error("planet.land root-level planet_id compatibility variant is present")
		quit(1)
		return
	if not source.contains('data.get("planet_id", 0)'):
		push_error("planet.land nested authoritative planet_id handling is missing")
		quit(1)
		return
	print("Godot V3 documented planet landing response test passed")
	quit(0)
