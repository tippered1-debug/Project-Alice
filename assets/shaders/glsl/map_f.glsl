in vec2 tex_coord;
in vec3 space_coords;
out vec4 frag_color;

uniform sampler2D provinces_texture_sampler;
uniform sampler2D terrain_texture_sampler;
uniform sampler2D terrainsheet_texture_sampler;
uniform sampler2DArray terrainsheet_texture_sampler_array;
uniform sampler2D water_normal;
uniform sampler2D colormap_water;
uniform sampler2D colormap_terrain;
uniform sampler2D overlay;
uniform sampler2DArray province_color;
uniform sampler2D colormap_political;
uniform sampler2D province_highlight;
uniform sampler2D stripes_texture;
uniform sampler2D province_fow;
uniform sampler2D provinces_sea_mask;
uniform usampler2D diag_border_identifier;
uniform sampler2D hatching;
uniform sampler2D watercolor;
uniform uint subroutines_index_2;
uniform uint graphics_mode;
// location 0 : offset
// location 1 : zoom
// location 2 : screen_size
uniform float zoom;
uniform vec2 map_size;
uniform vec2 screen_size;
uniform float time;
uniform float gamma;
uniform vec3 light_direction;
uniform float ignore_light;
uniform int terrain_is_array;
uniform int map_mode_is_data;
vec4 gamma_correct(vec4 colour) {
	return vec4(pow(colour.rgb, vec3(1.f / gamma)), colour.a);
}

// sheet is composed of 64 files, in 4 cubes of 4 rows of 4 columns
// so each column has 8 tiles, and each row has 8 tiles too
float xx = 1 / map_size.x;
float yy = 1 / map_size.y;
vec2 pix = vec2(xx, yy);

vec2 get_corrected_coords(vec2 coords) {
	coords.y -= (1 - 1 / 1.3) * 3 / 5;
	coords.y *= 1.3;
	return coords;
}

// The water effect
vec4 get_water_terrain()
{
	vec2 prov_id = texture(provinces_texture_sampler, gl_FragCoord.xy / screen_size).xy;

	// Water effect taken from Vic2 fx/water/PixelShader_HoiWater_2_0
	const float WRAP = 0.8;
	const float WaveModOne = 3.0;
	const float WaveModTwo = 4.0;
	const float SpecValueOne = 8.0;
	const float SpecValueTwo = 2.0;
	const float vWaterTransparens = 1.0; //more transparance lets you see more of background
	const float vColorMapFactor = 1.0f; //how much colormap

	vec2 tex_coord = tex_coord;
	vec2 corrected_coord = get_corrected_coords(tex_coord);
	vec3 WorldColorColor = texture(colormap_water, corrected_coord).rgb;
	if(corrected_coord.y > 1)
		WorldColorColor = vec3(0.14, 0.23, 0.36);
	if(corrected_coord.y < 0)
		WorldColorColor = vec3(0.20, 0.35, 0.43);
	tex_coord *= 100.;
	tex_coord = tex_coord * 0.25 + time * 0.002;

	const vec3 eyeDirection = vec3(0.0, 1.0, 1.0);
	const vec3 lightDirection = vec3(0.0, 1.0, 1.0);

	vec2 coordA = tex_coord * 3 + vec2(0.10, 0.10);
	vec2 coordB = tex_coord * 1 + vec2(0.00, 0.10);
	vec2 coordC = tex_coord * 2 + vec2(0.00, 0.15);
	vec2 coordD = tex_coord * 5 + vec2(0.00, 0.30);

	// Uses textureNoTile for non repeting textures,
	// probably unnecessarily expensive
	vec4 vBumpA = texture(water_normal, coordA);
	coordB += vec2(0.03, -0.02) * time;
	vec4 vBumpB = texture(water_normal, coordB);
	coordC += vec2(0.03, -0.01) * time;
	vec4 vBumpC = texture(water_normal, coordC);
	coordD += vec2(0.02, -0.01) * time;
	vec4 vBumpD = texture(water_normal, coordD);

	vec3 vBumpTex = normalize(
		WaveModOne * (
			vBumpA.xyz
			+ vBumpB.xyz
			+ vBumpC.xyz
			+ vBumpD.xyz
		) - WaveModTwo
	);

	vec3 eyeDir = normalize(eyeDirection);
	float NdotL = max(dot(eyeDir, (vBumpTex / 2)), 0);

	NdotL = clamp((NdotL + WRAP) / (1 + WRAP), 0.f, 1.f);
	NdotL = mix(NdotL, 1.0, 0.0);

	vec3 OutColor = NdotL * (WorldColorColor * vColorMapFactor);

	vec3	reflVector = -reflect(lightDirection, vBumpTex);
	float	specular = dot(normalize(reflVector), eyeDir);
	specular = clamp(specular, 0.0, 1.0);

	specular = pow(specular, SpecValueOne);
	OutColor += (specular / SpecValueTwo);
	OutColor *= 1.5;

	OutColor *= texture(province_fow, prov_id).rgb;

	return vec4(OutColor, vWaterTransparens);
}

