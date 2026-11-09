extends SceneTree

func _init() -> void:
	var source := FileAccess.get_file_as_string("res://AuthoritativeRefresh.gd")
	if source.contains("response_type == null") or source.contains("str(response_type).is_empty()"):
		push_error("refresh acceptance still permits an untyped response variant")
		quit(1)
		return
	if not source.contains("response_type == expected_type"):
		push_error("refresh acceptance does not require the authoritative response type")
		quit(1)
		return
	print("Godot V3 PLAN-01 refresh type contract test passed")
	quit(0)
