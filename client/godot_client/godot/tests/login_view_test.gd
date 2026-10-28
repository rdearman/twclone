extends SceneTree

const LoginView = preload("res://LoginView.gd")

var failures: Array[String] = []
var persisted_profiles: Array = []
var persisted_index := -2
var registration: Dictionary = {}

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var view = LoginView.new()
	root.add_child(view)
	await process_frame
	view.profiles_changed.connect(func(profiles: Array, selected_index: int) -> void:
		persisted_profiles = profiles.duplicate(true)
		persisted_index = selected_index
	)
	_check(view.profile_selector != null and view.host_input != null, "login profile/address controls were not created")
	_check(_has_label(view, "SERVER ADDRESS") and _has_label(view, "PORT") and _has_label(view, "USERNAME") and _has_label(view, "PASSWORD"), "connection inputs do not have visible labels")
	_check(view.mode_toggle_button != null and view.confirm_password_input != null and not view._registration_mode, "account registration controls were not initialized")
	view.registration_requested.connect(func(host: String, port: int, username: String, password: String, ship_name: String, locale: String, timezone: String) -> void:
		registration = {"host": host, "port": port, "username": username, "password": password, "ship_name": ship_name, "ui_locale": locale, "ui_timezone": timezone}
	)
	view.set_profiles([
		{"name": "Development", "host": "localhost", "port": 1234, "username": "newguy"},
		{"name": "Test universe", "host": "192.0.2.40", "port": 4321, "username": "captain"},
	], 1)
	_check(view.profile_selector.item_count == 3 and view.profile_selector.get_item_text(2) == "Test universe", "saved server profiles were not shown in the selector")
	_check(view.host_input.text == "192.0.2.40" and view.port_input.text == "4321" and view.username_input.text == "captain", "selected server profile did not populate connection fields")
	_check(view.password_input.text.is_empty(), "loading a server profile restored a password")
	var capture_path := OS.get_environment("TWCLONE_LOGIN_CAPTURE")
	if not capture_path.is_empty():
		await process_frame
		await process_frame
		var capture := get_root().get_texture().get_image()
		var capture_error := capture.save_png(capture_path)
		_check(capture_error == OK, "login view capture could not be saved")
	view.host_input.text = "192.0.2.41"
	view._save_profile()
	_check(view._profiles.size() == 2 and view._profiles[1]["host"] == "192.0.2.41", "saving edited profile did not update its saved endpoint")
	_check(not view._profiles[1].has("password") and persisted_profiles.size() == 2 and not persisted_profiles[1].has("password"), "saved profile data included a password")
	_check(persisted_index == 1 and view.password_input.text.is_empty(), "profile save did not persist selection or clear the password field")
	view.profile_name_input.text = "Production"
	view.host_input.text = "game.example.test"
	view.port_input.text = "1234"
	view._save_profile()
	_check(view._profiles.size() == 3 and view._profiles[2]["name"] == "Production", "saving under a new profile name did not add a profile")
	view._remove_profile()
	_check(view._profiles.size() == 2 and persisted_index == -1, "removing the selected profile did not update the saved list")
	_check(view.profile_selector.get_item_text(0) == "Current connection · not saved", "profile selector does not expose unsaved current connection")
	view._toggle_registration_mode()
	_check(view._registration_mode and view._registration_fields.visible, "registration mode did not show account fields")
	view.host_input.text = "localhost"
	view.port_input.text = "1234"
	view.username_input.text = "captain"
	view.password_input.text = "first-secret"
	view.confirm_password_input.text = "different-secret"
	view._submit()
	_check(registration.is_empty() and view.status_label.text.contains("do not match"), "mismatched registration passwords were submitted")
	view.password_input.text = "registration-secret"
	view.confirm_password_input.text = "registration-secret"
	view.ship_name_input.text = "  New Scout  "
	view.ui_locale_input.text = "en-GB"
	view.ui_timezone_input.text = "Europe/London"
	view._submit()
	_check(registration.get("username") == "captain" and registration.get("port") == 1234, "valid registration did not emit server connection details")
	_check(registration.get("password") == "registration-secret" and registration.get("ship_name") == "New Scout", "registration request fields were not normalized")
	_check(registration.get("ui_locale") == "en-GB" and registration.get("ui_timezone") == "Europe/London", "optional registration preferences were omitted")
	_check(view.password_input.text.is_empty() and view.confirm_password_input.text.is_empty(), "registration form retained password values after submission")
	view.queue_free()
	await process_frame
	if failures.is_empty():
		print("Godot login view tests: 18 passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _has_label(node: Node, expected: String) -> bool:
	if node is Label and (node as Label).text == expected:
		return true
	for child in node.get_children():
		if _has_label(child, expected):
			return true
	return false

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
