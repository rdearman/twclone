extends SceneTree

const CommandMenu = preload("res://CommandMenu.gd")
const PortWorkflow = preload("res://PortWorkflow.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var menu := CommandMenu.new()
	root.add_child(menu)
	await process_frame

	var form_label := menu._form_dialog.get_label()
	_check(menu._form_content.get_parent() == form_label.get_parent(), "command form content bypassed the dialog's vertical layout")
	_check(form_label.autowrap_mode == TextServer.AUTOWRAP_WORD_SMART, "command dialog description does not wrap")

	menu._choose_action({
		"command": "hardware.buy",
		"label": "Long prompt form",
		"fields": [["item_code", "text", "A deliberately long hardware item description that must wrap instead of colliding with the input", ""]]
	})
	await process_frame
	var field_row: VBoxContainer = menu._form_content.get_child(0)
	var field_label: Label = field_row.get_child(0)
	_check(field_row.get_child_count() == 2, "command form field did not keep its label and input in one vertical row")
	_check(field_label.autowrap_mode == TextServer.AUTOWRAP_WORD_SMART, "long command field label does not wrap")
	_check((field_row.get_child(1) as Control).size_flags_horizontal & Control.SIZE_EXPAND_FILL, "command form input does not expand within the dialog")

	var port := PortWorkflow.new()
	root.add_child(port)
	await process_frame
	var trade_label := port._trade_dialog.get_label()
	var trade_body: Control = null
	for child in trade_label.get_parent().get_children():
		if child is VBoxContainer and child != trade_label:
			trade_body = child
	_check(trade_body != null and trade_body.get_parent() == trade_label.get_parent(), "trade dialog content bypassed the dialog's vertical layout")
	_check(trade_body != null and trade_body.size_flags_horizontal & Control.SIZE_EXPAND_FILL, "trade dialog body does not resize with the window")

	menu.queue_free()
	port.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot dialog layout tests: 8 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