vec4 get_water_political() {
	vec3 water_background = vec3(0.21, 0.38, 0.45);
	vec3 color = water_background.rgb;

	// The "foldable map" overlay effect
	vec4 OverlayColor = texture(overlay, tex_coord * vec2(11., 11.*map_size.y/map_size.x));
	vec4 OutColor;
	OutColor.r = OverlayColor.r < .5 ? (2. * OverlayColor.r * color.r) : (1. - 2. * (1. - OverlayColor.r) * (1. - color.r));
	OutColor.g = OverlayColor.r < .5 ? (2. * OverlayColor.g * color.g) : (1. - 2. * (1. - OverlayColor.g) * (1. - color.g));
	OutColor.b = OverlayColor.b < .5 ? (2. * OverlayColor.b * color.b) : (1. - 2. * (1. - OverlayColor.b) * (1. - color.b));
	OutColor.a = OverlayColor.a;

	return OutColor;
}

// The terrain color from the current texture coordinate offset with one pixel in the "corner" direction
vec4 get_terrain(vec2 corner, vec2 offset) {
	vec4 province_sample = texture(provinces_texture_sampler, gl_FragCoord.xy / screen_size);
	vec2 prov_id = province_sample.xy;
	float index = texture(terrain_texture_sampler, floor(tex_coord * map_size + vec2(0.5, 0.5)) / map_size + 0.5 * pix * corner).r;
	index = floor(index * 256);

	float is_water = 0.f; //step(64, index);

	if (texture(provinces_sea_mask, prov_id).x > 0.0f || (prov_id.x == 0.f && prov_id.y == 0.f)) {
		is_water = 1.f;
	}

	if (terrain_is_array == 0) {
		vec2 sample_from = (mod(offset, 1.f) * 255.f / 256.f + 1.f / 256.f / 2.f) / 8.f  + vec2(mod(index, 8.f), floor(index / 8.f)) / 8.f;
		vec4 colour = texture(terrainsheet_texture_sampler, vec2(sample_from.x, sample_from.y));
		return mix(colour, vec4(0.), is_water);
	} else {
		vec4 colour = texture(terrainsheet_texture_sampler_array, vec3(offset, index));
		return mix(colour, vec4(0.), is_water);
	}
}

vec4 get_terrain_mix() {
	if (int(graphics_mode) == 0) {
		return get_terrain(vec2(0.f, 0.f), vec2(0.f, 0.f));
	}

	// Pixel size on map texture
	vec2 scaling = fract(tex_coord * map_size + vec2(0.5, 0.5));

	vec2 offset = tex_coord / (16. * pix);

	vec4 colourlu = get_terrain(vec2(-1, -1), offset);
	vec4 colourld = get_terrain(vec2(-1, +1), offset);
	vec4 colourru = get_terrain(vec2(+1, -1), offset);
	vec4 colourrd = get_terrain(vec2(+1, +1), offset);

	// Mix together the terrains based on close they are to the current texture coordinate
	vec4 colour_u = mix(colourlu, colourru, scaling.x);
	vec4 colour_d = mix(colourld, colourrd, scaling.x);
	vec4 terrain = mix(colour_u, colour_d, scaling.y);

	// Mixes the terrains from "texturesheet.tga" with the "colormap.dds" background color.
	vec4 terrain_background = texture(colormap_terrain, get_corrected_coords(tex_coord));
	terrain.rgb = (terrain.rgb * 2. + terrain_background.rgb) / 3.;
	return terrain;
}

