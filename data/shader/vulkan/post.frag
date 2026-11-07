#version 450
#extension GL_ARB_separate_shader_objects : enable

// Fog client: live post-process. Everything is driven by push constants so every
// effect can change per frame without rebuilding anything.
layout(set = 0, binding = 0) uniform sampler2D gScene;
layout(set = 1, binding = 0) uniform sampler2D gHistory;

layout(push_constant) uniform SPostParams
{
	vec4 gScreen; // 1/w, 1/h, time, history blend (motion blur)
	vec4 gBloom; // strength, threshold, radius in px, knee
	vec4 gColor; // exposure, contrast, saturation, gamma
	vec4 gGrade; // temperature, tint, vibrance, fade (lifted blacks)
	vec4 gLens; // vignette, chromatic aberration px, grain, sharpen
	vec4 gStyle; // scanlines, fog haze, posterize levels (0 off), pixelate px (0 off)
	vec4 gFilter; // colour filter rgb, amount
} gParams;

layout(location = 0) noperspective in vec2 vUV;
layout(location = 0) out vec4 FragOut;
layout(location = 1) out vec4 HistoryOut;

float Luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// integer hash (no visible grid or diagonal patterns, unlike the usual fract tricks)
float Hash(vec2 p)
{
	uvec2 q = uvec2(ivec2(p)) * uvec2(1597334673u, 3812015801u);
	uint n = (q.x ^ q.y) * 1597334673u;
	return float(n) * (1.0 / 4294967295.0);
}

