extends RefCounted

const CUTOUT_SHADER := preload("res://PlanetCutout.gdshader")
const AssetCatalog = preload("res://AssetCatalog.gd")

static func class_for_planet(data: Dictionary) -> String:
	var asset_id := AssetCatalog.asset_id_for_object("planet", data)
	var prefix := "planet.class."
	return asset_id.trim_prefix(prefix).to_upper() if asset_id.begins_with(prefix) else ""

static func texture_for_class(planet_class: String) -> Texture2D:
	var key := planet_class.strip_edges().to_lower()
	return AssetCatalog.texture_for_id("planet.class." + key, "large")

static func cutout_material(_planet_class: String = "M") -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = CUTOUT_SHADER
	return material
