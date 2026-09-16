#pragma once

// Internal state and constants shared by the LightLimitFix translation units
// under src/Features/LightLimit/. These were an anonymous namespace inside
// LightLimitFix.cpp; splitting that file means the mutable state needs external
// linkage, so it lives in CommunityShaders::lightlimit and this header stays
// private to src/Features.
//
// Two rules this header exists to enforce:
//
//  1. Every `s_preNG*` below is ONE object with ONE definition. They are the
//     cross-cluster coupling -- observation latches written by the descriptor
//     binds and read by the config gates, hook call counts written by the hook
//     installers and read by the diagnostics watchdog, and the preview-menu
//     suppression state touched by three clusters. Declaring any of them in a
//     cluster instead of here silently gives that cluster its own copy, and the
//     gates start disagreeing with the writers.
//
//  2. Do NOT move a gate predicate body into this header, or make one inline.
//     Nearly every gate in LightLimitFix.cpp owns a function-local
//     `static const` that resolves and logs once. Two TUs means two latches and
//     a second log line. Each predicate keeps exactly one definition in exactly
//     one TU.
//
// `s_preNGDFLightCameraCB` deserves its own mention: it is a COM pointer written
// by the DFLight forward-capture hook and read by the compute dispatch in
// RunClusterPrepass. It must stay a single object across that boundary.