vec4 get_land_terrain() {
	return get_terrain_mix();
}

// if we display data, ignore pretty effects
vec4 get_land_data_modern() {
	vec4 terrain = get_terrain_mix();
	float is_land = terrain.a;
	vec4 province_sample = texture(provinces_texture_sampler, gl_FragCoord.xy / screen_size);
	vec2 prov_id = province_sample.xy;
	return vec4(texture(province_color, vec3(prov_id, 0.)).rgb * 0.8f, is_land);
}

vec4 get_land_political_modern() {
	vec4 terrain = get_terrain_mix();
	float is_land = terrain.a;
	const vec3 GREYIFY = vec3( 0.212671, 0.715160, 0.072169 );
	float grey = dot( terrain.rgb, GREYIFY );

	vec4 province_sample = texture(provinces_texture_sampler, gl_FragCoord.xy / screen_size);
	vec2 prov_id = province_sample.xy;
	vec4 prov_color = texture(province_color, vec3(prov_id, 0.));

	float to_national_border = smootherstep(sqrt(province_sample.z));
	
	vec4 stripe_color = texture(province_color, vec3(prov_id, 1.));
	vec2 stripe_coord = tex_coord * vec2(512., 512. * map_size.y / map_size.x);

	float stripeFactor = texture(stripes_texture, stripe_coord).a;
	float prov_highlight = texture(province_highlight, prov_id).r * (abs(cos(time * 3.f)) + 1.f);
	vec3 political = clamp(mix(prov_color, stripe_color, stripeFactor) + vec4(prov_highlight), 0.0, 1.0).rgb;

	vec4 OutColor = vec4(0.5f, 0.5f, 0.5f, is_land);

	float sum_of_prov_colors = dot(prov_color, vec4(1.f, 1.f, 1.f, 0.f));
	bool is_colonised = true;
	if (sum_of_prov_colors > 2.99f) {
		is_colonised = false;
	}

	if (is_colonised) {
		OutColor.rgb = mix((grey + terrain.rgb) / 2.f, political, 0.3f + 0.5f / zoom + (1.f - to_national_border) * 0.7f);
		OutColor.rgb -= (1.f - to_national_border) * 0.1f;
	} else {
		OutColor.rgb = (grey * 2.f + terrain.rgb) / 3.f + 0.2f;
	}

	OutColor.rgb *= texture(province_fow, prov_id).rgb * (to_national_border * 0.3f + 0.7f);

	return OutColor;
}


vec4 get_land_political_paper() {
	vec2 uv = tex_coord * 300.f;

	vec4 province_sample = texture(provinces_texture_sampler, gl_FragCoord.xy / screen_size);
	vec2 prov_id = province_sample.xy;	
	float to_national_border = province_sample.z;
	vec4 hatching = texture(hatching, uv / 2.f);

	vec4 prov_highlight = vec4(1.f, 1.f, 1.f, 1.f);
	float highlight = texture(province_highlight, prov_id).r;

	if (highlight > 0.f) {
		prov_highlight = vec4(hatching.rgb * 0.2f * (hatching.a) + 0.5f * (1.f - hatching.a), 1.f);
	}

	vec4 prov_color = texture(province_color, vec3(prov_id, 0.));

	vec4 base_color = vec4(1.f, 247.f / 255.f, 240.f / 255.f, 1.f);
	float wc_base = texture(watercolor, uv).b;
	float gradient = 1.f;
	if (int(graphics_mode) == 2) {
		gradient = (0.7f + sqrt(to_national_border) * 0.3f);
	}
	float wc_texture = max(0.f, wc_base * wc_base * wc_base) * gradient;
	vec4 mixed_color = prov_color * (1.f - wc_texture) + base_color * wc_texture;

	vec4 terrain = get_terrain_mix();
	const vec3 GREYIFY = vec3( 0.212671, 0.715160, 0.072169 );
	float grey = dot( terrain.rgb, GREYIFY ) * 2.f;
	float is_land = terrain.a;
	return mixed_color * prov_highlight * is_land + texture(watercolor, uv) * gradient * (1 - is_land);
}

