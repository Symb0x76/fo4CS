#include "LightLimitFix/Common.hlsli"

// Per-frame constant buffer — written by LightLimitFix::Prepass()
// CPU struct: LightBuildingCB + CameraData appended.
// LightsNear/LightsFar are now the VANILLA cb12 view-z domain [1, 333333],
// matching the DFLight dual inverse-projection rows (cb12[20..27]).
cbuffer PerFrame : register(b0) {
  float LightsNear; // = 1.0 (vanilla near matrix z at d=0)
  float LightsFar;  // = 333333 (vanilla far matrix z at d->1)
  uint2 padCB0;     // → 16 bytes

  uint4 ClusterSize; // x, y, z, pad — grid dimensions
                     // → 32 bytes

  float4x4 CameraProjInverse; // unused now; kept for CPU struct compatibility
                              // → 96 bytes
}

// The real vanilla camera cb12 (captured at runtime and bound to CS slot 12).
cbuffer CameraCB : register(b12) {
  float4 CameraRows[30];
}

RWStructuredBuffer<ClusterAABB> clusters : register(u0);

// Unproject a screen-space texcoord + view-space Z using the SAME dual
// inverse-projection matrices vanilla DFLight uses in cb12[20..27].
//   far matrix  = rows[20..23], d = (1/z - rows[23].w) / rows[23].z
//   near matrix = rows[24..27], d = (1/z - rows[27].w) / rows[27].z
// Switch at the vanilla raw-depth threshold (rawDepth = 0.01), which
// corresponds to z = 1 / (rows[23].w + rows[23].z * 0.0001).
float3 GetPositionVS(float2 texcoord, float viewZ) {
  float4 r0, r1, r2, r3;
  float d;
  float switchDenom = CameraRows[23].w + CameraRows[23].z * 0.0001f;
  const float zSwitch = 1.0f / switchDenom;
  if (viewZ < zSwitch) {
    r0 = CameraRows[24]; r1 = CameraRows[25]; r2 = CameraRows[26]; r3 = CameraRows[27];
    d = (1.0f / viewZ - r3.w) / r3.z;
  } else {
    r0 = CameraRows[20]; r1 = CameraRows[21]; r2 = CameraRows[22]; r3 = CameraRows[23];
    d = (1.0f / viewZ - r3.w) / r3.z;
  }

  float2 ndc = float2(texcoord.x * 2.0f - 1.0f, 1.0f - texcoord.y * 2.0f);
  float4 clip = float4(ndc, d, 1.0f);
  float3 viewPos;
  viewPos.x = dot(r0, clip);
  viewPos.y = dot(r1, clip);
  viewPos.z = dot(r2, clip);
  float w = dot(r3, clip);
  return viewPos / w;
}

// One thread per cluster — builds view-space AABBs for the frustum grid using
// the exact vanilla dual-projection reconstruction at each Z-slice boundary.
[numthreads(1, 1, 1)] void main(uint3 groupId : SV_GroupID,
                                 uint3 dispatchThreadId : SV_DispatchThreadID,
                                 uint3 groupThreadId : SV_GroupThreadID,
                                 uint groupIndex : SV_GroupIndex) {
  uint clusterIndex = groupId.x + groupId.y * ClusterSize.x +
                      groupId.z * (ClusterSize.x * ClusterSize.y);

  float2 rcpCluster = rcp(float2(ClusterSize.x, ClusterSize.y));
  float2 tcMin = float2(groupId.xy) * rcpCluster;
  float2 tcMax = float2(groupId.xy + 1u) * rcpCluster;

  float clusterNear = LightsNear * pow(abs(LightsFar / LightsNear),
                                       float(groupId.z) / float(ClusterSize.z));
  float clusterFar =
      LightsNear * pow(abs(LightsFar / LightsNear),
                       float(groupId.z + 1u) / float(ClusterSize.z));

  float3 aabbMin = 1.0e10f;
  float3 aabbMax = -1.0e10f;
  float2 cornersX = float2(tcMin.x, tcMax.x);
  float2 cornersY = float2(tcMin.y, tcMax.y);
  [unroll]
  for (uint ix = 0; ix < 2; ++ix) {
    [unroll]
    for (uint iy = 0; iy < 2; ++iy) {
      float2 tc = float2(cornersX[ix], cornersY[iy]);
      float3 pNear = GetPositionVS(tc, clusterNear);
      float3 pFar = GetPositionVS(tc, clusterFar);
      aabbMin = min(aabbMin, min(pNear, pFar));
      aabbMax = max(aabbMax, max(pNear, pFar));
    }
  }

  clusters[clusterIndex].minPoint = float4(aabbMin, 0.0f);
  clusters[clusterIndex].maxPoint = float4(aabbMax, 0.0f);
}
