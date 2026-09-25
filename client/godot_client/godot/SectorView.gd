extends Control

signal object_selected(selection_key: String, source: String)
signal warp_selected(destination: int, source: String)
signal warp_activated(destination: int)

const BACKDROP := preload("res://assets/sector_starfield.png")
const OBJECT_ATLAS := preload("res://assets/sector_objects_atlas.png")

var world_layer: Node2D
var current_objects: Array[Dictionary] = []
var selected_key := ""
var _sprite_nodes: Dictionary = {}
var _warp_markers: Dictionary = {}
var _hovered_key := ""
var _beacon_panel: PanelContainer
var _beacon_label: Label

func _ready() -> void:
	clip_contents = true
	# Let the Node2D/Area2D hit targets receive artwork clicks. The parent
	# Control must not consume the event before it reaches the object.
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	focus_mode = Control.FOCUS_ALL
	var background := TextureRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.texture = BACKDROP
	background.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	background.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(background)
	var wash := ColorRect.new()
	wash.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	wash.color = Color(0.015, 0.027, 0.055, 0.12)
	wash.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(wash)
	_beacon_panel = PanelContainer.new()
	_beacon_panel.set_anchors_preset(Control.PRESET_TOP_RIGHT)
	_beacon_panel.position = Vector2(-360, 18)
	_beacon_panel.custom_minimum_size = Vector2(330, 0)
	_beacon_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_beacon_panel.add_theme_stylebox_override("panel", _beacon_style())
	var beacon_margin := MarginContainer.new()
	beacon_margin.add_theme_constant_override("margin_left", 13)
	beacon_margin.add_theme_constant_override("margin_right", 13)
	beacon_margin.add_theme_constant_override("margin_top", 9)
	beacon_margin.add_theme_constant_override("margin_bottom", 9)
	_beacon_panel.add_child(beacon_margin)
	_beacon_label = Label.new()
	_beacon_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_beacon_label.add_theme_font_size_override("font_size", 13)
	_beacon_label.add_theme_color_override("font_color", Color(0.96, 0.9, 0.72))
	beacon_margin.add_child(_beacon_label)
	add_child(_beacon_panel)
	world_layer = Node2D.new()
	world_layer.name = "WorldLayer"
	add_child(world_layer)
	resized.connect(_layout_objects)

func compose(sector: Dictionary, selection_key: String) -> void:
	var beacon := str(sector.get("beacon", "")).strip_edges()
	_beacon_label.text = "BEACON\n" + beacon if not beacon.is_empty() else "NO BEACON MESSAGE"
	_beacon_panel.visible = not beacon.is_empty()
	current_objects.clear()
	for item in sector.get("ports", []):
		if item is Dictionary:
			current_objects.append(_descriptor("port", item, "port", ["id", "port_id"]))
	for item in sector.get("planets", []):
		if item is Dictionary:
			current_objects.append(_descriptor("planet", item, "planet", ["id", "planet_id"]))
	for item in sector.get("ships", []):
		if item is Dictionary:
			current_objects.append(_descriptor("ship", item, "ship", ["id", "ship_id"]))
	# Keep records with a missing server ID visible as non-actionable artwork so
	# the player can see the object and the details panel can explain the gap.
	current_objects.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return str(a["key"]) < str(b["key"]))
	selected_key = selection_key
	_rebuild_art_objects()
	_rebuild_warp_markers(sector.get("adjacent_sector_ids", []))

func select_object(selection_key: String, source: String = "artwork") -> void:
	if _find_object(selection_key).is_empty():
		return
	selected_key = selection_key
	_update_highlight()
	object_selected.emit(selection_key, source)

func _beacon_style() -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.015, 0.04, 0.06, 0.9)
	style.border_color = Color(0.82, 0.63, 0.31, 0.8)
	style.set_border_width_all(1)
	style.set_corner_radius_all(8)
	return style

func select_warp(destination: int) -> void:
	if not _warp_markers.has(destination):
		return
	selected_key = "warp:%d" % destination
	_update_highlight()

func selected_object() -> Dictionary:
	return _find_object(selected_key)