vec4 get_land_political_close() {
	vec4 terrain = get_terrain_mix();
	float is_land = terrain.a;

	// Make the terrain a gray scale color
	const vec3 GREYIFY = vec3( 0.212671, 0.715160, 0.072169 );
	float grey = dot( terrain.rgb, GREYIFY );

	vec2 tex_coords = tex_coord;
	vec2 rounded_tex_coords = (floor(tex_coord * map_size) + vec2(0.5, 0.5)) / map_size;

	vec2 rel_coord = tex_coord * map_size - floor(tex_coord * map_size) - vec2(0.5);
	uint test = texture(diag_border_identifier, rounded_tex_coords).x;
	int shift = int(sign(rel_coord.x) + 2 * sign(rel_coord.y) + 3);

	rounded_tex_coords.y += ((int(test >> shift) & 1) != 0) && (abs(rel_coord.x) + abs(rel_coord.y) > 0.5) ? sign(rel_coord.y) / map_size.y : 0;

	vec4 province_sample = texture(provinces_texture_sampler, gl_FragCoord.xy / screen_size);
	vec2 prov_id = province_sample.xy;
	float to_national_border = province_sample.z;

	// The primary and secondary map mode province colors
	vec4 prov_color = texture(province_color, vec3(prov_id, 0.));

	float sum_of_prov_colors = dot(prov_color, vec4(1.f, 1.f, 1.f, 0.f));
	bool is_colonised = true;
	if (sum_of_prov_colors > 2.99f) {
		is_colonised = false;
	}

	vec4 stripe_color = texture(province_color, vec3(prov_id, 1.));

	vec2 stripe_coord = tex_coord * vec2(512., 512. * map_size.y / map_size.x);

	// Mix together the primary and secondary colors with the stripes
	float stripeFactor = texture(stripes_texture, stripe_coord).a;
	float prov_highlight = texture(province_highlight, prov_id).r * (abs(cos(time * 3.f)) + 1.f);
	vec3 political = clamp(mix(prov_color, stripe_color, stripeFactor) + vec4(prov_highlight), 0.0, 1.0).rgb;
	political *= texture(province_fow, prov_id).rgb;
	political = political - 0.7;

	// Mix together the terrain and map mode color
	terrain.rgb = mix(vec3(grey), political, 0.3f);
	terrain.rgb *= 1.5;

	return terrain;
}

