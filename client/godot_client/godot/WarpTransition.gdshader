shader_type canvas_item;
render_mode unshaded;

uniform float phase : hint_range(0.0, 1.0) = 0.0;
uniform float strength : hint_range(0.0, 1.0) = 0.0;

float hash_value(float value) {
	return fract(sin(value * 127.1) * 43758.5453123);
}

void fragment() {
	vec2 centered = UV * 2.0 - 1.0;
	float radius = length(centered);
	float angle = atan(centered.y, centered.x);
	float ring_wave = 0.5 + 0.5 * sin((radius * 18.0 - phase * 9.0) * 3.14159);
	float rings = pow(ring_wave, 12.0) * smoothstep(0.04, 0.35, radius);
	float ray_index = floor((angle + 3.14159) * 42.0);
	float ray_noise = hash_value(ray_index);
	float rays = smoothstep(0.91, 0.99, ray_noise) * smoothstep(0.08, 0.48, radius);
	float edge = 1.0 - smoothstep(0.32, 1.35, radius);
	float center = 1.0 - smoothstep(0.0, 0.24, radius);
	vec3 blue = vec3(0.16, 0.52, 0.92);
	vec3 red = vec3(0.91, 0.24, 0.33);
	float red_shift = 0.5 + 0.5 * sin(angle + 0.7);
	vec3 tint = mix(blue, red, red_shift * 0.56);
	float detail = clamp(0.15 + rings * 0.55 + rays * (0.45 + ray_noise * 0.45), 0.0, 1.0);
	float alpha = strength * edge * min(0.94, detail + center * 0.48);
	vec3 color = mix(tint, vec3(0.008, 0.018, 0.035), center * 0.88);
	COLOR = vec4(color, alpha);
}
