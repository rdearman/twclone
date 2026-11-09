extends SceneTree

const GameplayView = preload("res://GameplayView.gd")

var failures: Array[String] = []
var checks := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var view = GameplayView.new()
	root.add_child(view)
	await process_frame
	var expected := {
		"NAVIGATION": ["SCAN ADJACENT SECTORS", "PLOT ROUTE", "EXPRESS", "SINGLE STEP", "PAUSE", "RESUME", "CANCEL"],
		"TRADING": ["DOCK · OPEN PORT", "PORT INFORMATION", "BANK BALANCE"],
		"COMMUNICATIONS": ["CHAT", "MAIL", "BROADCASTS", "NOTICES", "OFFERS", "BOUNTIES", "PLAYERS"],
		"COMPUTER": ["PLAYER INFO", "SHIP STATUS", "SECTOR INFO", "DETAILED SCAN"],
	}
	for index in range(1, view.WORKSPACES.size()):
		view._on_workspace_tab_changed(index)
		await process_frame
		var text := _button_text(view.workspace_controls_column)
		for required in expected[view.WORKSPACES[index]]:
			_check(text.has(required), "%s workspace is missing %s" % [view.WORKSPACES[index], required])
		_check(view.center_split.visible, "%s workspace lost the graphical sector viewport" % view.WORKSPACES[index])
		_check(view.workspace_stack.get_parent() == view.center_split, "%s workspace controls are not in the viewport's second column" % view.WORKSPACES[index])
		_check(view.workspace_stack.size_flags_vertical == Control.SIZE_EXPAND_FILL, "%s workspace did not fill the viewport column" % view.WORKSPACES[index])
		if view.WORKSPACES[index] == "COMPUTER":
			_check(not view.workspace_controls.visible, "%s retained a duplicate shortcut panel" % view.WORKSPACES[index])
			_check(view.command_menu.visible, "%s did not open its integrated command surface" % view.WORKSPACES[index])
		if view.WORKSPACES[index] == "COMMUNICATIONS":
			_check(view.workspace_controls.visible, "Communications workspace is not visible")
			_check(not view.command_menu.visible, "Communications retained the generic command catalogue")
		if view.WORKSPACES[index] == "TRADING":
			_check(not view.command_menu.visible, "Trading retained the generic command catalogue")
	view._on_workspace_tab_changed(0)
	await process_frame
	_check(view.center_split.visible, "Sector workspace did not show the graphical sector viewport")
	_check(_find_button(view.command_panel, "SCAN ADJACENT SECTORS") != null, "Sector viewport menu did not expose adjacent-sector scanning")
	view._on_workspace_tab_changed(1)
	await process_frame
	var destination := view.workspace_controls_column.find_child("NavigationDestination", true, false) as LineEdit
	_check(destination != null, "navigation destination control was not created")
	var plot_route := _find_button(view.workspace_controls_column, "PLOT ROUTE")
	var express := _find_button(view.workspace_controls_column, "EXPRESS")
	_check(plot_route != null and plot_route.disabled, "Plot Route was enabled before a destination was entered")
	_check(express != null and express.disabled, "Express was enabled before a route was plotted")
	if destination != null:
		destination.text = "999"
	_check(plot_route != null and not plot_route.disabled, "Plot Route did not enable for a valid destination")
	_check(express != null and express.disabled, "Express enabled before the route was plotted")
	view.show_planned_route([1, 2, 999], 999)
	_check(express != null and not express.disabled, "Express remained disabled after the route was plotted")
	view.show_command_result("Scan adjacent sectors", {"sector_id": 999, "adjacent_sectors": [{"to_sector": 331}]})
	_check(view.information_panel.visible, "Navigation scan result did not reveal the Information panel")
	view._on_workspace_tab_changed(4)
	await process_frame
	view._on_workspace_tab_changed(1)
	await process_frame
	destination = view.workspace_controls_column.find_child("NavigationDestination", true, false) as LineEdit
	_check(destination != null and destination.text == "999", "navigation destination was lost when switching workspaces")
	_check(not view.workspace_status.text.contains("server-backed commands and results"), "workspace status exposed developer-oriented placeholder text")
	_check(not view._note_content.visible, "private sector note editor is expanded by default")
	view._toggle_note_section()
	_check(view._note_content.visible, "private sector note editor did not expand")
	_check(view._note_toggle.text.contains("▾"), "expanded note editor did not update its toggle")
	view._on_workspace_tab_changed(4)
	view.command_menu.select_category("MAIL & SUBSCRIPTIONS")
	await process_frame
	var send_mail := _find_button(view.command_menu._action_list, "Send mail")
	_check(send_mail != null, "Computer command catalogue did not expose Send mail")
	if send_mail != null:
		send_mail.pressed.emit()
		await process_frame
		_check(view.command_menu._inline_form_panel.visible, "routine command parameters did not open inline")
		_check(not view.command_menu._form_dialog.visible, "routine command parameters still opened a modal")
	view.queue_free()
	if failures.is_empty():
		print("Godot workspace layout tests: %d checks passed" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		print("Godot workspace layout tests: %d failed" % failures.size())
		quit(1)

func _button_text(node: Node) -> Array[String]:
	var result: Array[String] = []
	for child in node.get_children():
		if child is Button:
			result.append(str(child.text))
		result.append_array(_button_text(child))
	return result

func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures.append(message)

func _find_button(node: Node, label: String) -> Button:
	for child in node.get_children():
		if child is Button and str(child.text) == label:
			return child
		var nested := _find_button(child, label)
		if nested != null:
			return nested
	return null