vec4 get_land_political_far() {
	//vec4 terrain = get_terrain(vec2(0, 0), vec2(0));
	vec4 terrain = get_terrain_mix();
	const vec3 GREYIFY = vec3( 0.212671, 0.715160, 0.072169 );
	float grey = dot( terrain.rgb, GREYIFY );

	float is_land = terrain.a;

	vec4 province_sample = texture(provinces_texture_sampler, gl_FragCoord.xy / screen_size);
	vec2 prov_id = province_sample.xy;
	float to_national_border = province_sample.z;

	// The primary and secondary map mode province colors
	vec4 prov_color = texture(province_color, vec3(prov_id, 0.));
	vec4 stripe_color = texture(province_color, vec3(prov_id, 1.));

	float sum_of_prov_colors = dot(prov_color, vec4(1.f, 1.f, 1.f, 0.f));
	bool is_colonised = true;
	if (sum_of_prov_colors > 2.99f) {
		is_colonised = false;
	}

	vec2 stripe_coord = tex_coord * vec2(512., 512. * map_size.y / map_size.x);

	// Mix together the primary and secondary colors with the stripes
	float stripeFactor = texture(stripes_texture, stripe_coord).a;
	float prov_highlight = texture(province_highlight, prov_id).r * (abs(cos(time * 3.f)) + 1.f);
	vec3 political = clamp(mix(prov_color, stripe_color, stripeFactor) + vec4(prov_highlight), 0.0, 1.0).rgb;
	political = political - 0.7;

	// The "foldable map" overlay effect
	vec4 OverlayColor = texture(overlay, tex_coord * vec2(11., 11.*map_size.y/map_size.x));
	vec4 OutColor;
	OutColor.r = OverlayColor.r < .5 ? (2. * OverlayColor.r * political.r) : (1. - 2. * (1. - OverlayColor.r) * (1. - political.r));
	OutColor.g = OverlayColor.r < .5 ? (2. * OverlayColor.g * political.g) : (1. - 2. * (1. - OverlayColor.g) * (1. - political.g));
	OutColor.b = OverlayColor.b < .5 ? (2. * OverlayColor.b * political.b) : (1. - 2. * (1. - OverlayColor.b) * (1. - political.b));
	OutColor.a = OverlayColor.a;

	vec3 background = texture(colormap_political, get_corrected_coords(tex_coord)).rgb;

	if (int(graphics_mode) == 2) {
		if (map_mode_is_data == 0) {
			if (is_colonised) {
				OutColor.rgb = mix((grey + terrain.rgb) / 2.f, political + 0.7, 0.5f + (1.f - to_national_border) * 0.5f);
				OutColor.rgb *= 1.05;
			} else {
				OutColor.rgb = (grey * 2.f + terrain.rgb) / 3.f + 0.2f;
			}
		} else {
			vec3 political_raw = clamp(mix(prov_color, stripe_color, stripeFactor) + vec4(prov_highlight), 0.0, 1.0).rgb;
			OutColor.rgb = (political_raw * 0.9f) * 0.85f + grey * 0.15f;
		}
	} else {
		OutColor.rgb = mix(background, OutColor.rgb, 0.3);
		OutColor.rgb *= 1.5;
	}

	OutColor.a = is_land;

	return OutColor;
}

// KATECHON political cartography: the country colour carries the map, the
// terrain only models it, and the colour deepens towards a national border.
const vec3 LUMA = vec3(0.212671, 0.715160, 0.072169);

float hash21(vec2 p) {
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}
float value_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3. - 2. * f);
	return mix(mix(hash21(i), hash21(i + vec2(1., 0.)), f.x), mix(hash21(i + vec2(0., 1.)), hash21(i + vec2(1., 1.)), f.x), f.y);
}
// Ground in the manner of mountains: sharp crests, each scale half the last.
// The camera coming down adds finer scales, the last of them fading in, so
// that the smallest forms are always about a screen pixel or two across.
float ridges(vec2 p, float scales) {
	float height = 0.;
	float weight = 0.5;
	for(int i = 0; i < 8; ++i) {
		float present = clamp(scales - float(i), 0., 1.);
		if(present <= 0.)
			break;
		float n = 1. - abs(2. * value_noise(p) - 1.);
		height += weight * present * n * n;
		p = mat2(1.62, 1.18, -1.18, 1.62) * p + vec2(17.3, 9.1); // twice as fine, and turned, so no grain lines up
		weight *= 0.5;
	}
	return height;
}