func _descriptor(kind: String, source: Dictionary, sprite_kind: String, id_fields: Array) -> Dictionary:
	var identity = null
	for field in id_fields:
		if source.has(field) and str(source[field]) != "":
			identity = source[field]
			break
	if identity == null:
		var fallback_name := str(source.get("name", source.get("ship_name", kind.capitalize())))
		var fallback_key := "%s:unknown:%s" % [kind, fallback_name.to_lower().replace(" ", "_")]
		return {"kind": kind, "name": fallback_name, "key": fallback_key, "selectable": false, "data": source.duplicate(true), "sprite_kind": "ship" if kind == "ship" else sprite_kind}
	var display_name := str(source.get("name", source.get("ship_name", "Unnamed %s" % kind.capitalize())))
	return {
		"kind": kind,
		"name": display_name,
		"key": "%s:%s" % [kind, str(identity)],
		"selectable": true,
		"data": source.duplicate(true),
		"sprite_kind": "ship" if kind == "ship" else sprite_kind,
	}

func _rebuild_art_objects() -> void:
	for child in world_layer.get_children():
		child.queue_free()
	_sprite_nodes.clear()
	for object in current_objects:
		var art_object := Node2D.new()
		art_object.name = str(object["key"]).replace(":", "_")
		art_object.set_meta("selection_key", object["key"])
		var sprite := Sprite2D.new()
		sprite.texture = _atlas_region(str(object["sprite_kind"]))
		sprite.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
		sprite.scale = Vector2.ONE * _sprite_scale(str(object["sprite_kind"]))
		art_object.add_child(sprite)
		var hit_area := Area2D.new()
		hit_area.input_pickable = true
		hit_area.collision_layer = 1
		hit_area.collision_mask = 1
		hit_area.z_index = 2
		hit_area.mouse_entered.connect(_on_object_hovered.bind(str(object["key"])))
		hit_area.mouse_exited.connect(_on_object_unhovered.bind(str(object["key"])))
		var hit_shape := CollisionShape2D.new()
		var circle := CircleShape2D.new()
		# Match the visible atlas cell rather than relying on the transparent
		# sprite pixels to define input. This keeps port/planet clicks reliable.
		circle.radius = 108.0 if str(object["kind"]) == "ship" else 122.0
		hit_shape.shape = circle
		hit_area.add_child(hit_shape)
		art_object.add_child(hit_area)
		hit_area.input_event.connect(_on_object_input.bind(str(object["key"])))
		var halo := SelectionHalo.new()
		halo.visible = false
		art_object.add_child(halo)
		var anchor_label := Label.new()
		anchor_label.text = "%s  ·  %s" % [str(object["kind"]).to_upper(), str(object["name"])]
		anchor_label.position = Vector2(-116, 92)
		anchor_label.add_theme_font_size_override("font_size", 12)
		anchor_label.add_theme_color_override("font_color", Color(0.96, 0.93, 0.83))
		anchor_label.add_theme_color_override("font_shadow_color", Color(0.005, 0.012, 0.02, 0.95))
		anchor_label.add_theme_constant_override("shadow_offset_x", 1)
		anchor_label.add_theme_constant_override("shadow_offset_y", 2)
		anchor_label.visible = false
		anchor_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		art_object.add_child(anchor_label)
		_sprite_nodes[str(object["key"])] = {"root": art_object, "sprite": sprite, "halo": halo, "label": anchor_label}
		world_layer.add_child(art_object)
	_layout_objects()
	_update_highlight()

func _layout_objects() -> void:
	if not is_instance_valid(world_layer):
		return
	var compact := size.y < 300.0 or size.x < 640.0
	var ports: Array[String] = []
	var planets: Array[String] = []
	var ships: Array[String] = []
	for item in current_objects:
		match str(item.get("kind", "")):
			"port": ports.append(str(item["key"]))
			"planet": planets.append(str(item["key"]))
			"ship": ships.append(str(item["key"]))
	_place_group(ports, Vector2(0.54, 0.34) if compact else Vector2(0.54, 0.42), Vector2(0.09, 0.05) if compact else Vector2(0.13, 0.06))
	_place_group(planets, Vector2(0.80, 0.56) if compact else Vector2(0.78, 0.68), Vector2(0.07, 0.05) if compact else Vector2(0.09, 0.04))
	_place_ships(ships, compact)
	for key in _sprite_nodes:
		var nodes: Dictionary = _sprite_nodes[key]
		var object := _find_object(str(key))
		var compact_factor := 0.48 if compact else 1.0
		nodes["sprite"].scale = Vector2.ONE * _sprite_scale(str(object.get("sprite_kind", ""))) * compact_factor
		nodes["halo"].scale = Vector2.ONE * compact_factor
		nodes["label"].scale = Vector2.ONE * (0.85 if compact else 1.0)
	_layout_warp_markers()

