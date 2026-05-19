// Lighting.hlsl — FO4 Community Shaders lighting pixel shader
// Replaces vanilla FO4 lighting PS via BSShader::ReloadShaders hook.
// Architecture mirrors Skyrim CS: compiled per (shaderType, descriptor)
// with feature defines injected at compile time.

#include "LightLimitFix/Common.hlsli"

#if defined(LIGHT_LIMIT_FIX)
	// Clustered-forward light grid resources (bound by LightLimitFix::Prepass)
	StructuredBuffer<Light>    Lights          : register(t35);
	StructuredBuffer<uint>     LightIndexList  : register(t36);
	StructuredBuffer<LightGrid> LightGrid      : register(t37);

	cbuffer ClusterData : register(b0)
	{
		uint4 ClusterSize;
		uint  EnableVisualisation;
		uint  VisualisationMode;
		float pad[2];
	}

	uint GetClusterIndex(float2 screenPos, float viewZ)
	{
		uint3 clusterCoord;
		clusterCoord.x = uint(screenPos.x * float(ClusterSize.x));
		clusterCoord.y = uint(screenPos.y * float(ClusterSize.y));
		// Logarithmic Z-slice (matching ClusterBuildingCS)
		clusterCoord.z = uint(log2(max(viewZ, 0.001)) * float(ClusterSize.z) / 10.0);
		clusterCoord = clamp(clusterCoord, 0u, uint3(ClusterSize.xyz - 1u));
		return clusterCoord.x
			+ clusterCoord.y * ClusterSize.x
			+ clusterCoord.z * (ClusterSize.x * ClusterSize.y);
	}

	float3 EvaluateLight(Light light, float3 worldPos, float3 N, float3 V)
	{
		float3 toLight = light.positionWS[0].xyz - worldPos;
		float dist = length(toLight);
		float3 L = toLight / dist;

		float attenuation = 1.0 - saturate(dist * light.invRadius);
		attenuation *= attenuation * light.fade;

		float NdotL = saturate(dot(N, L));
		float3 diffuse = light.color * NdotL * attenuation;

		float3 H = normalize(L + V);
		float spec = pow(saturate(dot(N, H)), 32.0);
		float3 specular = light.color * spec * attenuation * 0.5;

		return diffuse + specular;
	}
#endif

// PS entry point — invoked by FO4's BSShader::SetupGeometry pipeline.
// Input signature matches FO4's default lighting pixel shader layout.
float4 main(
	float4 position  : SV_Position,
	float3 worldPos  : TEXCOORD0,
	float3 normal    : TEXCOORD1,
	float2 texCoord  : TEXCOORD2
) : SV_Target0
{
	float3 totalLight = float3(0.03, 0.03, 0.03); // ambient

#if defined(LIGHT_LIMIT_FIX)
	float3 V = normalize(float3(0, 0, 1) - worldPos);
	uint clusterIdx = GetClusterIndex(
		position.xy / float2(1920.0, 1080.0),
		position.z);

	LightGrid grid = LightGrid[clusterIdx];
	float3 N = normalize(normal);

	for (uint i = 0; i < grid.lightCount && i < 128; i++) {
		uint lightIdx = LightIndexList[grid.offset + i];
		totalLight += EvaluateLight(Lights[lightIdx], worldPos, N, V);
	}
#endif

	return float4(totalLight, 1.0);
}
