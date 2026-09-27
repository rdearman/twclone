extends RefCounted

const ATLAS_PATH := "res://assets/planet_classes_atlas.png"
const CUTOUT_SHADER := preload("res://PlanetCutout.gdshader")
const CELL := 384
static var _atlas_image: Image
static var _atlas_texture: Texture2D
static var _class_textures: Dictionary = {}

static func _cell_for_class(planet_class: String) -> Vector2i:
	var cells := {
		"M": Vector2i(0, 0), # Earth
		"K": Vector2i(1, 0), # Desert
		"O": Vector2i(2, 0), # Ocean
		"L": Vector2i(3, 0), # Mountain
		"H": Vector2i(0, 1), # Volcano
		"U": Vector2i(1, 1), # Gas giant
		"C": Vector2i(2, 1), # Ice world
	}
	return cells.get(planet_class.to_upper(), Vector2i.ZERO)

static func atlas_image() -> Image:
	if _atlas_image == null:
		# Load through Godot's resource system so exported projects use the
		# imported texture instead of trying to open the source PNG as a file.
		_atlas_texture = load(ATLAS_PATH) as Texture2D
		if _atlas_texture == null:
			push_error("Unable to load planet art atlas: %s" % ATLAS_PATH)
			return null
		_atlas_image = _atlas_texture.get_image()
	return _atlas_image

static func region_for_class(planet_class: String) -> Rect2i:
	var cell := _cell_for_class(planet_class)
	return Rect2i(Vector2i(cell.x * CELL, 64 + cell.y * 512), Vector2i(CELL, CELL))

static func class_for_planet(data: Dictionary) -> String:
	var code := str(data.get("class", "")).strip_edges().to_upper()
	if ["M", "K", "O", "L", "H", "U", "C"].has(code):
		return code
	var type_name := str(data.get("type_name", "")).to_lower()
	if type_name.contains("mountain"):
		return "L"
	if type_name.contains("desert"):
		return "K"
	if type_name.contains("ocean"):
		return "O"
	if type_name.contains("volcan"):
		return "H"
	if type_name.contains("gas") or type_name.contains("gaseous"):
		return "U"
	if type_name.contains("ice") or type_name.contains("glacial"):
		return "C"
	if type_name.contains("earth"):
		return "M"
	# Compatibility with older sector packets that expose only the FK. These
	# IDs follow the planettypes seed order: M, L, O, K, H, U, C.
	match int(data.get("type", 0)):
		1: return "M"
		2: return "L"
		3: return "O"
		4: return "K"
		5: return "H"
		6: return "U"
		7: return "C"
	return ""

static func texture_for_class(planet_class: String) -> Texture2D:
	var key := planet_class.strip_edges().to_upper()
	if _class_textures.has(key):
		return _class_textures[key]
	var image := atlas_image()
	if image == null:
		return null
	var class_image := image.get_region(region_for_class(key))
	var texture := ImageTexture.create_from_image(class_image)
	_class_textures[key] = texture
	return texture

static func cutout_material(_planet_class: String = "M") -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = CUTOUT_SHADER
	return material
