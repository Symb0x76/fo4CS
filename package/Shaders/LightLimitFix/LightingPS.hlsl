#include "LightLimitFix/Common.hlsli"

// Clustered-forward lighting pixel shader template.
// Replaces the vanilla per-draw 4-light strict-light CB with a clustered light list.
//
// Deployment: Create a ShaderDB entry mapping the vanilla PS asm hash to this file.
// The ShaderCompiler compiles this and the Hooks redirect CreatePixelShader to use it.

// --- Vanilla-compatible inputs (adjust based on actual FO4 PS signature) ---
struct PixelIn
{
	float4 position : SV_Position;
	float3 worldPos : TEXCOORD0;
	float3 normal   : TEXCOORD1;
	float2 texCoord : TEXCOORD2;
};

// --- Resources bound by LightLimitFix::Prepass() ---
// t35: StructuredBuffer<Light>  lights          (all scene lights)
// t36: StructuredBuffer<uint>   lightIndexList  (per-cluster light indices)
// t37: StructuredBuffer<LightGrid> lightGrid    (per-cluster offset + count)

StructuredBuffer<Light>    Lights          : register(t35);
StructuredBuffer<uint>     LightIndexList  : register(t36);
StructuredBuffer<LightGrid> LightGrid      : register(t37);

// --- Cluster lookup ---
// ClusterSize is bound via the FeatureBuffer constant buffer
cbuffer ClusterData : register(b0)
{
	uint4 ClusterSize;           // x, y, z — grid dimensions
	uint  EnableVisualisation;
	uint  VisualisationMode;
	float pad[2];
}

uint GetClusterIndex(float2 screenPos, float viewZ)
{
	uint3 clusterCoord;
	clusterCoord.x = uint(screenPos.x * float(ClusterSize.x));
	clusterCoord.y = uint(screenPos.y * float(ClusterSize.y));
	clusterCoord.z = uint(log2(viewZ) * 0.05 * float(ClusterSize.z)); // simplified Z-slice
	clusterCoord = clamp(clusterCoord, 0u, uint3(ClusterSize.xyz - 1u));
	return clusterCoord.x + clusterCoord.y * ClusterSize.x + clusterCoord.z * (ClusterSize.x * ClusterSize.y);
}

// --- Light evaluation (simplified diffuse + specular) ---
float3 EvaluateLight(Light light, float3 worldPos, float3 normal, float3 viewDir)
{
	float3 toLight = light.positionWS[0].xyz - worldPos;
	float dist = length(toLight);
	float3 L = toLight / dist;

	float attenuation = 1.0f - saturate(dist * light.invRadius);
	attenuation *= attenuation;
	attenuation *= light.fade;

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
	uint clusterIndex = GetClusterIndex(input.position.xy / float2(1920.0f, 1080.0f), input.position.z);

	LightGrid grid = LightGrid[clusterIndex];
	float3 viewDir = float3(0, 0, 1); // TODO: actual view direction

	float3 totalLight = float3(0.05f, 0.05f, 0.05f); // ambient

	// Iterate over all lights affecting this cluster
	for (uint i = 0; i < grid.lightCount && i < MAX_CLUSTER_LIGHTS; i++) {
		uint lightIdx = LightIndexList[grid.offset + i];
		Light light = Lights[lightIdx];
		totalLight += EvaluateLight(light, input.worldPos, normalize(input.normal), viewDir);
	}

	return float4(totalLight, 1.0f);
}
