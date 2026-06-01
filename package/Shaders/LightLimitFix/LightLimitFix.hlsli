#ifndef LLF_LIGHT_LIMIT_FIX_HLSLI
#define LLF_LIGHT_LIMIT_FIX_HLSLI

namespace LightLimitFix
{
	// Support-only include for the eventual verified FO4 lighting shader path.
	// Keep the Common include inside this namespace like Skyrim CS so shader-side
	// consumers use LightLimitFix::Light and LightLimitFix::LightFlags.
#include "LightLimitFix/Common.hlsli"

	static const uint MaxStrictLights = 15;

	cbuffer StrictLightData : register(b3)
	{
		uint NumStrictLights;
		int RoomIndex;
		uint ShadowBitMask;
		uint pad0;
		Light StrictLights[MaxStrictLights];
	};

	StructuredBuffer<Light> lights : register(t35);
	StructuredBuffer<uint> lightList : register(t36);
	StructuredBuffer<LightGrid> lightGrid : register(t37);

	bool GetClusterIndex(in float2 uv, in float viewZ, in uint3 clusterSize, in float cameraNear, in float cameraFar, inout uint clusterIndex)
	{
		clusterIndex = 0;

		if (clusterSize.x == 0 || clusterSize.y == 0 || clusterSize.z == 0)
			return false;

		if (uv.x < 0.0f || uv.y < 0.0f || uv.x >= 1.0f || uv.y >= 1.0f)
			return false;

		const float nearZ = max(cameraNear, 0.001f);
		const float farZ = max(cameraFar, nearZ + 0.001f);
		const float logRange = log(farZ / nearZ);
		if (logRange <= 0.0f)
			return false;

		const float z = max(viewZ, nearZ);
		const uint clusterZ = (uint)(log(z / nearZ) * float(clusterSize.z) / logRange);
		const uint3 cluster = uint3(uint2(uv * float2(clusterSize.xy)), clusterZ);

		if (any(cluster >= clusterSize))
			return false;

		clusterIndex = cluster.x + (clusterSize.x * cluster.y) + (clusterSize.x * clusterSize.y * cluster.z);
		return true;
	}

	bool GetClusterIndex(in float2 uv, in float viewZ, in uint4 clusterSize, in float cameraNear, in float cameraFar, inout uint clusterIndex)
	{
		return GetClusterIndex(uv, viewZ, clusterSize.xyz, cameraNear, cameraFar, clusterIndex);
	}

	bool TryGetCluster(in float2 uv, in float viewZ, in uint3 clusterSize, in float cameraNear, in float cameraFar, out uint lightOffset, out uint lightCount)
	{
		uint clusterIndex = 0;
		lightOffset = 0;
		lightCount = 0;

		if (!GetClusterIndex(uv, viewZ, clusterSize, cameraNear, cameraFar, clusterIndex))
			return false;

		LightGrid grid = lightGrid[clusterIndex];
		lightOffset = grid.offset;
		lightCount = min(grid.lightCount, (uint)MAX_CLUSTER_LIGHTS);
		return true;
	}

	bool TryGetCluster(in float2 uv, in float viewZ, in uint4 clusterSize, in float cameraNear, in float cameraFar, out uint lightOffset, out uint lightCount)
	{
		return TryGetCluster(uv, viewZ, clusterSize.xyz, cameraNear, cameraFar, lightOffset, lightCount);
	}

	bool IsLightIgnored(Light light)
	{
		if (light.lightFlags & LightFlags::Shadow) {
			if (light.shadowLightIndex >= 32)
				return true;

			return (ShadowBitMask & (1u << light.shadowLightIndex)) == 0;
		}

		bool lightIgnored = false;
		if ((light.lightFlags & LightFlags::PortalStrict) && RoomIndex >= 0) {
			lightIgnored = true;
			int roomIndex = RoomIndex;

			[unroll]
			for (int flagsIndex = 0; flagsIndex < 4; ++flagsIndex) {
				if (roomIndex < 32) {
					if (((light.roomFlags[flagsIndex] >> roomIndex) & 1u) == 1u)
						lightIgnored = false;

					break;
				}

				roomIndex -= 32;
			}
		}

		return lightIgnored;
	}

	uint GetStrictLightCount()
	{
		return min(NumStrictLights, MaxStrictLights);
	}

	bool GetStrictOrClusteredLight(in uint lightIndex, in uint clusterLightOffset, inout Light light)
	{
		light = (Light)0;

		bool accepted = false;
		const uint strictLightCount = GetStrictLightCount();
		if (lightIndex < strictLightCount) {
			light = StrictLights[lightIndex];
			accepted = true;
		} else {
			const uint clusteredLightIndex = lightList[clusterLightOffset + (lightIndex - strictLightCount)];
			light = lights[clusteredLightIndex];
			accepted = !IsLightIgnored(light);
		}

		return accepted;
	}
}

#endif // LLF_LIGHT_LIMIT_FIX_HLSLI