// 4x4 ordered dither in [-0.5, 0.5)
float Bayer(vec2 FragCoord)
{
	const float M[16] = float[](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
	ivec2 i = ivec2(mod(FragCoord, 4.0));
	return M[i.y * 4 + i.x] / 16.0 - 0.5;
}

vec3 SampleScene(vec2 uv)
{
	return texture(gScene, uv).rgb;
}

void main()
{
	vec2 Px = gParams.gScreen.xy;
	vec2 UV = vUV;

	// pixelate: snap to blocks before anything samples the frame
	if(gParams.gStyle.w > 1.0)
	{
		vec2 Block = Px * gParams.gStyle.w;
		UV = (floor(UV / Block) + 0.5) * Block;
	}

	// chromatic aberration grows towards the edges
	vec3 Col;
	float Chroma = gParams.gLens.y;
	if(Chroma > 0.0)
	{
		// the split grows with the distance from the centre (squared, like a real lens) and is
		// kept inside the frame so the edge does not smear into a coloured line
		vec2 Off = UV - 0.5;
		vec2 Dir = Off * dot(Off, Off) * 4.0 * Px * Chroma * 3.5;
		vec2 Lo = Px * 0.5, Hi = 1.0 - Px * 0.5;
		Col.r = SampleScene(clamp(UV - Dir, Lo, Hi)).r;
		Col.g = SampleScene(UV).g;
		Col.b = SampleScene(clamp(UV + Dir, Lo, Hi)).b;
	}
	else
		Col = SampleScene(UV);

	// sharpen: unsharp mask against the 4 neighbours
	if(gParams.gLens.w > 0.0)
	{
		vec3 Blur = (SampleScene(UV + vec2(Px.x, 0.0)) + SampleScene(UV - vec2(Px.x, 0.0)) + SampleScene(UV + vec2(0.0, Px.y)) + SampleScene(UV - vec2(0.0, Px.y))) * 0.25;
		Col += (Col - Blur) * gParams.gLens.w;
	}

	// bloom: soft-knee bright pass gathered over a Vogel disk; every pixel rotates the disk
	// by interleaved gradient noise, so the glow is smooth instead of showing copies
	if(gParams.gBloom.x > 0.0)
	{
		vec3 Glow = vec3(0.0);
		float WeightSum = 0.0;
		float Threshold = gParams.gBloom.y, Knee = max(gParams.gBloom.w, 0.001);
		float Rotation = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
		const int TAPS = 64;
		for(int i = 0; i < TAPS; i++)
		{
			float R = sqrt((float(i) + 0.5) / float(TAPS));
			float A = float(i) * 2.3999632 + Rotation;
			vec3 S = SampleScene(UV + vec2(cos(A), sin(A)) * R * gParams.gBloom.z * Px);
			float Lum = Luma(S);
			float Soft = clamp((Lum - Threshold + Knee) / (2.0 * Knee), 0.0, 1.0);
			float Weight = exp(-R * R * 3.0);
			Glow += S * Soft * Soft * Weight;
			WeightSum += Weight;
		}
		// screen blend: the glow brightens without burning bright parts into flat white blobs
		vec3 G = clamp(Glow / WeightSum * gParams.gBloom.x * 1.4, 0.0, 1.0);
		Col = 1.0 - (1.0 - clamp(Col, 0.0, 1.0)) * (1.0 - G);
	}

	// exposure, contrast around mid grey, fade
	Col *= gParams.gColor.x;
	// contrast as a curve around a darker pivot: dark scenes keep their detail instead of being
	// crushed to black, and black stays black
	const float PIVOT = 0.4;
	Col = PIVOT * pow(max(Col, vec3(0.0)) / PIVOT, vec3(gParams.gColor.y));
	// faded film: blacks lift towards a soft grey, whites stay white
	Col = gParams.gGrade.w * 0.22 + Col * (1.0 - gParams.gGrade.w * 0.22);

	// white balance: warm/cool and green/magenta
	// warm = more red and a little green (amber), cool = more blue; tint moves green/magenta
	float T = gParams.gGrade.x;
	Col *= vec3(1.0 + T * 0.12, 1.0 + T * 0.035 - gParams.gGrade.y * 0.08, 1.0 - T * 0.12);

	// saturation and vibrance (vibrance lifts the dull colours more than the vivid ones)
	float L = Luma(Col);
	Col = mix(vec3(L), Col, gParams.gColor.z);
	float Sat = max(max(Col.r, Col.g), Col.b) - min(min(Col.r, Col.g), Col.b);
	Col = mix(vec3(L), Col, 1.0 + gParams.gGrade.z * (1.0 - Sat));

	// colour filter and the Fog haze (lifted, cooled, slightly desaturated distance)
	// colour filter: a tint that keeps the brightness (the filter colour is normalised by its luma)
	vec3 Filter = gParams.gFilter.rgb / max(Luma(gParams.gFilter.rgb), 0.05);
	Col = mix(Col, Col * Filter, gParams.gFilter.a);
	if(gParams.gStyle.y > 0.0)
	{
		vec3 Haze = vec3(0.62, 0.68, 0.76);
		Col = mix(Col, mix(vec3(Luma(Col)), Col, 0.6) * 0.75 + Haze * 0.25, gParams.gStyle.y);
	}

	// gamma
	Col = pow(max(Col, vec3(0.0)), vec3(1.0 / max(gParams.gColor.w, 0.05)));

	// posterize
	if(gParams.gStyle.z >= 2.0)
	{
		// quantise the brightness, not each channel (channels rounding apart turned greys into
		// purple and teal blobs), with an ordered dither so steps look printed, not broken
		float Levels = gParams.gStyle.z;
		float Lum = Luma(Col);
		float Q = floor(Lum * Levels + 0.5 + Bayer(gl_FragCoord.xy) * 0.85) / Levels;
		Col *= Q / max(Lum, 0.001);
	}

	// scanlines
	if(gParams.gStyle.x > 0.0)
	{
		// about 360 soft lines whatever the resolution, and the lost light given back so the
		// picture does not just get darker
		float Lines = 360.0;
		float Wave = 0.5 + 0.5 * cos(vUV.y * Lines * 6.2831853);
		float Dark = gParams.gStyle.x * 0.55;
		Col *= (1.0 - Dark * Wave) / (1.0 - Dark * 0.5);
	}

	// vignette
	if(gParams.gLens.x > 0.0)
	{
		vec2 D = (vUV - 0.5) * vec2(Px.y / Px.x, 1.0);
		Col *= mix(1.0, smoothstep(0.95, 0.25, length(D) * 1.15), gParams.gLens.x);
	}

	// film grain
	if(gParams.gLens.z > 0.0)
	{
		// film grain changes 24 times a second (not every frame: at high frame rates that is TV
		// static), sits mostly in the midtones and is soft
		float Frame = floor(gParams.gScreen.z * 24.0);
		vec2 Cell = floor(vUV / Px / 1.5);
		float N = (Hash(Cell + Frame * 37.0) + Hash(Cell * 1.7 + Frame * 13.0)) * 0.5 - 0.5;
		float Mid = 1.0 - abs(Luma(Col) - 0.5) * 1.6;
		Col += N * gParams.gLens.z * 0.22 * max(Mid, 0.25);
	}

	Col = clamp(Col, 0.0, 1.0);

	// motion blur: blend with the previous output (frame-rate independent factor from the CPU)
	vec3 Prev = texture(gHistory, vUV).rgb;
	vec3 Out = mix(Col, Prev, gParams.gScreen.w);
	FragOut = vec4(Out, 1.0);
	HistoryOut = vec4(Out, 1.0);
}