vec4 get_land_katechon() {
	vec4 terrain = get_terrain_mix();
	float is_land = terrain.a;
	float relief = dot(terrain.rgb, LUMA);

	vec4 province_sample = texture(provinces_texture_sampler, gl_FragCoord.xy / screen_size);
	vec2 prov_id = province_sample.xy;
	float inside = smoothstep(0.0, 1.0, sqrt(province_sample.z)); // 0 on a national border, 1 away from it

	vec4 prov_color = texture(province_color, vec3(prov_id, 0.));
	vec4 stripe_color = texture(province_color, vec3(prov_id, 1.));
	vec2 stripe_coord = tex_coord * vec2(512., 512. * map_size.y / map_size.x);
	float stripe_factor = texture(stripes_texture, stripe_coord).a;
	vec3 political = mix(prov_color, stripe_color, stripe_factor).rgb;

	vec3 colour;
	if(dot(prov_color.rgb, vec3(1.)) > 2.99) {
		// nobody's land: bare, slightly warm ground
		colour = vec3(0.47, 0.46, 0.43) * (0.62 + 0.75 * relief);
	} else if(map_mode_is_data != 0) {
		// a data layer must stay comparable across the map: relief only as a faint grain
		colour = political * (0.93 + 0.16 * relief);
	} else {
		// The colour of a state is laid on as a pigment on paper, not as a
		// fresh fill: a little of it gives way to a warm grey, and the sheet
		// under it is warm too. Reds turn towards brick, blues go drier,
		// greens towards moss, yellows towards ochre; the hues keep apart.
		political = mix(political, vec3(dot(political, LUMA)) * vec3(1.06, 1.00, 0.86), 0.40);
		political = pow(political, vec3(1.10)) * vec3(0.95, 0.90, 0.80);
		vec4 ground = texture(colormap_terrain, get_corrected_coords(tex_coord));
		if(ground.a < 0.99) {
			// The scenario brings the shading of the real ground (alpha of the
			// terrain colormap, tools/katechon_2010/build_relief.py). Then the
			// land is modelled by that alone: the tiled terrain texture, which
			// grains plain and mountain alike, is left out but for a trace.
			// seen from far away the colours would sink a little
			colour = political * (0.95 + 0.06 * relief + 0.06 * clamp(2.2 / zoom - 0.3, 0., 1.));
			float lit = ground.a * (255. / 250.) * 2. - 1.;
			// The elevations behind this are no finer than the map itself, so
			// close to they can only give soft masses. Where the ground is
			// rugged (and only there) the camera coming down finds the smaller
			// forms: spurs and side valleys, drawn in the same light. Level
			// country gets none, so plains and deserts stay as calm as before.
			vec2 texel = 1.5 / vec2(textureSize(colormap_terrain, 0));
			vec2 at = get_corrected_coords(tex_coord);
			float rugged = (abs(lit)
				+ abs(texture(colormap_terrain, at + vec2(texel.x, 0.)).a * (255. / 250.) * 2. - 1.)
				+ abs(texture(colormap_terrain, at - vec2(texel.x, 0.)).a * (255. / 250.) * 2. - 1.)
				+ abs(texture(colormap_terrain, at + vec2(0., texel.y)).a * (255. / 250.) * 2. - 1.)
				+ abs(texture(colormap_terrain, at - vec2(0., texel.y)).a * (255. / 250.) * 2. - 1.)) * 0.2;
			float close_to = smoothstep(3.5, 11., zoom);
			if(rugged > 0.03 && close_to > 0.) {
				vec2 p = tex_coord * map_size / 3.6;
				// the light comes from the north-west; north is up the rows of the map
				const vec2 towards_light = vec2(-0.7071, 0.7071);
				float scales = clamp(2. + log2(max(zoom, 3.5) / 3.5), 2., 8.);
				float reach = 0.30 * exp2(1. - scales); // half the width of the finest form
				float spurs = (ridges(p + towards_light * reach, scales) - ridges(p - towards_light * reach, scales)) / (2. * reach) * 0.16;
				lit = clamp(lit * 1.25 + clamp(spurs, -1., 1.) * smoothstep(0.07, 0.36, rugged) * close_to, -1., 1.);
			}
			float strength = mix(1., 0.80, smoothstep(20., 45., zoom));
			// Shade and light belong to the pigment, not to a layer over it:
			// a slope in shade is the same colour deeper and a touch cooler,
			// a slope in the light the same colour warmed.
			float shade_in = max(-lit, 0.) * strength;
			float light_in = max(lit, 0.) * strength;
			colour *= mix(vec3(1.), vec3(0.50, 0.53, 0.60), shade_in);
			colour += colour * vec3(0.30, 0.27, 0.20) * light_in;
		} else {
			// seen from far away the relief averages out and the colours would sink
			float shade = 0.78 + 0.50 * relief + 0.08 * clamp(2.2 / zoom - 0.3, 0., 1.);
			colour = political * shade;
			colour = mix(colour, terrain.rgb * dot(political, LUMA) * 1.6, 0.10);
		}

		// the border band: deeper and more saturated, like a wash laid along the frontier
		vec3 band = mix(vec3(dot(colour, LUMA)), colour, 1.30) * 0.74;
		colour = mix(band, colour, inside);
	}

	float lit = texture(province_highlight, prov_id).r;
	colour = mix(colour, vec3(1.0, 0.88, 0.62), clamp(lit, 0., 1.) * (0.26 + 0.10 * abs(cos(time * 2.2))));
	// Fog of war only tones the political map down a little. At full strength it
	// made every foreign country darker than the player's own, and on a data
	// layer brightness is the data, so there it is not applied at all.
	if(map_mode_is_data == 0)
		colour *= mix(vec3(1.), texture(province_fow, prov_id).rgb, 0.18);
	return vec4(colour, is_land);
}

