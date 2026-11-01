extends Control

const ClientState = preload("res://ClientState.gd")
const GameplayScene = preload("res://GameplayView.tscn")

func _ready() -> void:
	# This scene is a deterministic visual-capture fixture, not a playable
	# client.  It contains deliberately fixed sample state so screenshots are
	# repeatable.  If it is launched directly (for example by an exported
	# project or an editor scene run), never present that sample state as live
	# gameplay; hand control to the authoritative client scene instead.
	var showcase_mode := OS.get_environment("TWCLONE_SHOWCASE") == "1" or not OS.get_environment("TWCLONE_CAPTURE").is_empty()
	if not showcase_mode:
		# The scene is still inside the tree during _ready(); defer the scene
		# replacement so Godot does not mutate its parent while instantiating it.
		call_deferred("_redirect_to_live_client")
		return
	var showcase_width := int(OS.get_environment("TWCLONE_SHOWCASE_WIDTH"))
	if showcase_width <= 0:
		showcase_width = 1600
	get_window().size = Vector2i(showcase_width, 900 if showcase_width > 980 else 780)
	var state = ClientState.new()
	state.mark_authenticated()
	state.begin_refresh(1)
	state.accept_player({"player": {
		"id": 8, "username": "CAPTAIN", "credits": "125000.00", "turns_remaining": 438,
		"sector": 17, "ship_id": 41,
	}}, 1)
	state.accept_ship({"ship": {
		"id": 41, "name": "Wayfarer", "type_id": 3, "holds": 40, "fighters": 28,
		"shields": 100, "cargo": [{"commodity": "ore", "quantity": 12}],
	}}, 1)
	state.accept_sector({
		"sector_id": 17,
		"name": "Orion Reach",
		"ports": [{"id": 7, "name": "Helix Trade Port", "type": 2}],
		"celestial_objects": [{"planet_id": 19, "name": "Nereid", "type": "oceanic"}],
		"ships_present": [{"ship_id": 41, "name": "Wayfarer"}, {"ship_id": 74, "name": "Long Meridian"}, {"ship_id": 89, "name": "Pioneer"}, {"ship_id": 92, "name": "Far Traveller"}],
		"adjacent_sectors": [12, 18, 42],
		"counts": {"fighters": 0, "mines": 0},
	}, 1)
	var gameplay = GameplayScene.instantiate()
	gameplay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(gameplay)
	await get_tree().process_frame
	gameplay.set_snapshot(state.snapshot())
	var showcase_selection := OS.get_environment("TWCLONE_SHOWCASE_SELECT")
	if showcase_selection == "warp":
		gameplay.select_object_key("warp:18")
	elif showcase_selection.begins_with("planet:") or showcase_selection.begins_with("ship:"):
		gameplay.select_object_key(showcase_selection)
	else:
		gameplay.select_object_key("port:7")
	var showcase_workspace := OS.get_environment("TWCLONE_SHOWCASE_WORKSPACE").to_upper()
	var workspace_index := ["SECTOR", "NAVIGATION", "TRADING", "COMMUNICATIONS", "COMPUTER"].find(showcase_workspace)
	if workspace_index >= 0:
		gameplay._on_workspace_tab_changed(workspace_index)
	if OS.get_environment("TWCLONE_SHOWCASE_COMMANDS") == "1":
		gameplay.command_menu.open_for(state.snapshot(), {})
	var showcase_notice := OS.get_environment("TWCLONE_SHOWCASE_NOTICE")
	if not showcase_notice.is_empty():
		gameplay.show_notification(showcase_notice)
	if OS.get_environment("TWCLONE_SHOWCASE_WARP_EFFECT") == "1":
		gameplay.play_warp_transition()
		await get_tree().create_timer(0.28).timeout
	var showcase_category := OS.get_environment("TWCLONE_SHOWCASE_CATEGORY")
	if not showcase_category.is_empty():
		gameplay.command_menu._active_category = showcase_category
		gameplay.command_menu._rebuild_categories()
	if OS.get_environment("TWCLONE_SHOWCASE_PORT") == "1":
		var showcase_port_type := int(OS.get_environment("TWCLONE_SHOWCASE_PORT_TYPE"))
		if showcase_port_type <= 0:
			showcase_port_type = 2
		gameplay.enter_port_workflow({"port": {"id": 7, "name": "Helix Trade Port", "type": showcase_port_type, "commodities": [
			{"code": "ORE", "quantity": 120, "max_quantity": 500, "price": "42.00"},
			{"code": "ORG", "quantity": 0, "max_quantity": 400, "price": "71.00"},
		]}})
		if OS.get_environment("TWCLONE_SHOWCASE_TAVERN") == "1" and showcase_port_type == 9:
			gameplay.port_workflow.confirm_tavern_entered()
	if OS.get_environment("TWCLONE_SHOWCASE_PLANET") == "1":
		gameplay.enter_planet_workflow(19)
		gameplay.set_planet_information({"name": "Nereid", "type": "Oceanic", "owner": "Unclaimed", "sector_id": 17, "citadel_level": 1, "ore_on_hand": 240, "organics_on_hand": 180, "equipment_on_hand": 95})
		gameplay.set_planet_colonist_state({"planet_colonists": 850, "ship_colonists": 24, "ship_holds_available": 16})
	var output_path := OS.get_environment("TWCLONE_CAPTURE")
	if not output_path.is_empty():
		await get_tree().process_frame
		await get_tree().process_frame
		var capture := get_viewport().get_texture().get_image()
		if capture == null:
			push_error("The active renderer cannot capture a viewport image.")
			get_tree().quit(1)
			return
		var error := capture.save_png(output_path)
		if error != OK:
			push_error("Could not save showcase capture: %s" % error_string(error))
		else:
			print("Showcase capture saved: %s" % output_path)
			if OS.get_environment("TWCLONE_SHOWCASE_WARP_EFFECT") == "1":
				await get_tree().create_timer(0.55).timeout
		get_tree().quit(0 if error == OK else 1)

func _redirect_to_live_client() -> void:
	get_tree().change_scene_to_file("res://Main.tscn")
