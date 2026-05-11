#include "LightLimitFix/Common.hlsli"

// Per-frame constant buffer — written by LightLimitFix::Prepass()
// CPU struct: LightBuildingCB + CameraData appended
cbuffer PerFrame : register(b0) {
  float LightsNear; // near plane for logarithmic Z-slicing
  float LightsFar;  // far plane for logarithmic Z-slicing
  uint2 padCB0;     // → 16 bytes

  uint4 ClusterSize; // x, y, z, pad — grid dimensions
                     // → 32 bytes

  float4x4 CameraProjInverse; // inverse projection (worldFromClip)
                              // → 96 bytes
}

RWStructuredBuffer<ClusterAABB> clusters : register(u0);

// Unproject a screen-space texcoord + depth to view-space position.
// texcoord in [0,1], depth in view-space Z.
float3 GetPositionVS(float2 texcoord, float depth) {
  float4 clip;
  clip.xy = texcoord * 2.0f - 1.0f;
  clip.y = -clip.y;
  clip.z = depth;
  clip.w = 1.0f;
  float4 homo = mul(CameraProjInverse, clip);
  return homo.xyz / homo.w;
}

// Intersect a view-space ray with a Z-aligned plane at the given depth.
float3 IntersectZPlane(float3 ray, float zDist) {
  float t = zDist / ray.z;
  return t * ray;
}

// One thread per cluster — builds view-space AABBs for the frustum grid.
[numthreads(1, 1, 1)] void main(uint3 groupId : SV_GroupID,
                                uint3 dispatchThreadId : SV_DispatchThreadID,
                                uint3 groupThreadId : SV_GroupThreadID,
                                uint groupIndex : SV_GroupIndex) {
  uint clusterIndex = groupId.x + groupId.y * ClusterSize.x +
                      groupId.z * (ClusterSize.x * ClusterSize.y);

  // Tile boundaries in normalized screen coordinates
  float2 rcpCluster = rcp(float2(ClusterSize.x, ClusterSize.y));
  float2 tcMin = float2(groupId.xy) * rcpCluster;
  float2 tcMax = float2(groupId.xy + 1u) * rcpCluster;

  // Unproject tile corners at the far plane (depth = 1)
  float3 minVS = GetPositionVS(tcMin, 1.0f);
  float3 maxVS = GetPositionVS(tcMax, 1.0f);

  // Logarithmic Z-slice partitioning
  float clusterNear = LightsNear * pow(abs(LightsFar / LightsNear),
                                       float(groupId.z) / float(ClusterSize.z));
  float clusterFar =
      LightsNear * pow(abs(LightsFar / LightsNear),
                       float(groupId.z + 1u) / float(ClusterSize.z));

  // Intersect frustum rays with near and far Z planes
  float3 mnNear = IntersectZPlane(minVS, clusterNear);
  float3 mnFar = IntersectZPlane(minVS, clusterFar);
  float3 mxNear = IntersectZPlane(maxVS, clusterNear);
  float3 mxFar = IntersectZPlane(maxVS, clusterFar);

  // Compute AABB enclosing the tile frustum
  float3 aabbMin = min(min(mnNear, mnFar), min(mxNear, mxFar));
  float3 aabbMax = max(max(mnNear, mnFar), max(mxNear, mxFar));

  clusters[clusterIndex].minPoint = float4(aabbMin, 0.0f);
  clusters[clusterIndex].maxPoint = float4(aabbMax, 0.0f);
}
