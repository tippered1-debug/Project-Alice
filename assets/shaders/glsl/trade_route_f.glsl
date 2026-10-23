in float tex_coord;
in float o_dist;
in vec2 map_coord;
in float width_factor;
out vec4 frag_color;

uniform float gamma;
uniform vec2 screen_size;

uniform sampler2D provinces_texture_sampler;
uniform sampler2D province_fow;
uniform sampler2D line_texture;

vec4 gamma_correct(vec4 colour) {
	return vec4(pow(colour.rgb, vec3(1.f / gamma)), colour.a);
}

// The KATECHON trade lanes carry their kind in the texture coordinate across
// the line: 2..3 outgoing (gold), 4..5 incoming (azure), 6..7 the trade of
// other countries (faint ivory). They are drawn without the arrow texture: a
// soft glow, a thin core, and beads running from the start of the lane.
vec4 trade_lane(float across, float kind) {
	bool own = kind < 2.5;
	vec3 colour = kind < 1.5 ? vec3(0.98, 0.78, 0.44) : (kind < 2.5 ? vec3(0.62, 0.84, 0.98) : vec3(0.90, 0.86, 0.78));
	float edge = abs(across - 0.5) * 2.0;
	float glow = (1.0 - smoothstep(0.0, 1.0, edge)) * (own ? 0.38 : 0.10);
	float core = 1.0 - smoothstep(own ? 0.30 : 0.10, own ? 0.50 : 0.22, edge);
	float phase = fract(o_dist * 0.35);
	float bead = (1.0 - smoothstep(0.05, 0.16, abs(phase - 0.5))) * (1.0 - smoothstep(0.45, 0.75, edge));
	float alpha = own ? clamp(glow + core * 0.78 + bead, 0.0, 1.0) : clamp(glow + core * 0.30 + bead * 0.25, 0.0, 1.0) * 0.55;
	return vec4(colour * (1.0 + bead * (own ? 0.45 : 0.1)) * alpha, alpha);
}

// The KATECHON diplomacy ties, kinds 4..9: a thin line from one capital to
// another. 4 alliance (azure, solid), 5 war (red, with pulses), 6 protection
// (pale steel, long dashes), 7 rivalry, claims or hostility (rust, short
// dashes), 8 old friendship (jade, faint), 9 the relation under review (gold,
// dashed). Kinds 10..15 are the same ties stepped back: faint.
vec4 tie_line(float across, float kind) {
	float strength = 1.0;
	if(kind > 9.5) {
		kind -= 6.0;
		strength = 0.26;
	}
	if(kind > 8.5) {
		float edge9 = abs(across - 0.5) * 2.0;
		float core9 = 1.0 - smoothstep(0.30, 0.62, edge9);
		float glow9 = (1.0 - smoothstep(0.0, 1.0, edge9)) * 0.35;
		float dash9 = 1.0 - smoothstep(0.55, 0.62, fract(o_dist * 0.6));
		float alpha9 = clamp((glow9 + core9) * dash9, 0.0, 1.0);
		return vec4(vec3(0.98, 0.80, 0.44) * alpha9, alpha9);
	}
	vec3 colour = kind < 4.5 ? vec3(0.50, 0.74, 0.96)
		: (kind < 5.5 ? vec3(0.96, 0.38, 0.32)
		: (kind < 6.5 ? vec3(0.74, 0.86, 0.94)
		: (kind < 7.5 ? vec3(0.94, 0.56, 0.40) : vec3(0.56, 0.86, 0.74))));
	float edge = abs(across - 0.5) * 2.0;
	float core = 1.0 - smoothstep(0.30, 0.62, edge);
	float glow = (1.0 - smoothstep(0.0, 1.0, edge)) * 0.28;
	float dash = 1.0;
	if(kind > 5.5 && kind < 6.5)
		dash = 1.0 - smoothstep(0.62, 0.70, fract(o_dist * 0.45));
	else if(kind > 6.5 && kind < 7.5)
		dash = 1.0 - smoothstep(0.48, 0.56, fract(o_dist * 0.9));
	float pulse = (kind > 4.5 && kind < 5.5) ? (1.0 - smoothstep(0.04, 0.18, abs(fract(o_dist * 0.3) - 0.5))) * (1.0 - smoothstep(0.5, 0.9, edge)) : 0.0;
	strength *= kind > 7.5 ? 0.62 : 1.0;
	float alpha = clamp((glow + core * 0.92) * dash + pulse * 0.7, 0.0, 1.0) * strength;
	return vec4(colour * (1.0 + pulse * 0.5) * alpha, alpha);
}

void main() {
	float across = tex_coord / width_factor;
	if(across > 1.5) {
		float kind = floor(across * 0.5);
		if(kind > 3.5) {
			frag_color = gamma_correct(tie_line(across - 2.0 * kind, kind));
			return;
		}
		frag_color = gamma_correct(trade_lane(across - 2.0 * kind, kind));
		return;
	}
	vec4 out_color = texture(line_texture, vec2(o_dist, across));
	out_color.rgb *= out_color.a;
	frag_color = gamma_correct(out_color);
}
