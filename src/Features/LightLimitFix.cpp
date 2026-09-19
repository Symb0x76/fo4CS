// LightLimitFix -- the cross-cluster state.
//
// This file used to be the whole feature: 5208 lines, 5.8x the next largest in
// the repository. The implementation now lives in fourteen translation units
// under src/Features/LightLimit/, split along the cluster boundaries in
// docs/refactor-outlines/outline-LightLimitFix.md.
//
// What is left here is the one thing that cannot live in a cluster: the single
// definition of each object the clusters share. LLFInternal.h declares them all
// extern and explains, per object, which cluster writes it and which reads it.
// The pairs that matter:
//
//   - the descriptor observation latches, written by LLFClusterBind.cpp and read
//     by the gates in LLFConfig.cpp;
//   - the hook call counters, written by LLFHooks.cpp and LLFPointLightHook.cpp
//     and read by the reachability watchdog in LLFDiagnostics.cpp -- the log
//     line that reports whether the verified consumer path ran at all;
//   - s_preNGDFLightCameraCB, a COM pointer written by the capture in
//     LLFDFLightForward.cpp and read by the compute dispatch in
//     LLFClusterPrepass.cpp.
//
// Give any of these a definition inside a cluster and that cluster gets its own
// copy, the writers and readers stop agreeing, and the diagnostics start
// reporting a state the game is not in.
//
// The file keeps its name and path deliberately. A split file that disappears
// loses git's rename link on merge and gets its pre-split version written back
// into the working tree -- see docs/cs-refactor-merge-plan.md.

#include "Features/LightLimit/LLFInternal.h"

// Definitions for the cross-cluster state declared in LLFInternal.h. Exactly one
// definition each -- see that header for why they cannot live in a cluster.
namespace CommunityShaders::lightlimit
{
#if defined(FALLOUT_PRE_NG)
std::atomic_bool s_preNGDFLightLLFConsumerDescriptorObserved = false;
std::atomic_uint32_t s_preNGDFLightLLFConsumerDescriptorObservations = 0;
std::atomic_bool s_preNGDFCompositeLLFConsumerDescriptorObserved = false;
std::atomic_uint32_t s_preNGDFCompositeLLFConsumerDescriptorObservations = 0;
std::atomic_bool s_preNGBSLightingLLFConsumerDescriptorObserved = false;
std::atomic_uint32_t s_preNGBSLightingLLFConsumerDescriptorObservations = 0;
std::atomic_uint32_t s_preNGBSLightingLLFConsumerLastVertexDescriptor = 0;
std::atomic_uint32_t s_preNGBSLightingLLFConsumerLastPixelDescriptor = 0;
std::atomic_bool s_preNGBSLightingLLFConsumerLastFound = false;
std::atomic<std::uintptr_t> s_preNGBSLightingLLFConsumerLastVanillaPixelShader = 0;
std::atomic_bool s_preNGBSLightingDeferredResourceProofComplete = false;
std::atomic_uint64_t s_preNGBSLightingResourceProofBypassUntilFrame = 0;
std::atomic_uint32_t s_preNGBSLightingResourceProofBypassLogs = 0;
std::atomic_uint32_t s_preNGBSLightingVisibleConsumerMenuSuppressLogs = 0;
std::atomic_uint32_t s_preNGBSLightingPreviewMenuLastReason = 0;
std::atomic_bool s_preNGBSLightingPreviewMenuResumePending = false;
std::atomic_bool s_preNGBSLightingPreviewMenuConsumerResumePending = false;
std::atomic_uint32_t s_preNGShadowSceneLastBucketTotal = 0;
std::atomic_uint64_t s_preNGBSLightingSetupGeometryNoLightNextProbeFrame = 0;
std::atomic_uint64_t s_preNGBSLightingSetupGeometryBypassUntilFrame = 0;
std::atomic_uint32_t s_preNGBSLightingSetupGeometryBypassLogs = 0;
std::atomic_uint64_t s_preNGBSLightingSetupGeometryPreviewCacheFrame =
    kPreNGBSLightingSetupGeometryPreviewCacheInvalidFrame;
std::atomic_uint32_t s_preNGBSLightingSetupGeometryPreviewCacheReason = 0;
std::atomic_bool s_preNGPointLightHookInstalled = false;
std::atomic_bool s_preNGPointLightHookPatchVerified = false;
std::atomic_uint32_t s_preNGPointLightHookCallCount = 0;
std::atomic_bool s_preNGBSLightingSetupGeometryHookInstalled = false;
std::atomic_uint32_t s_preNGBSLightingSetupGeometryHookCallCount = 0;
std::atomic_uint32_t s_preNGBSLightingSetupGeometryBypassCallCount = 0;
std::atomic_bool s_preNGBSLightingBatchSetupHookInstalled = false;
std::atomic_uint32_t s_preNGBSLightingBatchSetupHookCallCount = 0;
winrt::com_ptr<ID3D11Buffer> s_preNGDFLightCameraCB;
std::atomic_bool s_preNGDFLightCameraCBCaptured = false;
#endif
}
