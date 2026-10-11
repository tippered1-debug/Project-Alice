in float tex_coord;
in float o_dist;
in vec2 map_coord;
in vec2 space_coord;
out vec4 frag_color;
in float border_width_coord;

uniform float gamma;
uniform vec2 screen_size;
uniform vec2 map_size;

uniform sampler2D provinces_texture_sampler;
uniform sampler2D province_fow;
uniform sampler2D line_texture;
uniform sampler2D printbrush;

vec4 gamma_correct(vec4 colour) {
	return vec4(pow(colour.rgb, vec3(1.f / gamma)), colour.a);
}

uniform float width;

// Borders are drawn as clean ink. The line texture only gives the profile
// across the line; the kind of border is told apart by the width it is drawn
// with: frontiers are firm, internal divisions faint, the outline of a
// selection is gold.
void main() {
	float profile = texture(line_texture, vec2(o_dist, border_width_coord)).a;
	vec4 out_color;
	if(abs(width - 0.0004) < 0.00003 || abs(width - 0.00085) < 0.00003) {
		out_color = vec4(1.0, 0.84, 0.50, profile * 0.95);
	} else if(width > 0.00012) {
		out_color = vec4(0.035, 0.040, 0.055, profile * 0.92);
	} else {
		out_color = vec4(0.035, 0.040, 0.055, profile * 0.34);
	}
	out_color.rgb *= out_color.a;
	frag_color = gamma_correct(out_color);
}