vec4 get_water_katechon() {
	vec2 corrected = get_corrected_coords(tex_coord);
	// The texture covers the province map; the game adds open water above and
	// below it. There the nearest charted row is read, half a texel of the mip
	// in use inside the edge, or the filter would wrap round to the other pole.
	vec2 chart_size = vec2(textureSize(colormap_water, 0));
	float mip = ceil(max(0., textureQueryLod(colormap_water, corrected).y));
	float margin = 0.5 * exp2(mip) / chart_size.y;
	vec4 chart = textureGrad(colormap_water, vec2(corrected.x, clamp(corrected.y, margin, 1. - margin)), dFdx(corrected), dFdy(corrected));
	float beyond = max(max(-corrected.y, corrected.y - 1.), 0.) * map_size.y / 1.3; // map pixels outside the chart
	vec3 colour;
	if(chart.a < 0.99) {
		// a sea chart (tools/katechon_2010/build_sea.py): the tint is the water
		// itself, alpha is the distance to the coast in map pixels
		colour = chart.rgb;
		float a = chart.a * (255. / 250.);
		float to_coast = a * a * 64.;
		if(beyond > 0.) {
			// Past the edge of the chart the water is the plain polar tone the
			// chart itself ends in; only hard against the edge does it take up
			// what the last charted row shows, so that nothing steps there.
			colour = mix(colour, vec3(0.114, 0.206, 0.262), smoothstep(0., 10., beyond));
			to_coast = 64.;
		}
		// the shoreline: a fine light rim, about a screen pixel and a half wide
		float pixel = screen_size.y / map_size.y * zoom;
		float rim = 1. - smoothstep(0., clamp(1.5 / pixel, 1.2, 4.), to_coast);
		colour += vec3(0.085, 0.100, 0.095) * rim;
		// and hard against the land a fine dark line, which is what makes a coast read as drawn
		float shore = 1. - smoothstep(0., clamp(0.55 / pixel, 0.45, 1.5), to_coast);
		colour = mix(colour, vec3(0.030, 0.050, 0.065), 0.42 * shore);
	} else {
		// any other water colormap: only its brightness is read, as a depth
		float depth = dot(chart.rgb, LUMA);
		colour = mix(vec3(0.085, 0.160, 0.225), vec3(0.165, 0.275, 0.335), smoothstep(0.12, 0.55, depth));
	}

	// the grain of the sheet: still, and too faint to read as waves
	vec2 grain_coord = tex_coord * vec2(40., 40. * map_size.y / map_size.x);
	float grain = texture(water_normal, grain_coord).g + texture(water_normal, grain_coord * 3.7 + vec2(0.37, 0.61)).g;
	colour *= 0.975 + 0.025 * grain;

	// unseen water is only a shade quieter: at full strength the fog blackens
	// the sea and draws the outline of every sea zone
	vec4 province_sample = texture(provinces_texture_sampler, gl_FragCoord.xy / screen_size);
	// the water outside the province map belongs to no sea zone: it takes the
	// shade of unseen water, or the edge of the chart would show as a step
	vec3 fog = (province_sample.x == 0. && province_sample.y == 0.) ? vec3(0.482) : texture(province_fow, province_sample.xy).rgb;
	colour *= mix(vec3(1.), fog, 0.16);
	return vec4(colour, 1.);
}

