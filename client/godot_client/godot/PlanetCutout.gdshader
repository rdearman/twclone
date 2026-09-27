shader_type canvas_item;

void fragment() {
	COLOR = texture(TEXTURE, UV) * COLOR;
	float radius = length((UV - vec2(0.5)) / vec2(0.49));
	COLOR.a *= 1.0 - smoothstep(0.965, 1.0, radius);
}
