extends SceneTree

const Notes = preload("res://LocalSectorNotes.gd")
var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var notes := Notes.new()
	var sector_id := 987654321
	notes.remove_note(sector_id)
	_check(notes.get_note(sector_id).is_empty(), "new note was not empty")
	notes.set_note(sector_id, "Batiredigo sells slaves.")
	_check(notes.get_note(sector_id) == "Batiredigo sells slaves.", "note was not stored")
	var reloaded := Notes.new()
	_check(reloaded.get_note(sector_id) == "Batiredigo sells slaves.", "note did not persist across instances")
	reloaded.set_note(sector_id, "   ")
	_check(reloaded.get_note(sector_id).is_empty(), "blank note was not removed")
	if failures.is_empty():
		print("Godot local notes tests: 4 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