func _place_ships(keys: Array[String], compact: bool) -> void:
	if keys.is_empty():
		return
	var x_positions := [0.12, 0.35, 0.58, 0.22, 0.45, 0.68] if compact else [0.16, 0.37, 0.58, 0.25, 0.49, 0.68]
	var y_positions := [0.72, 0.72, 0.72, 0.50, 0.50, 0.82] if compact else [0.72, 0.82, 0.68, 0.53, 0.54, 0.82]
	for index in keys.size():
		var nodes: Dictionary = _sprite_nodes.get(keys[index], {})
		if nodes.is_empty():
			continue
		var slot := index % x_positions.size()
		var row := index / x_positions.size()
		var x: float = x_positions[slot]
		var y: float = y_positions[slot] + float(row) * 0.08
		nodes["root"].position = Vector2(size.x * x, size.y * y)

func _rebuild_warp_markers(destinations: Variant) -> void:
	for marker in _warp_markers.values():
		if is_instance_valid(marker):
			marker.queue_free()
	_warp_markers.clear()
	if not (destinations is Array):
		return
	var sorted: Array = destinations.duplicate()
	sorted.sort()
	for destination in sorted:
		if typeof(destination) != TYPE_INT:
			continue
		var marker := Button.new()
		marker.text = "◇  %d" % destination
		marker.tooltip_text = "Adjacent sector %d. Marker placement is decorative." % destination
		marker.custom_minimum_size = Vector2(82, 34)
		marker.focus_mode = Control.FOCUS_ALL
		marker.add_theme_font_size_override("font_size", 12)
		marker.add_theme_color_override("font_color", Color(0.9, 0.92, 0.86))
		marker.add_theme_color_override("font_focus_color", Color(1.0, 0.85, 0.58))
		marker.add_theme_stylebox_override("normal", _marker_style(false))
		marker.add_theme_stylebox_override("hover", _marker_style(true))
		marker.add_theme_stylebox_override("focus", _marker_style(true))
		marker.mouse_entered.connect(_on_warp_hovered.bind(int(destination)))
		marker.mouse_exited.connect(_on_warp_unhovered.bind(int(destination)))
		marker.focus_entered.connect(func() -> void: _on_warp_hovered(int(destination)))
		marker.focus_exited.connect(func() -> void: _on_warp_unhovered(int(destination)))
		marker.pressed.connect(func() -> void: warp_selected.emit(int(destination), "artwork"))
		marker.gui_input.connect(_on_warp_marker_gui_input.bind(int(destination)))
		add_child(marker)
		_warp_markers[int(destination)] = marker
	_layout_warp_markers()
	_update_highlight()

func _on_warp_marker_gui_input(event: InputEvent, destination: int) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT and event.double_click:
		warp_activated.emit(destination)
		get_viewport().set_input_as_handled()

func _layout_warp_markers() -> void:
	if size.x <= 0:
		return
	var columns := maxi(1, int(size.x / 96.0))
	for destination in _warp_markers:
		var marker: Button = _warp_markers[destination]
		var index := _warp_markers.keys().find(destination)
		var col := index % columns
		var row := index / columns
		marker.position = Vector2(16 + col * 88, 15 + row * 39)

func _marker_style(active: bool) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.018, 0.05, 0.069, 0.94) if not active else Color(0.05, 0.15, 0.17, 0.98)
	style.border_color = Color(0.3, 0.58, 0.61, 0.88) if not active else Color(0.97, 0.73, 0.4, 1.0)
	style.set_border_width_all(1)
	style.set_corner_radius_all(14)
	style.content_margin_left = 10
	style.content_margin_right = 10
	return style

func _place_group(keys: Array[String], center: Vector2, step: Vector2) -> void:
	if keys.is_empty():
		return
	for index in keys.size():
		var object_nodes: Dictionary = _sprite_nodes.get(keys[index], {})
		if object_nodes.is_empty():
			continue
		var offset := Vector2.ZERO
		if index > 0:
			var ring := float((index + 1) / 2)
			var side := -1.0 if index % 2 == 1 else 1.0
			offset = Vector2(step.x * ring * side, step.y * ring * side)
		object_nodes["root"].position = Vector2(size.x * center.x, size.y * center.y) + Vector2(size.x * offset.x, size.y * offset.y)

