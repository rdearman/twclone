extends SceneTree

const AssetCatalog = preload("res://AssetCatalog.gd")
const PlanetArt = preload("res://PlanetArt.gd")
const SectorView = preload("res://SectorView.gd")

var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	_check(AssetCatalog.asset_id_for_object("planet", {"class": "K"}) == "planet.class.k", "class K did not resolve to its catalogue asset")
	_check(AssetCatalog.asset_id_for_object("planet", {"class": "L"}) == "planet.class.l", "class L did not resolve to its catalogue asset")
	_check(AssetCatalog.asset_id_for_object("planet", {"type": 2}) == "planet.class.l", "legacy planet type did not resolve through the catalogue mapping")
	_check(AssetCatalog.asset_id_for_object("port", {"type": 9}) == "location.stardock", "Stardock type did not resolve to its dedicated asset")
	_check(AssetCatalog.asset_id_for_object("port", {"type": 2}) == "location.port.station", "ordinary port did not resolve to the default port asset")
	_check(AssetCatalog.asset_id_for_object("ship", {"type_name": "Merchant Cruiser"}) == "ship.generic-hauler", "ship type name did not resolve to its catalogue asset")
	_check(AssetCatalog.asset_id_for_object("planet", {"asset_id": "planet.class.k", "class": "L"}) == "planet.class.k", "explicit server asset ID did not take precedence over inferred planet class")
	_check(AssetCatalog.asset_id_for_object("planet", {"class": "unmapped", "type": null, "metadata": [1, "new field"]}) == "", "unknown planet metadata resolved to unrelated artwork")

	var class_k := AssetCatalog.texture_for_object("planet", {"class": "K"})
	var class_l := AssetCatalog.texture_for_object("planet", {"class": "L"})
	_check(class_k is Texture2D and class_l is Texture2D, "class K or L artwork failed to load")
	if class_k is Texture2D and class_l is Texture2D:
		_check(class_k.get_image().get_data() != class_l.get_image().get_data(), "class K and L artwork rendered from identical image data")

	for asset_id in ["planet.class.k", "location.port.station", "ship.generic-hauler"]:
		for size in ["thumbnail", "standard", "large"]:
			_check(AssetCatalog.texture_for_id(asset_id, size) is Texture2D, "%s %s variant failed to load" % [asset_id, size])

	# Regression: newer server entity metadata may include explicit IDs and
	# extra fields; it must not crash rendering or make two classes reuse art.
	var sector_view := SectorView.new()
	root.add_child(sector_view)
	await process_frame
	sector_view.compose({
		"sector_id": 2271,
		"planets": [
			{"planet_id": 41, "name": "Explicit Art", "asset_id": "planet.class.k", "type": {"name": "new schema field"}, "metadata": {"revision": 2}},
			{"planet_id": 42, "name": "Class L", "class": "L", "type": 2.0, "new_field": ["ignored safely"]},
		],
		"ports": [{"id": 9, "type": 9, "asset_id": "location.stardock", "service_flags": ["new", "metadata"]}],
		"ships": [{"ship_id": 43, "type": {"name": "Merchant Cruiser"}, "new_field": true}],
		"adjacent_sector_ids": [],
	}, "")
	var explicit_nodes: Dictionary = sector_view._sprite_nodes.get("planet:41", {})
	var class_l_nodes: Dictionary = sector_view._sprite_nodes.get("planet:42", {})
	_check(not explicit_nodes.is_empty() and explicit_nodes["sprite"].texture is Texture2D, "planet with explicit asset metadata failed to render")
	_check(not class_l_nodes.is_empty() and class_l_nodes["sprite"].texture is Texture2D, "planet with legacy type metadata failed to render")
	if not explicit_nodes.is_empty() and not class_l_nodes.is_empty():
		_check(explicit_nodes["sprite"].texture.get_image().get_data() != class_l_nodes["sprite"].texture.get_image().get_data(), "explicit class K and legacy class L metadata rendered identical art")
	_check(PlanetArt.class_for_planet({"class": "unmapped", "type": null}) == "", "unknown class metadata was reinterpreted as a known class")
	sector_view.queue_free()
	await process_frame

	if failures.is_empty():
		print("Godot shared asset catalogue tests: 23 checks passed")
		quit(0)
	else:
		for failure in failures:
			push_error(failure)
		quit(1)

func _check(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)
