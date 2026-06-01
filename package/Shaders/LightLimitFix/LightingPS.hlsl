#include "LightLimitFix/LightLimitFix.hlsli"

// Experimental clustered-forward lighting pixel shader template.
// This is not the active LLF implementation path and must not be deployed by
// PS hash guessing or by enabling the packaged ShaderDB placeholder. FO4 LLF
// follows the Skyrim CS engine-lighting path first: ShadowSceneNode clustered
// prepass, point-light hook scene-light collection, strict CB proof, Skyrim-style
// t35-t37 resource binding, then verified lighting-shader integration.
// PreNG does not currently route an active shader through this template.

// --- Vanilla-compatible inputs (adjust based on actual FO4 PS signature) ---
struct PixelIn
{
	float4 position : SV_Position;
	float3 worldPos : TEXCOORD0;
	float3 normal   : TEXCOORD1;
	float2 texCoord : TEXCOORD2;
};

cbuffer LightLimitFixSupportData : register(b0)
{
	uint EnableVisualisation;
	uint VisualisationMode;
	float CameraNear;
	float CameraFar;
	uint4 ClusterSize;
}

float GetLLFCameraNear()
{
	return CameraNear > 0.0f ? CameraNear : 0.1f;
}

float GetLLFCameraFar()
{
	const float cameraNear = GetLLFCameraNear();
	return CameraFar > cameraNear ? CameraFar : 10000.0f;
}

float3 EvaluateLight(LightLimitFix::Light light, float3 worldPos, float3 normal, float3 viewDir)
{
	float3 toLight = light.positionWS[0].xyz - worldPos;
	float dist = max(length(toLight), 1e-4f);
	float3 L = toLight / dist;

	float intensityFactor = saturate(dist / max(light.radius, 1e-4f));
	float attenuation = (1.0f - intensityFactor * intensityFactor) * light.fade;

	float NdotL = saturate(dot(normal, L));
	float3 diffuse = light.color * NdotL * attenuation;

	// Simplified specular (Blinn-Phong)
	float3 H = normalize(L + viewDir);
	float spec = pow(saturate(dot(normal, H)), 32.0f);
	float3 specular = light.color * spec * attenuation * 0.5f;

	return diffuse + specular;
}

float4 main(PixelIn input) : SV_Target0
{
	uint clusterLightOffset = 0;
	uint clusteredLightCount = 0;
	LightLimitFix::TryGetCluster(
		input.position.xy / float2(1920.0f, 1080.0f),
		input.position.z,
		ClusterSize,
		GetLLFCameraNear(),
		GetLLFCameraFar(),
		clusterLightOffset,
		clusteredLightCount);

	float3 viewDir = float3(0, 0, 1); // TODO: actual view direction

	float3 totalLight = float3(0.05f, 0.05f, 0.05f); // ambient
	uint totalLightCount = LightLimitFix::GetStrictLightCount() + clusteredLightCount;

	[loop] for (uint i = 0; i < totalLightCount; i++) {
		LightLimitFix::Light light = (LightLimitFix::Light)0;
		if (!LightLimitFix::GetStrictOrClusteredLight(i, clusterLightOffset, light))
			continue;

		totalLight += EvaluateLight(light, input.worldPos, normalize(input.normal), viewDir);
	}

	return float4(totalLight, 1.0f);
}