func _update_highlight() -> void:
	for key in _sprite_nodes:
		var object_nodes: Dictionary = _sprite_nodes[key]
		var active: bool = str(key) == selected_key
		object_nodes["halo"].visible = active
		object_nodes["root"].z_index = 5 if active else 1
		object_nodes["label"].visible = active or str(key) == _hovered_key
	for destination in _warp_markers:
		var marker: Button = _warp_markers[destination]
		var active := selected_key == "warp:%d" % int(destination)
		marker.add_theme_stylebox_override("normal", _marker_style(active))

func _on_object_input(_viewport: Node, event: InputEvent, _shape_index: int, key: String) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index in [MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT]:
		select_object(key, "artwork")
		grab_focus()
		get_viewport().set_input_as_handled()
	elif event is InputEventScreenTouch and event.pressed:
		select_object(key, "artwork")
		grab_focus()
		get_viewport().set_input_as_handled()

func _input(event: InputEvent) -> void:
	# Area2D is the normal path, but a Control parent or a renderer/input
	# backend can prevent physics picking from reaching a child Node2D. Use the
	# same object geometry as a deterministic fallback hit test on the artwork
	# canvas itself; this is not a second UI button or an action shortcut.
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		var local_position: Vector2 = get_global_transform_with_canvas().affine_inverse() * event.position
		var candidate := _object_at_position(local_position)
		if not candidate.is_empty():
			select_object(str(candidate.get("key", "")), "artwork")
			grab_focus()
			get_viewport().set_input_as_handled()

func _object_at_position(position: Vector2) -> Dictionary:
	var best := {}
	var best_distance := INF
	for object in current_objects:
		var key := str(object.get("key", ""))
		var nodes: Dictionary = _sprite_nodes.get(key, {})
		if nodes.is_empty():
			continue
		var root: Node2D = nodes.get("root")
		if not is_instance_valid(root):
			continue
		var distance := position.distance_to(root.position)
		var radius := 108.0 if str(object.get("kind", "")) == "ship" else 122.0
		if distance <= radius and distance < best_distance:
			best = object
			best_distance = distance
	return best
func _on_object_hovered(key: String) -> void:
	_hovered_key = key
	_update_highlight()
	var object := _find_object(key)
	if not object.is_empty():
		var parent := get_parent()
		if parent and parent.has_method("show_hover_hint"):
			parent.show_hover_hint(str(object.get("kind", "Object")), str(object.get("name", "")))

func _on_object_unhovered(key: String) -> void:
	if _hovered_key == key:
		_hovered_key = ""
	_update_highlight()
	var parent := get_parent()
	if parent and parent.has_method("clear_hover_hint"):
		parent.clear_hover_hint(key)

func _on_warp_hovered(destination: int) -> void:
	var parent := get_parent()
	if parent and parent.has_method("show_hover_hint"):
		parent.show_hover_hint("warp", "Sector %d" % destination)

func _on_warp_unhovered(destination: int) -> void:
	var parent := get_parent()
	if parent and parent.has_method("clear_hover_hint"):
		parent.clear_hover_hint("warp:%d" % destination)

func _atlas_region(kind: String) -> AtlasTexture:
	var texture := AtlasTexture.new()
	texture.atlas = OBJECT_ATLAS
	var cell := Vector2(627, 627)
	var cell_position := Vector2.ZERO
	match kind:
		"port": cell_position = Vector2.ZERO
		"ship": cell_position = Vector2(627, 0)
		"ship_alt": cell_position = Vector2(0, 627)
		"planet": cell_position = Vector2(627, 627)
	texture.region = Rect2(cell_position, cell)
	return texture

func _sprite_scale(kind: String) -> float:
	match kind:
		"port": return 0.40
		"planet": return 0.39
		"ship": return 0.37
		"ship_alt": return 0.37
		_: return 0.19

func _find_object(key: String) -> Dictionary:
	for object in current_objects:
		if str(object.get("key", "")) == key:
			return object
	return {}

class SelectionHalo:
	extends Node2D

	func _draw() -> void:
		draw_arc(Vector2.ZERO, 94.0, 0.0, TAU, 72, Color(0.29, 0.91, 0.88, 0.95), 2.5, true)
		draw_arc(Vector2.ZERO, 101.0, -0.25, 0.45, 20, Color(0.96, 0.69, 0.35, 0.95), 1.5, true)

	func _process(_delta: float) -> void:
		queue_redraw()
