extends SceneTree

const MainScene = preload("res://Main.tscn")

var failures := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var app = MainScene.instantiate()
	root.add_child(app)
	await process_frame
	_check(is_instance_valid(app._feedback_form), "feedback form was not created")
	_check(is_instance_valid(app._feedback_preview), "feedback preview was not created")
	_check(app._feedback_preview.size == Vector2i(760, 620), "feedback preview window size was not applied")
	_check(is_instance_valid(app._feedback_preview_text), "feedback preview text was not created")
	_check(app._feedback_preview_text.custom_minimum_size == Vector2(700, 470), "feedback preview text layout was not applied")
	app.queue_free()
	await process_frame
	if failures == 0:
		print("feedback_ui_test: PASS")
		quit(0)
	else:
		push_error("feedback_ui_test: %d failure(s)" % failures)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if condition:
		return
	failures += 1
	push_error(message)
