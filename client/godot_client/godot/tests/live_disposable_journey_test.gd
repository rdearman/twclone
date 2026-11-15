extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures: Array[String] = []
var app
var registered_username := ""
var test_password := "Temporary-QA-Password-42"
var login_existing_account := false

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	if OS.get_environment("TWCLONE_GODOT_LIVE") != "1":
		print("SKIP: set TWCLONE_GODOT_LIVE=1 for a dedicated disposable server journey")
		quit(0)
		return
	login_existing_account = OS.get_environment("TWCLONE_GODOT_LOGIN") == "1"
	if login_existing_account:
		registered_username = OS.get_environment("TWCLONE_GODOT_USERNAME")
		test_password = OS.get_environment("TWCLONE_GODOT_PASSWORD")
		_check(not registered_username.is_empty() and not test_password.is_empty(), "live login acceptance requires a disposable username and password")
		if registered_username.is_empty() or test_password.is_empty():
			quit(1)
			return
	app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	var login = app.get_node("LoginView")
	randomize()
	if not login_existing_account:
		registered_username = "gd%d" % randi_range(10000000, 99999999)
	login.host_input.text = "127.0.0.1"
	login.port_input.text = OS.get_environment("TWCLONE_GODOT_PORT") if not OS.get_environment("TWCLONE_GODOT_PORT").is_empty() else "55499"
	login.username_input.text = registered_username
	login.password_input.text = test_password
	if not login_existing_account:
		login.mode_toggle_button.pressed.emit()
		login.confirm_password_input.text = test_password
		login.ship_name_input.text = "Disposable QA"
	login.connect_button.pressed.emit()
	var authenticated := await _wait_until(func() -> bool:
		var snapshot: Dictionary = app.client_state.snapshot()
		var freshness: Dictionary = snapshot.get("freshness", {})
		return app.auth_session.is_authenticated() and freshness.get("player") == "fresh" \
			and freshness.get("ship") == "fresh" and freshness.get("sector") == "fresh",
		240
	)
	_check(authenticated, "ordinary-player authentication through the Godot LoginView did not complete")
	if not authenticated:
		_finish()
		return
	var initial: Dictionary = app.client_state.snapshot()
	var hud: Dictionary = initial.get("hud", {})
	var initial_sector: int = int(hud.get("sector_id", 0))
	var initial_turns: int = int(hud.get("turns_remaining", -1))
	var ship_id: int = int(hud.get("ship_id", 0))
	_check(initial_sector > 0 and initial_turns >= 0 and ship_id > 0, "starting sector, turns or active ship were absent after authoritative refresh")
	var adjacent: Array = initial.get("sector", {}).get("adjacent_sector_ids", [])
	_check(not adjacent.is_empty(), "confirmed sector did not expose connected navigation targets")
	if adjacent.is_empty():
		_finish()
		return
	var destination := int(adjacent[0])
	var menu = app.gameplay_view.command_menu
	menu.select_category("NAVIGATION")
	await process_frame
	var warp_button := _find_button(menu._action_list, "Warp to sector number")
	_check(warp_button != null and not warp_button.disabled, "navigation menu did not expose a usable direct warp action")
	if warp_button == null or warp_button.disabled:
		_finish()
		return
	warp_button.pressed.emit()
	if not menu._form_fields.has("to_sector_id"):
		_check(false, "warp menu action did not open its destination form")
		_finish()
		return
	(menu._form_fields["to_sector_id"] as LineEdit).text = str(destination)
	menu._inline_submit.pressed.emit()
	var moved := await _wait_until(func() -> bool:
		return (int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0)) == destination
			and not app.gameplay_view.command_menu._busy),
		240
	)
	_check(moved, "Godot warp action did not settle at the selected connected sector")
	var after_move: Dictionary = app.client_state.snapshot()
	_check(int(after_move.get("hud", {}).get("turns_remaining", initial_turns)) == initial_turns - 1, "Godot warp did not consume exactly one turn")
	# The disposable acceptance fixture places its StarDock and owned planet in
	# sector 2. Existing-account runs can start in either connected sector, so
	# return to the fixture sector through the ordinary navigation menu before
	# exercising those workflows.
	if int(after_move.get("hud", {}).get("sector_id", 0)) != 2:
		menu.select_category("NAVIGATION")
		await process_frame
		var fixture_warp_button := _find_button(menu._action_list, "Warp to sector number")
		_check(fixture_warp_button != null and not fixture_warp_button.disabled, "navigation menu could not return to the disposable gameplay fixture")
		if fixture_warp_button != null and not fixture_warp_button.disabled:
			fixture_warp_button.pressed.emit()
			if menu._form_fields.has("to_sector_id"):
				(menu._form_fields["to_sector_id"] as LineEdit).text = "2"
				menu._inline_submit.pressed.emit()
				var reached_fixture_sector := await _wait_until(func() -> bool:
					return int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0)) == 2 and not menu._busy,
					240
				)
				_check(reached_fixture_sector, "ordinary-player warp did not return to the disposable port and planet sector")
			else:
				_check(false, "fixture-sector warp did not open its destination form")
	menu.select_category("NAVIGATION")
	await process_frame
	var scan_button := _find_button(menu._action_list, "Detailed current-sector scan")
	_check(scan_button != null and not scan_button.disabled, "navigation menu did not expose the current-sector scan")
	if scan_button != null and not scan_button.disabled:
		var before_info: String = app.gameplay_view.information_text.text
		scan_button.pressed.emit()
		var scanned := await _wait_until(func() -> bool:
			return (not app.gameplay_view.command_menu._busy
				and app.gameplay_view.information_text.text != before_info),
			180
		)
		_check(scanned, "Godot scan did not return a client-visible server result")
	else:
		_finish()
		return
	# Visit a real StarDock port using the sector selection and contextual
	# command menu. Docking and trading are driven through the same buttons and
	# handlers used by a player; the test never sends protocol requests itself.
	var port_id := 1
	var port_key := "port:%d" % port_id
	var sector_snapshot: Dictionary = app.client_state.snapshot().get("sector", {})
	var port_rows: Array = sector_snapshot.get("ports", [])
	var visible_port := false
	for port_row in port_rows:
		if port_row is Dictionary and int(port_row.get("id", port_row.get("port_id", 0))) == port_id:
			visible_port = true
	_check(visible_port, "server-confirmed current sector did not show the disposable StarDock fixture")
	if visible_port:
		app.gameplay_view.select_object_key(port_key)
		menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
		await process_frame
		var dock_button := _find_button(menu._action_list, "Dock at selected port")
		_check(dock_button != null and not dock_button.disabled, "selected StarDock did not expose the dock action")
		if dock_button != null and not dock_button.disabled:
			dock_button.pressed.emit()
			_check(menu._form_dialog.visible, "docking did not require player confirmation")
			(menu._form_dialog.get_ok_button() as Button).pressed.emit()
			var docked := await _wait_until(func() -> bool:
				return int(app.client_state.snapshot().get("ship", {}).get("ported", 0)) == port_id and not menu._busy,
				240
			)
			_check(docked, "server-confirmed dock operation did not refresh the active ship")
			if docked:
				app.gameplay_view.select_object_key(port_key)
				menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
				await process_frame
				var port_info_button := _find_button(menu._action_list, "Dock · open port screen")
				_check(port_info_button != null and not port_info_button.disabled, "selected port did not expose its port workflow")
				if port_info_button != null and not port_info_button.disabled:
					port_info_button.pressed.emit()
					var port_open := await _wait_until(func() -> bool:
						return app.gameplay_view.port_workflow.visible and not menu._busy,
						180
					)
					_check(port_open, "port.info did not open the server-backed trade workflow")
					if port_open:
						var ore_controls: Dictionary = app.gameplay_view.port_workflow._row_controls.get("ORE", {})
						_check(ore_controls.has("buy") and not (ore_controls.get("buy") as Button).disabled, "port workflow did not expose the configured ORE purchase")
						if ore_controls.has("buy") and not (ore_controls.get("buy") as Button).disabled:
							var before_ore := _cargo_quantity(app.client_state.snapshot().get("ship", {}), "ORE")
							(ore_controls["quantity"] as LineEdit).text = "2"
							(ore_controls["buy"] as Button).pressed.emit()
							var bought := await _wait_until(func() -> bool:
								return (_cargo_quantity(app.client_state.snapshot().get("ship", {}), "ORE") == before_ore + 2
									and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
								240
							)
							_check(bought, "ordinary-player port purchase did not update the authoritative cargo snapshot")
							if bought:
								var equipment_controls: Dictionary = app.gameplay_view.port_workflow._row_controls.get("EQU", {})
								_check(equipment_controls.has("sell") and not (equipment_controls.get("sell") as Button).disabled, "port workflow did not expose the configured equipment sale")
								if equipment_controls.has("sell") and not (equipment_controls.get("sell") as Button).disabled:
									var before_equipment := _cargo_quantity(app.client_state.snapshot().get("ship", {}), "EQU")
									(equipment_controls["quantity"] as LineEdit).text = "1"
									(equipment_controls["sell"] as Button).pressed.emit()
									var sold := await _wait_until(func() -> bool:
										return (_cargo_quantity(app.client_state.snapshot().get("ship", {}), "EQU") == before_equipment - 1
											and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
										240
									)
									_check(sold, "ordinary-player port sale did not update the authoritative cargo snapshot")
									if sold:
										# Leave the port through its contextual action, then land on
										# an owned disposable planet and harvest using the surface UI.
										app.gameplay_view.port_workflow._back_button.pressed.emit()
										await process_frame
										app.gameplay_view.select_object_key(port_key)
										menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
										await process_frame
										var undock_button := _find_button(menu._action_list, "Undock from selected port")
										_check(undock_button != null and not undock_button.disabled, "docked player could not reach the undock workflow")
										var undocked := false
										if undock_button != null and not undock_button.disabled:
											undock_button.pressed.emit()
											if menu._form_dialog.visible:
												(menu._form_dialog.get_ok_button() as Button).pressed.emit()
											undocked = await _wait_until(func() -> bool:
												var current_snapshot: Dictionary = app.client_state.snapshot()
												return (int(current_snapshot.get("ship", {}).get("ported", 0)) == 0
													and app._pending_game_commands.is_empty() and not app._command_refresh_pending
													and current_snapshot.get("freshness", {}).get("sector") == "fresh"),
												240
											)
										_check(undocked, "undock action did not refresh authoritative ship state")
										if undocked:
											app.gameplay_view.select_object_key("planet:4")
											var selected_planet: Dictionary = app.gameplay_view.sector_view.selected_object()
											menu.open_for(app.client_state.snapshot(), selected_planet)
											await process_frame
											var land_button := _find_button(menu._action_list, "Land on planet")
											_check(land_button != null and not land_button.disabled, "owned planet did not expose player landing")
											var landed := false
											if land_button != null and not land_button.disabled:
												land_button.pressed.emit()
												landed = await _wait_until(func() -> bool:
													return app._landing_surface_ready and app._landed_planet_id == 4 and app.gameplay_view.planet_workflow.visible,
													240
												)
											_check(landed, "ordinary-player landing did not expose the server-confirmed planet surface")
											if landed:
												var surface_ready := await _wait_until(func() -> bool:
													return app._pending_game_commands.is_empty() and app._planet_pending_count == 0 and not menu._busy,
													240
												)
												_check(surface_ready, "planet surface reads did not settle before gameplay actions")
												app.gameplay_view.select_object_key("planet:4")
												menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
												await process_frame
												var harvest_button := _find_button(menu._action_list, "Harvest planet production")
												_check(harvest_button != null and not harvest_button.disabled, "landed owner could not reach the published harvest workflow")
												var harvested := false
												if harvest_button != null and not harvest_button.disabled:
													var ore_before_harvest := _cargo_quantity(app.client_state.snapshot().get("ship", {}), "ORE")
													harvest_button.pressed.emit()
													_check(menu._form_dialog.visible, "planet harvest did not require mutation confirmation")
													if menu._form_dialog.visible:
														(menu._form_dialog.get_ok_button() as Button).pressed.emit()
													harvested = await _wait_until(func() -> bool:
														return (_cargo_quantity(app.client_state.snapshot().get("ship", {}), "ORE") == ore_before_harvest + 3
															and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
														240
													)
												_check(harvested and app.gameplay_view.notification_label.text.contains("Planet harvest confirmed"), "ordinary-player harvest did not apply the typed receipt and authoritative cargo change")
												if harvested:
													var surface_mutation_settled := await _wait_until(func() -> bool:
														return app._pending_game_commands.is_empty() and app._planet_pending_count == 0 and not menu._busy,
														240
													)
													_check(surface_mutation_settled, "harvest refresh did not settle before launch")
													app.gameplay_view.select_object_key("planet:4")
													menu.open_for(app.client_state.snapshot(), app.gameplay_view.sector_view.selected_object())
													await process_frame
													var launch_button := _find_button(menu._action_list, "Launch from planet")
													_check(launch_button != null and not launch_button.disabled, "landed player could not reach launch")
													if launch_button != null and not launch_button.disabled:
														launch_button.pressed.emit()
														if menu._form_dialog.visible:
															(menu._form_dialog.get_ok_button() as Button).pressed.emit()
														var departed := await _wait_until(func() -> bool:
															var current_snapshot: Dictionary = app.client_state.snapshot()
															return (int(current_snapshot.get("ship", {}).get("onplanet", 1)) == 0
																and app._landed_planet_id == 0 and not app.gameplay_view.planet_workflow.visible
																and app._pending_game_commands.is_empty() and not app._command_refresh_pending),
															240
														)
														_check(departed, "launch did not close the surface after authoritative departure refresh")
	if OS.get_environment("TWCLONE_GODOT_FEEDBACK") == "1":
		# Use the actual Computer menu and player-facing feedback dialogs, including
		# the server-generated public preview and final submit confirmation.
		menu.select_category("COMPUTER")
		await process_frame
		var feedback_button := _find_button(menu._action_list, "Report Bug / Suggest Improvement")
		_check(feedback_button != null, "Computer menu did not expose player feedback")
		if feedback_button != null:
			feedback_button.pressed.emit()
			var fields: Dictionary = app._feedback_fields
			(fields["kind"] as OptionButton).select(1)
			(fields["subject"] as LineEdit).text = "Disposable Godot feedback acceptance"
			(fields["description"] as TextEdit).text = "Test-only suggestion submitted from the Godot UI."
			(app._feedback_form.get_ok_button() as Button).pressed.emit()
			var preview_ready := await _wait_until(func() -> bool: return app._feedback_preview.visible, 120)
			_check(preview_ready and app._feedback_preview_text.text.contains("Disposable Godot feedback acceptance"), "feedback preview was not returned to the actual UI")
			var report_id := str(app._feedback_payload.get("report_id", ""))
			_check(not report_id.is_empty(), "feedback preview did not supply an idempotent report reference")
			if preview_ready:
				_check(not app._feedback_preview_text.text.contains(test_password), "public feedback preview contains the player password")
				(app._feedback_preview.get_ok_button() as Button).pressed.emit()
				var submitted := await _wait_until(func() -> bool: return app._feedback_result.visible, 120)
				_check(submitted and app._feedback_result.dialog_text.contains(report_id), "feedback result did not display its local report reference")
				app._feedback_result.hide()
				var status_button := _find_button(menu._action_list, "Check feedback report status")
				_check(status_button != null, "Computer menu did not expose feedback status lookup")
				var status_ready := false
				if status_button != null:
					status_button.pressed.emit()
					app._feedback_status_input.text = report_id
					(app._feedback_status_form.get_ok_button() as Button).pressed.emit()
					status_ready = await _wait_until(func() -> bool: return app._feedback_result.visible, 120)
				_check(status_ready and app._feedback_result.dialog_text.contains(report_id), "feedback status was not returned through the Godot UI")
	var persisted_player_id := int(app.auth_session.player_id)
	var persisted_sector_id := int(app.client_state.snapshot().get("hud", {}).get("sector_id", 0))
	var persisted_turns := int(app.client_state.snapshot().get("hud", {}).get("turns_remaining", -1))
	app._feedback_result.hide()
	menu.select_category("COMPUTER")
	await process_frame
	var logout_button := _find_button(menu._action_list, "Log out to login screen")
	_check(logout_button != null, "Session menu did not expose logout")
	if logout_button != null:
		logout_button.pressed.emit()
		_check(menu._logout_dialog.visible, "logout action did not request explicit confirmation")
		(menu._logout_dialog.get_ok_button() as Button).pressed.emit()
		var logged_out := await _wait_until(func() -> bool:
			return not app.auth_session.is_authenticated() and app.login_view.visible,
			120
		)
		_check(logged_out, "logout did not return the player to the login view")
		if not login_existing_account:
			app.login_view.mode_toggle_button.pressed.emit() # registration -> login
		app.login_view.host_input.text = "127.0.0.1"
		app.login_view.port_input.text = OS.get_environment("TWCLONE_GODOT_PORT")
		app.login_view.username_input.text = registered_username
		app.login_view.password_input.text = test_password
		app.login_view.connect_button.pressed.emit()
		var reconnected := await _wait_until(func() -> bool:
			var snapshot: Dictionary = app.client_state.snapshot()
			var freshness: Dictionary = snapshot.get("freshness", {})
			return (app.auth_session.is_authenticated() and int(app.auth_session.player_id) == persisted_player_id
				and freshness.get("player") == "fresh" and freshness.get("ship") == "fresh" and freshness.get("sector") == "fresh"),
			240
		)
		_check(reconnected, "ordinary-player login did not reconnect to the persisted account")
		if reconnected:
			var resumed: Dictionary = app.client_state.snapshot()
			_check(int(resumed.get("hud", {}).get("sector_id", 0)) == persisted_sector_id, "reconnect did not restore the persisted sector")
			_check(int(resumed.get("hud", {}).get("turns_remaining", -1)) == persisted_turns, "reconnect changed the persisted turn balance")
		# End the accepted journey through the player-facing logout confirmation
		# as well, so the harness leaves no live socket or pending UI state behind.
		menu.select_category("COMPUTER")
		await process_frame
		var final_logout_button := _find_button(menu._action_list, "Log out to login screen")
		if final_logout_button != null:
			final_logout_button.pressed.emit()
			if menu._logout_dialog.visible:
				(menu._logout_dialog.get_ok_button() as Button).pressed.emit()
			await _wait_until(func() -> bool: return not app.auth_session.is_authenticated(), 120)
	var auth_journey := "existing-login" if login_existing_account else "registration"
	var feedback_journey := ",feedback" if OS.get_environment("TWCLONE_GODOT_FEEDBACK") == "1" else ""
	print("LIVE_DISPOSABLE_PASS journey=%s,navigation,turns,scan,docking,port-workflow,commodity-buy,commodity-sale,planet-landing,harvest,launch%s,logout,reconnect player_id=%d origin=%d destination=%d ship_id=%d" % [auth_journey, feedback_journey, persisted_player_id, initial_sector, destination, ship_id])
	_finish()

func _wait_until(predicate: Callable, frames: int) -> bool:
	for _frame in frames:
		if predicate.call():
			return true
		await process_frame
		# Headless tests can process hundreds of frames before the TCP poller or
		# PostgreSQL-backed server gets a scheduling slice. Keep real network time
		# in the acceptance harness instead of exhausting the frame budget early.
		await create_timer(0.02).timeout
	return predicate.call()

func _find_button(parent: Node, label: String) -> Button:
	for child in parent.get_children():
		if child is Button and (child as Button).text == label:
			return child
	return null

func _cargo_quantity(ship: Variant, commodity: String) -> int:
	if not ship is Dictionary:
		return -1
	var cargo: Variant = ship.get("cargo", [])
	if not cargo is Array:
		return -1
	var aliases := {"EQUIPMENT": "EQU", "ORGANICS": "ORG", "SLAVES": "SLV", "DRUGS": "DRG", "WEAPONS": "WPN"}
	for item in cargo:
		if item is Dictionary:
			var raw_code := str(item.get("commodity", "")).to_upper()
			if str(aliases.get(raw_code, raw_code)) == commodity:
				return int(item.get("quantity", -1))
	return 0

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)

func _finish() -> void:
	# Let deferred UI row removals and server-reply handlers settle before
	# tearing down the root scene. The live journey rebuilds several dynamic
	# menus while their prior action labels are queued for deletion.
	for _frame in 3:
		await process_frame
	if is_instance_valid(app):
		var finished_app = app
		finished_app.queue_free()
		await finished_app.tree_exited
	for _frame in 6:
		await process_frame
	await create_timer(0.1).timeout
	if failures.is_empty():
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)
