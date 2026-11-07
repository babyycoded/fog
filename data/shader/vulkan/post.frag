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

float Hash(vec2 p)
{
	p = fract(p * vec2(443.897, 441.423));
	p += dot(p, p.yx + 19.19);
	return fract((p.x + p.y) * p.x);
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
		vec2 Dir = (UV - 0.5) * Px * Chroma * 2.0;
		Col.r = SampleScene(UV + Dir).r;
		Col.g = SampleScene(UV).g;
		Col.b = SampleScene(UV - Dir).b;
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
		const int TAPS = 40;
		for(int i = 0; i < TAPS; i++)
		{
			float R = sqrt((float(i) + 0.5) / float(TAPS));
			float A = float(i) * 2.3999632 + Rotation;
			vec3 S = SampleScene(UV + vec2(cos(A), sin(A)) * R * gParams.gBloom.z * Px);
			float Lum = Luma(S);
			float Soft = clamp((Lum - Threshold + Knee) / (2.0 * Knee), 0.0, 1.0);
			float Weight = exp(-R * R * 2.0);
			Glow += S * Soft * Soft * Weight;
			WeightSum += Weight;
		}
		Col += Glow / WeightSum * gParams.gBloom.x * 2.0;
	}

	// exposure, contrast around mid grey, fade
	Col *= gParams.gColor.x;
	Col = (Col - 0.5) * gParams.gColor.y + 0.5;
	Col = mix(Col, Col * (1.0 - gParams.gGrade.w) + gParams.gGrade.w * 0.18, step(0.0001, gParams.gGrade.w));

	// white balance: warm/cool and green/magenta
	Col *= vec3(1.0 + gParams.gGrade.x * 0.10, 1.0 - gParams.gGrade.y * 0.08, 1.0 - gParams.gGrade.x * 0.10);

	// saturation and vibrance (vibrance lifts the dull colours more than the vivid ones)
	float L = Luma(Col);
	Col = mix(vec3(L), Col, gParams.gColor.z);
	float Sat = max(max(Col.r, Col.g), Col.b) - min(min(Col.r, Col.g), Col.b);
	Col = mix(vec3(L), Col, 1.0 + gParams.gGrade.z * (1.0 - Sat));

	// colour filter and the Fog haze (lifted, cooled, slightly desaturated distance)
	Col = mix(Col, Col * gParams.gFilter.rgb * 1.6, gParams.gFilter.a);
	if(gParams.gStyle.y > 0.0)
	{
		vec3 Haze = vec3(0.62, 0.68, 0.76);
		Col = mix(Col, mix(vec3(Luma(Col)), Col, 0.6) * 0.75 + Haze * 0.25, gParams.gStyle.y);
	}

	// gamma
	Col = pow(max(Col, vec3(0.0)), vec3(1.0 / max(gParams.gColor.w, 0.05)));

	// posterize
	if(gParams.gStyle.z >= 2.0)
		Col = floor(Col * gParams.gStyle.z + 0.5) / gParams.gStyle.z;

	// scanlines
	if(gParams.gStyle.x > 0.0)
		Col *= 1.0 - gParams.gStyle.x * (0.5 + 0.5 * cos(vUV.y / Px.y * 3.14159265));

	// vignette
	if(gParams.gLens.x > 0.0)
	{
		vec2 D = (vUV - 0.5) * vec2(Px.y / Px.x, 1.0);
		Col *= mix(1.0, smoothstep(0.95, 0.25, length(D) * 1.15), gParams.gLens.x);
	}

	// film grain
	if(gParams.gLens.z > 0.0)
		Col += (Hash(vUV / Px + fract(gParams.gScreen.z) * 913.0) - 0.5) * gParams.gLens.z * 0.25;

	Col = clamp(Col, 0.0, 1.0);

	// motion blur: blend with the previous output (frame-rate independent factor from the CPU)
	vec3 Prev = texture(gHistory, vUV).rgb;
	vec3 Out = mix(Col, Prev, gParams.gScreen.w);
	FragOut = vec4(Out, 1.0);
	HistoryOut = vec4(Out, 1.0);
}