#include <d3d11.h>
#include <winrt/base.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace CommunityShaders::lightlimit
{
constexpr std::uint32_t kClusterMaxLights = 128;
constexpr std::uint32_t kMaxLights = 1024;
#if defined(FALLOUT_PRE_NG)
constexpr std::uint64_t kPreNGStableFrame = 5;
constexpr bool kPreNGEnableInternalPointLightHook = false;
constexpr const char *kPreNGSetupGeometryHookOptInEnv = "FO4CS_LLF_PRENG_SETUP_GEOMETRY_HOOK";
constexpr const char *kPreNGSetupGeometryStrictCBBindEnv = "FO4CS_LLF_PRENG_SETUP_GEOMETRY_BIND_STRICT_CB";
constexpr const char *kPreNGSetupGeometryPersistStrictCBEnv = "FO4CS_LLF_PRENG_SETUP_GEOMETRY_PERSIST_STRICT_CB";
constexpr const char *kPreNGSetupGeometryCallBudgetEnv = "FO4CS_LLF_PRENG_SETUP_GEOMETRY_CALL_BUDGET";
constexpr const char *kPreNGSetupGeometryFrameBudgetEnv = "FO4CS_LLF_PRENG_SETUP_GEOMETRY_FRAME_BUDGET";
constexpr const char *kPreNGPointLightHookOptInEnv = "FO4CS_LLF_PRENG_POINT_LIGHT_HOOK";
constexpr const char *kPreNGStrictLightCBDiagnosticEnv = "FO4CS_LLF_PRENG_STRICT_CB_DIAG";
constexpr const char *kPreNGStrictLightCBBindEnv = "FO4CS_LLF_PRENG_BIND_STRICT_CB";
constexpr const char *kPreNGClusterSRVBindEnv = "FO4CS_LLF_PRENG_BIND_CLUSTER_SRVS";
constexpr const char *kPreNGPrepassResourceBindEnv = "FO4CS_LLF_PRENG_PREPASS_BIND_RESOURCES";
constexpr const char *kPreNGPersistentClusterPrepassEnv = "FO4CS_LLF_PRENG_PERSISTENT_CLUSTER_PREPASS";
constexpr const char *kPreNGShadowSceneFastReuseEnv = "FO4CS_LLF_PRENG_SHADOW_SCENE_FAST_REUSE";
constexpr const char *kPreNGShadowSceneFastReuseRefreshIntervalEnv =
    "FO4CS_LLF_PRENG_SHADOW_SCENE_FAST_REUSE_REFRESH_INTERVAL";
constexpr const char *kPreNGDFLightDrawStateStrictCBBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_BIND_STRICT_CB";
constexpr const char *kPreNGDFLightDrawStateClusterSRVBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_BIND_CLUSTER_SRVS";
constexpr const char *kPreNGDFLightResourceNoOpPassEnv = "FO4CS_LLF_PRENG_DFLIGHT_RESOURCE_NOOP_PASS";
constexpr const char *kPreNGDFLightFullContractNoOpPassEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_NOOP_PASS";
constexpr const char *kPreNGDFLightLLFAdditivePassEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_PASS";
constexpr const char *kPreNGDFLightLegacyAdditiveProofEnv = "FO4CS_LLF_PRENG_DFLIGHT_LEGACY_ADDITIVE_PROOF";
constexpr const char *kPreNGDFLightLLFAdditivePersistentEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_PERSISTENT";
constexpr const char *kPreNGDFLightLLFAdditiveRefreshIntervalEnv = "FO4CS_LLF_PRENG_DFLIGHT_LLF_ADD_REFRESH_INTERVAL";
constexpr const char *kPreNGDFLightFullShadowedDescriptorConsumerEnv =
    "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER";
constexpr const char *kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv =
    "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER_UNSAFE";
constexpr const char *kPreNGDFLightFullContractVisibleLLFEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_VISIBLE_LLF";
constexpr const char *kPreNGDFCompositeResourceBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_RESOURCE_BIND";
constexpr const char *kPreNGDFCompositeSafeBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_SAFE_BIND";
constexpr const char *kPreNGDFCompositeVisibleLLFEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_VISIBLE_LLF";
constexpr const char *kPreNGBSLightingResourceBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_RESOURCE_BIND";
constexpr const char *kPreNGBSLightingSetupGeometryResourceBindEnv =
    "FO4CS_LLF_PRENG_BSLIGHTING_SETUP_GEOMETRY_RESOURCE_BIND";
constexpr const char *kPreNGBSLightingContractCompileEnv = "FO4CS_LLF_PRENG_BSLIGHTING_CONTRACT_COMPILE";
constexpr const char *kPreNGBSLightingConsumerCompileEnv = "FO4CS_LLF_PRENG_BSLIGHTING_CONSUMER_COMPILE";
constexpr const char *kPreNGBSLightingDescriptorObserveEnv = "FO4CS_LLF_PRENG_BSLIGHTING_DESCRIPTOR_OBSERVE";
constexpr const char *kPreNGBSLightingVanillaBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_VANILLA_BIND";
constexpr const char *kPreNGBSLightingLLFBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_LLF_BIND";
constexpr const char *kPreNGDFLightForwardLLFBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_FORWARD_LLF_BIND";
constexpr const char *kPreNGDisablePreviewOverloadGateEnv = "FO4CS_LLF_PRENG_DISABLE_PREVIEW_OVERLOAD_GATE";
// Diagnostic: allow the visible BSLighting LLF consumer to bind while a
// fullscreen preview menu is open (bypasses menu suppression). The consumer
// bind is triggered by shader lookups that FO4 only performs for the menu 3D
// preview, so this switch exists to prove the bind path reaches
// llfConsumerComplete=true. Keep OFF for normal play (menus stay vanilla).
constexpr const char *kPreNGBSLightingLLFBindMenuEnv = "FO4CS_LLF_PRENG_BSLIGHTING_LLF_BIND_MENU";
constexpr const char *kPreNGShaderObjectMetadataEnv = "FO4CS_LLF_PRENG_SHADER_OBJECT_METADATA";
constexpr const char *kPreNGTraceLLFPixelEnv = "FO4CS_TRACE_LLF_PS";
constexpr const char *kPreNGGpuTimingEnv = "FO4CS_LLF_PRENG_GPU_TIMING";
// Selects WHICH render-pipeline hook submits the clustered compute. Default
// (off/absent) = the historical Main_RenderWorld_Start site via Prepass().
// "early"/"1" = submit from EarlyPrepass() (Main_RenderShadowMaps phase), so the
// dispatch overlaps the engine's shadow-map GPU batch instead of landing in the
// frame-start idle pocket that tips NVIDIA power-mgmt into a downclock under the
// FrameGen interop fence ping-pong.
constexpr const char *kPreNGPrepassEarlyHookEnv = "FO4CS_LLF_PRENG_PREPASS_EARLY_HOOK";
constexpr const char *kPreNGDFLightContractCompileEnv = "FO4CS_LLF_PRENG_DFLIGHT_CONTRACT_COMPILE";
constexpr const char *kPreNGDFLightCandidateCompileEnv = "FO4CS_LLF_PRENG_DFLIGHT_CANDIDATE_COMPILE";
constexpr const char *kPreNGDFLightContractProbeSource = "LightLimitFix\\DFLightContractProbePS.hlsl";
constexpr const char *kPreNGDFLightFullShadowedCandidateSource = "LightLimitFix\\DFLightFullShadowedPS.hlsl";
constexpr std::uint32_t kPreNGMaxSetupGeometryCallBudget = 1000000;
constexpr std::uint32_t kPreNGDefaultSetupGeometryFrameBudget = 2;
constexpr std::uint32_t kPreNGMaxSetupGeometryFrameBudget = 64;
constexpr std::uint32_t kPreNGDefaultShadowSceneFastReuseRefreshInterval = 8;
constexpr std::uint32_t kPreNGMinShadowSceneFastReuseRefreshInterval = 1;
constexpr std::uint32_t kPreNGMaxShadowSceneFastReuseRefreshInterval = 600;
constexpr std::uint32_t kPreNGDefaultDFLightLLFAdditiveRefreshInterval = 0;
constexpr std::uint32_t kPreNGMinDFLightLLFAdditiveRefreshInterval = 0;
constexpr std::uint32_t kPreNGMaxDFLightLLFAdditiveRefreshInterval = 600;
constexpr float kPreNGClusterBuildReuseTolerance = 1.0e-3f;
extern std::atomic_bool s_preNGDFLightLLFConsumerDescriptorObserved;
extern std::atomic_uint32_t s_preNGDFLightLLFConsumerDescriptorObservations;
extern std::atomic_bool s_preNGDFCompositeLLFConsumerDescriptorObserved;
extern std::atomic_uint32_t s_preNGDFCompositeLLFConsumerDescriptorObservations;
extern std::atomic_bool s_preNGBSLightingLLFConsumerDescriptorObserved;
extern std::atomic_uint32_t s_preNGBSLightingLLFConsumerDescriptorObservations;
extern std::atomic_uint32_t s_preNGBSLightingLLFConsumerLastVertexDescriptor;
extern std::atomic_uint32_t s_preNGBSLightingLLFConsumerLastPixelDescriptor;
extern std::atomic_bool s_preNGBSLightingLLFConsumerLastFound;
extern std::atomic<std::uintptr_t> s_preNGBSLightingLLFConsumerLastVanillaPixelShader;
extern std::atomic_bool s_preNGBSLightingDeferredResourceProofComplete;
// Descriptor-burst settle buffer (frames) applied after BSLighting descriptor
// observation by ExtendPreNGBSLightingResourceProofDescriptorSettle. Kept very
// short so the clustered prepass resumes almost immediately once a preview
// menu closes (BOSS: resume clustered prepass right after the menu closes).
// Preview-menu suppression itself is live per-frame via menu detection and
// does not depend on this value. A tiny non-zero buffer still smooths the
// descriptor-burst transition without a visible delay.
constexpr std::uint64_t kPreNGBSLightingResourceProofMenuSettleFrames = 2;
constexpr std::string_view kPreNGBSLightingResourceProofLockpickingMenu{"LockpickingMenu"};
// Fullscreen 3D preview menus that latch the LLF decode onto the world
// ShadowSceneNode (1000+ lights), collapsing framerate via the per-frame
// clustered prepass. Like LockpickingMenu, these need permanent suppression
// of the clustered prepass / deferred b3-t35-t37 bind for the process: the
// preview rig itself only needs its handful of vanilla lights, and visible
// LLF is not wanted while a preview menu is up. See docs/current-state.md.
constexpr std::array kPreNGBSLightingResourceProofBlockingMenus{kPreNGBSLightingResourceProofLockpickingMenu,
                                                                std::string_view{"PipboyMenu"},
                                                                std::string_view{"TerminalMenu"},
                                                                std::string_view{"ExamineMenu"},
                                                                std::string_view{"ExamineConfirmMenu"},
                                                                std::string_view{"ContainerMenu"},
                                                                std::string_view{"BarterMenu"},
                                                                std::string_view{"PowerArmorModMenu"}};
extern std::atomic_uint64_t s_preNGBSLightingResourceProofBypassUntilFrame;
extern std::atomic_uint32_t s_preNGBSLightingResourceProofBypassLogs;
extern std::atomic_uint32_t s_preNGBSLightingVisibleConsumerMenuSuppressLogs;
extern std::atomic_uint32_t s_preNGBSLightingPreviewMenuLastReason;
extern std::atomic_bool s_preNGBSLightingPreviewMenuResumePending;
extern std::atomic_bool s_preNGBSLightingPreviewMenuConsumerResumePending;
// Most recent raw shadow-scene bucket light total (pre-truncation). Preview
// menus (ExamineMenu etc.) latch the decode onto a world node with ~1000+
// lights; after the menu closes the node can stay selected for a few frames
// before the scene returns to its normal handful. Resuming the clustered
// prepass during that overload window causes the residual stutter. We hold
// the prepass until this count drops back below the threshold — a
// state-based resume gate, NOT a permanent light cap (Skyrim CS still
// supports dense scenes; this only avoids the preview-menu transition).
extern std::atomic_uint32_t s_preNGShadowSceneLastBucketTotal;
constexpr std::uint32_t kPreNGShadowScenePreviewOverloadLights = 256;
constexpr std::uint64_t kPreNGBSLightingSetupGeometryNoLightBypassFrames = 30;
constexpr std::array kPreNGBSLightingSetupGeometryPreviewMenus{
    std::string_view{"PipboyMenu"},         std::string_view{"TerminalMenu"},  std::string_view{"ExamineMenu"},
    std::string_view{"ExamineConfirmMenu"}, std::string_view{"ContainerMenu"}, std::string_view{"BarterMenu"},
    std::string_view{"PowerArmorModMenu"}};
constexpr std::uint32_t kPreNGBSLightingSetupGeometryWorkshopPreviewReason =
    static_cast<std::uint32_t>(kPreNGBSLightingSetupGeometryPreviewMenus.size() + 1);
constexpr std::uint64_t kPreNGBSLightingSetupGeometryPreviewCacheInvalidFrame = static_cast<std::uint64_t>(-1);
extern std::atomic_uint64_t s_preNGBSLightingSetupGeometryNoLightNextProbeFrame;
extern std::atomic_uint64_t s_preNGBSLightingSetupGeometryBypassUntilFrame;
extern std::atomic_uint32_t s_preNGBSLightingSetupGeometryBypassLogs;
extern std::atomic_uint64_t s_preNGBSLightingSetupGeometryPreviewCacheFrame;
extern std::atomic_uint32_t s_preNGBSLightingSetupGeometryPreviewCacheReason;
extern std::atomic_bool s_preNGPointLightHookInstalled;
extern std::atomic_bool s_preNGPointLightHookPatchVerified;
extern std::atomic_uint32_t s_preNGPointLightHookCallCount;
extern std::atomic_bool s_preNGBSLightingSetupGeometryHookInstalled;
extern std::atomic_uint32_t s_preNGBSLightingSetupGeometryHookCallCount;
extern std::atomic_uint32_t s_preNGBSLightingSetupGeometryBypassCallCount;
extern std::atomic_bool s_preNGBSLightingBatchSetupHookInstalled;
extern std::atomic_uint32_t s_preNGBSLightingBatchSetupHookCallCount;
// Written by the DFLight forward-capture hook, read by the clustered compute
// dispatch. One object, two clusters -- see the header comment.
extern winrt::com_ptr<ID3D11Buffer> s_preNGDFLightCameraCB;
extern std::atomic_bool s_preNGDFLightCameraCBCaptured;
#endif
}
