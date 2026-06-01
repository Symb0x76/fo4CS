#ifndef LLF_COMMON_HLSLI
#define LLF_COMMON_HLSLI

// Thread group dimensions for the culling compute shader
#define NUMTHREAD_X 16
#define NUMTHREAD_Y 16
#define NUMTHREAD_Z 4
#define GROUP_SIZE (NUMTHREAD_X * NUMTHREAD_Y * NUMTHREAD_Z)

// Maximum visible lights per cluster. Keep this matched with
// LightLimitFix.cpp:kClusterMaxLights.
#define MAX_CLUSTER_LIGHTS 128

namespace LightFlags
{
	static const uint PortalStrict = (1u << 0);
	static const uint Shadow       = (1u << 1);
	static const uint Simple       = (1u << 2);

	static const uint Initialised   = (1u << 8);
	static const uint Disabled      = (1u << 9);
	static const uint InverseSquare = (1u << 10);
	static const uint Linear        = (1u << 11);
}

// GPU-side layout matching LightLimitFix::ClusterAABB in C++
struct ClusterAABB
{
	float4 minPoint;
	float4 maxPoint;
};

// GPU-side layout matching LightLimitFix::LightGrid in C++
struct LightGrid
{
	uint offset;
	uint lightCount;
	uint2 pad0;
};

// GPU-side layout matching LightLimitFix::LightData in C++.
// float3 positionWS[2] → float4 positionWS[2] (float3 + uint pad per slot)
struct Light
{
	float3 color;           // offset 0
	float fade;             // offset 12 (+4 = 16 boundary)
	float radius;           // offset 16
	float invRadius;        // offset 20
	float fadeZone;         // offset 24
	float sizeBias;         // offset 28 (+4 = 16 boundary)
	float4 positionWS[2];   // offset 32 (+32 = 16 boundary)
	uint4 roomFlags;        // offset 64
	uint lightFlags;        // offset 80
	uint shadowLightIndex;  // offset 84
	uint pad0;              // offset 88
	uint pad1;              // offset 92 (+4 = 16 boundary)
};

#endif // LLF_COMMON_HLSLI
