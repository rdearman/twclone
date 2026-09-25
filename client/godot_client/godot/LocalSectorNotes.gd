class_name TwLocalSectorNotes
extends RefCounted

const PATH := "user://sector_notes.cfg"
var _notes: Dictionary = {}

func _init() -> void:
	_load()

func get_note(sector_id: int) -> String:
	return str(_notes.get(str(sector_id), ""))

func set_note(sector_id: int, note: String) -> void:
	var key := str(sector_id)
	var clean := note.strip_edges()
	if clean.is_empty():
		_notes.erase(key)
	else:
		_notes[key] = clean
	_save()

func remove_note(sector_id: int) -> void:
	_notes.erase(str(sector_id))
	_save()

func _load() -> void:
	var config := ConfigFile.new()
	if config.load(PATH) != OK:
		return
	if not config.has_section("sectors"):
		return
	for key in config.get_section_keys("sectors"):
		var value = config.get_value("sectors", key, "")
		if value is String and not value.strip_edges().is_empty():
			_notes[key] = value

func _save() -> void:
	var config := ConfigFile.new()
	for key in _notes:
		config.set_value("sectors", key, _notes[key])
	config.save(PATH)