vec4 get_land() {
	if(subroutines_index_2 == 0) {
		return get_land_terrain();
	}
	if(int(graphics_mode) != 0) {
		return get_land_katechon();
	}

	if (int(graphics_mode) == 2) {
		if (map_mode_is_data == 0) {
			return get_land_political_modern();
		} else {
			return get_land_data_modern();
		}
		return get_land_political_far();
	}

	if (int(graphics_mode) == 0) {
		return get_land_political_far();
	}

	switch(int(subroutines_index_2)) {
	case 0: return get_land_terrain();
	case 1: return get_land_political_close();
	case 2: return get_land_political_far();
	default: break;
	}
}
vec4 get_water() {
	switch(int(subroutines_index_2)) {
	case 0: return get_water_terrain();
	case 1: return get_water_terrain();
	case 2: return get_water_political();
	default: break;
	}
	return vec4(1.f);
}

// The terrain map
// No province color is used here
void main() {
	if(subroutines_index_2 != 0 && int(graphics_mode) != 0) {
		vec4 land = get_land_katechon();
		vec4 sea = get_water_katechon();
		frag_color.rgb = mix(sea.rgb, land.rgb, land.a);
		if(ignore_light == 0.f) {
			frag_color.rgb *= max(0.f, dot(light_direction, space_coords));
			frag_color.rgb += vec3(0.02f, 0.03f, 0.06f);
		}
		frag_color.a = 1.f;
		frag_color = gamma_correct(frag_color);
		return;
	}

	vec4 terrain = get_land();

	vec4 water = vec4(0.5f, 0.5f, 0.8f, 1.f);
	if (int(graphics_mode) > 0) {
		water = get_water();
	}

	float light = max(0.f, dot(light_direction, space_coords));
	float darkness = max(0.f, -dot(light_direction, space_coords));

	if (int(graphics_mode) == 2) {
		vec4 province_sample = texture(provinces_texture_sampler, gl_FragCoord.xy / screen_size);
		float to_national_border = province_sample.z;
		//light += (1.f - to_national_border) * 1.5f * darkness;
		water.rgb = mix(water.rgb, vec3(0.0f, 0.0f, 0.0f), (1.f - to_national_border) * 0.5f);
	}
	
	vec2 uv = tex_coord * 300.f;
	vec4 hatching = texture(hatching, uv / 2.f);
	vec4 province_sample = texture(provinces_texture_sampler, gl_FragCoord.xy / screen_size);
	vec2 prov_id = province_sample.xy;	
	vec4 prov_highlight = vec4(1.f, 1.f, 1.f, 1.f);
	float highlight = texture(province_highlight, prov_id).r;
	if (highlight > 0.f) {
		prov_highlight = vec4(hatching.rgb * 0.2f * (hatching.a) + 0.5f * (1.f - hatching.a), 1.f);
	}

	frag_color.rgb = mix(water.rgb, terrain.rgb, terrain.a) * prov_highlight.rgb;
	if (ignore_light == 0.f) {
		float darkness = max(0.f, -dot(light_direction, space_coords));
		frag_color *= light;
		frag_color.xyz += vec3(0.05f, 0.05f, 0.2f);
	}
	frag_color.a = 1.f;
	frag_color = gamma_correct(frag_color);
}
