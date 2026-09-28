extends RefCounted

## Client adapter for the repository-wide artwork catalogue.
## Artwork paths and game-object mappings live in assets/catalog.json.
const CATALOG_PATH := "res://assets/catalog.json"

static var _catalog: Dictionary = {}
static var _assets_by_id: Dictionary = {}

static func asset_id_for_object(kind: String, data: Dictionary) -> String:
	if _load_catalog().is_empty():
		return ""
	var explicit_id := str(data.get("asset_id", "")).strip_edges()
	if _assets_by_id.has(explicit_id):
		return explicit_id
	var mappings: Dictionary = _catalog.get("object_mappings", {})
	var kind_mapping: Dictionary = mappings.get(kind.to_lower(), {})
	match kind.to_lower():
		"port":
			var type_id := str(data.get("type_id", data.get("type", "")))
			return str(kind_mapping.get("by_type_id", {}).get(type_id, kind_mapping.get("default", "")))
		"planet":
			var code := str(data.get("class", "")).strip_edges().to_upper()
			var by_class: Dictionary = kind_mapping.get("by_class", {})
			if by_class.has(code):
				return str(by_class[code])
			var type_id := str(data.get("type_id", data.get("type", "")))
			return str(kind_mapping.get("by_type_id", {}).get(type_id, ""))
		"ship":
			var type_name := str(data.get("type_name", data.get("ship_type", ""))).strip_edges().to_lower()
			if type_name.is_empty() and data.get("type") is Dictionary:
				type_name = str(data["type"].get("name", "")).strip_edges().to_lower()
			return str(kind_mapping.get("by_type_name", {}).get(type_name, kind_mapping.get("default", "")))
	return ""

static func texture_for_object(kind: String, data: Dictionary, size: String = "large") -> Texture2D:
	return texture_for_id(asset_id_for_object(kind, data), size)

static func texture_for_id(asset_id: String, size: String = "large") -> Texture2D:
	var assets := _load_catalog()
	if assets.is_empty():
		return null
	var entry: Dictionary = _assets_by_id.get(asset_id, {})
	if entry.is_empty():
		push_warning("Artwork asset ID is not in the canonical catalogue: %s" % asset_id)
		return null
	var paths: Dictionary = entry.get("paths", {})
	var selected_size := size if paths.has(size) else "standard"
	var path := str(paths.get(selected_size, ""))
	if path.is_empty():
		return null
	var resource_path := "res://" + path.trim_prefix("/")
	var resource := ResourceLoader.load(resource_path)
	if resource is Texture2D:
		return resource
	push_warning("Catalogue path is not a Godot texture resource: %s" % resource_path)
	return null

static func _load_catalog() -> Dictionary:
	if not _catalog.is_empty():
		return _catalog
	if not FileAccess.file_exists(CATALOG_PATH):
		push_error("Canonical artwork catalogue is missing: %s" % CATALOG_PATH)
		return {}
	var file := FileAccess.open(CATALOG_PATH, FileAccess.READ)
	if file == null:
		push_error("Unable to read canonical artwork catalogue: %s" % CATALOG_PATH)
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	if not parsed is Dictionary or not parsed.get("assets", []) is Array:
		push_error("Canonical artwork catalogue has an invalid structure")
		return {}
	_catalog = parsed
	for entry in _catalog.get("assets", []):
		if entry is Dictionary:
			var asset_id := str(entry.get("asset_id", ""))
			if not asset_id.is_empty():
				_assets_by_id[asset_id] = entry
	return _catalog
