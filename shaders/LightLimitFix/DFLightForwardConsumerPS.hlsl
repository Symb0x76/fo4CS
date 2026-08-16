// Visible DFLight forward LLF consumer pixel shader (first cut).
//
// Vanilla DFLight forward (descriptors 0x204/0x8004) is a screen-space
// per-light pass: SV_POSITION only, reconstructs view-space position from t3
// depth, samples GBuffer t0 (diffuse occlusion), t1 (normal), t2
// (specular/gloss) and accumulates one point light into o0/o1.
//
// This consumer REPLACES the per-light evaluation with the LLF clustered
// light list (t35/t36/t37). The CPU binds it to the FIRST point-light pass of
// each frame and binds DFLightZeroOutputPS to the remaining point-light
// passes, so the clustered list is emitted exactly once and vanilla per-light
// passes no longer double-count.
//
// cb3 (LLF-owned, 96 bytes):
//   cb3[0..3] = inverse view matrix (view -> world), rows
//   cb3[4].xy  = camera near / far
//
// Output format mirrors the vanilla pass: o0.xyz = accumulated diffuse / 3,
// o0.w = 0, o1.xyz = accumulated specular, o1.w = 1.

// The DFLight forward consumer replaces the vanilla per-light passes 0x204 /
// 0x8004 directly. Those passes light shadow-flagged lights too (the decoded
// wrappers all carry Shadow mask bits), so the BSLighting-style shadow skip
// must NOT apply here — otherwise every light in the list is skipped and the
// cluster evaluation is empty.
#define LLF_DFLIGHT_FORWARD_CONSUMER 1
#include "LightLimitFix/LightLimitFix.hlsli"

#ifndef FO4CS_SHADER_DESCRIPTOR
#define FO4CS_SHADER_DESCRIPTOR 0
#endif

Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);

cbuffer cb12 : register(b12)
{
	float4 cb12[30];
}

cbuffer cb2 : register(b2)
{
	float4 cb2[23];
}

cbuffer cb3 : register(b3)
{
	float4x4 InvView;   // reserved (no longer used by the view-space consumer)
	float4 ClusterParams; // x = cameraNear, y = cameraFar
}

void main(
	float4 v0 : SV_POSITION0,
	out float4 o0 : SV_Target0,
	out float4 o1 : SV_Target1)
{
	// ---- vanilla position reconstruction (verbatim 0x204/0x8004) ----
	float2 uv = v0.xy * cb2[22].xy;
	uv = uv * cb2[0].xy;
	float ndcX = v0.x * cb2[0].x;
	float depth = t3.Sample(s3, uv).r;

	float4 row0, row1, row2, row3;
	float linearDepth;
	if (depth <= 0.01f) {
		row0 = cb12[24];
		row1 = cb12[25];
		row2 = cb12[26];
		row3 = cb12[27];
		linearDepth = depth * 100.0f;
	} else {
		row0 = cb12[20];
		row1 = cb12[21];
		row2 = cb12[22];
		row3 = cb12[23];
		linearDepth = depth * 1.01f - 0.01f;
	}

	float ndcY = -v0.y * cb2[0].y + 1.0f;
	float2 ndc = float2(ndcX, ndcY) * 2.0f - 1.0f;
	float4 ndcPos = float4(ndc, linearDepth, 1.0f);
	float3 viewPos;
	viewPos.x = dot(row0, ndcPos);
	viewPos.y = dot(row1, ndcPos);
	viewPos.z = dot(row2, ndcPos);
	float w = dot(row3, ndcPos);
	viewPos = viewPos / w;

	// ---- vanilla GBuffer decode (verbatim) ----
	float3 normal;
	normal.xy = t1.Sample(s1, uv).xy;
	normal.xy = normal.xy * float2(4.0f, 4.0f) + float2(-2.0f, -2.0f);
	float nDot = dot(normal.xy, normal.xy);
	float2 normalFix = -nDot * float2(0.25f, 0.5f) + float2(1.0f, 1.0f);
	float nz = sqrt(normalFix.x);
	normal.xy = normal.xy * nz;
	normal.z = -normalFix.y;

	float4 specRaw = t2.Sample(s2, uv);
	float diffuseOcclusion = t0.Sample(s0, uv).w;

	// ---- LLF clustered lights (replaces vanilla per-light pass) ----
	// The consumer runs entirely in VIEW space, exactly like vanilla DFLight:
	// the reconstructed position, GBuffer normal, and each Light's
	// positionWS[1] (CPU-filled view-space slot) all share one frame. No
	// per-pixel world transform is involved.
	float3 viewDir = normalize(-viewPos);

	uint clusterOffset = 0;
	uint clusterCount = 0;
	const uint3 clusterSize = uint3(8, 8, 16);
	const float2 clusterUV = saturate(v0.xy * cb2[0].xy);
	const float viewZ = max(viewPos.z, 0.001f);

	float3 diffuseSum = 0.0f;
	float3 specularSum = 0.0f;

	if (LightLimitFix::TryGetCluster(
			clusterUV,
			viewZ,
			clusterSize,
			max(ClusterParams.x, 0.001f),
			max(ClusterParams.y, 1.0f),
			clusterOffset,
			clusterCount)) {
		float gloss = exp2(specRaw.x * 10.0f + 1.0f);
		float specMask = specRaw.w;

		uint li = 0;
		while (li < clusterCount) {
			LightLimitFix::Light light = (LightLimitFix::Light)0;
			if (!LightLimitFix::GetClusteredLight(li, clusterOffset, light)) {
				li = li + 1;
				continue;
			}

			float3 lightColor = light.color * saturate(light.fade);

			// View-space light position (CPU-filled positionWS[1]).
			float3 toLight = light.positionWS[1].xyz - viewPos;
			float dist = length(toLight);
			float3 lightDir = toLight / max(dist, 1e-5f);

			float atten = saturate(dist * max(light.invRadius, 0.0f));
			atten = 1.0f - atten * atten;
			atten = max(atten, 0.0f);
			atten = pow(atten, 2.2f);

			float nDotL = saturate(dot(normal, lightDir));
			float rim = 1.0f - saturate(dot(normal, viewDir));
			rim = rim * rim;
			float diffuseFactor = nDotL * (diffuseOcclusion + rim * 0.25f);
			diffuseSum += lightColor * (atten * diffuseFactor);

			float3 halfDir = normalize(lightDir + viewDir);
			float nDotH = saturate(dot(normal, halfDir));
			float spec = pow(nDotH, gloss);
			specularSum += lightColor * (spec * specMask * nDotL * atten);

			li = li + 1;
		}
	}

	// ---- output (vanilla DFLight dual-target format) ----
	float4 out0 = float4(diffuseSum, 0.0f);
	out0 = out0 / 3.0f;
	o0 = out0;
	o1 = float4(specularSum, 1.0f);
}
