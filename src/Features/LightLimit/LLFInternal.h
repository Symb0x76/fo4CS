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

#include "Core/ShaderCache.h"
#include "Features/LightLimitFix.h"

#include <DirectXMath.h>
#include <RE/FO4Runtime.h>
#include <d3d11.h>
#include <winrt/base.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Nested profiling zones inside the LightLimitFix clusters. The Feature.h zone
// wraps the whole phase, which measured 7.74ms CPU against 0.28ms GPU per frame
// on PreNG (2026-09-14 capture) -- enough to say the cost is CPU-side, not
// enough to say which part. These split it. Compiled out entirely without
// TRACY_SUPPORT=ON, and the include is guarded because the tracy dependency only
// exists behind the vcpkg "tracy" manifest feature.
//
// ZoneScopedN declares a fixed-name variable, so two of them in one scope fail
// to compile. ZoneNamedN takes the variable name, which lets several coexist --
// needed because some of these markers share a scope with a nested zone.
//
// This lives here rather than in a cluster so the zone names stay stable across
// the split: the SYMB-5 consumer-reachability investigation reads these zones,
// and a renamed or duplicated zone would invalidate the captures it compares
// against.
#ifdef TRACY_ENABLE
#	include <Tracy/Tracy.hpp>
#	define FO4CS_LLF_ZONE_IMPL2(name, counter) ZoneNamedN(___fo4cs_llf_zone_##counter, name, true)
#	define FO4CS_LLF_ZONE_IMPL(name, counter) FO4CS_LLF_ZONE_IMPL2(name, counter)
#	define FO4CS_LLF_ZONE(name) FO4CS_LLF_ZONE_IMPL(name, __COUNTER__)
#else
#	define FO4CS_LLF_ZONE(name) \
		do {                    \
		} while (false)
#endif

namespace CommunityShaders::lightlimit
{
// The runtime-abstraction alias every PreNG cluster spells. Kept here so the
// clusters agree on it rather than each re-deriving it.
namespace F4Runtime = RE::FO4Runtime;

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

// ---------------------------------------------------------------------------
// Shared vocabulary.
//
// Types and constants that more than one cluster names. Unlike the gates above
// these carry no state, so a header is the right home for them: a struct
// definition and a constexpr are the same entity in every TU that sees them.
//
// The two raw-read templates are the one intentional exception to "no bodies in
// this header". They must be visible wherever they are instantiated, and being
// templates they are one entity per instantiation no matter how many TUs
// include this -- there is no latch to split.
// ---------------------------------------------------------------------------

constexpr std::uint64_t kPreNGFNVOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kPreNGFNVPrime = 1099511628211ull;

constexpr std::uint32_t kPreNGBSRenderPassSceneLightFirstIndex =
    F4Runtime::PreNG::BS_RENDER_PASS_SCENE_LIGHT_FIRST_INDEX;
constexpr std::uint32_t kPreNGInvalidShadowLightMaskIndex = F4Runtime::PreNG::INVALID_SHADOW_LIGHT_MASK_INDEX;
constexpr std::uint32_t kPreNGMaxShadowLightMaskBits = F4Runtime::PreNG::MAX_SHADOW_LIGHT_MASK_BITS;
constexpr std::uint32_t kPreNGMaxShadowSceneActiveLights = 8192;
constexpr std::uint32_t kPreNGMaxShadowSceneDecodeLights = kMaxLights;
constexpr float kPreNGLightContributionThreshold = 1.0e-4f;
constexpr float kPreNGLightRadiusThreshold = 1.0e-4f;

using PreNGPixelShaderEntryState = F4Runtime::PreNGShaderEntryState;
using PreNGShadowSceneNodeRef = F4Runtime::PreNGShadowSceneNodeRef;

// Raw reads for the per-frame shadow-scene decode hot path. The engine owns the
// shadow scene node + light wrappers + NiLights and keeps them valid for the
// whole frame, so a raw memcpy is safe here. The old F4Runtime::ReadValue path
// called VirtualQuery before EVERY read (~1800 syscalls per full decode), which
// measured ~12ms and produced the once-per-second stutter.
template <class T>
bool ReadPreNGRaw(std::uintptr_t a_address, T &a_value)
{
    std::memcpy(&a_value, reinterpret_cast<const void *>(a_address), sizeof(T));
    return true;
}

template <class T>
bool ReadPreNGRawField(const F4Runtime::RuntimeField &a_field, std::uintptr_t a_base, T &a_value)
{
    return ReadPreNGRaw(a_field.address(a_base), a_value);
}

struct PreNGShaderSlotEvidence
{
    bool hasMetadata = false;
    bool declaresCB3 = false;
    bool declaresT35 = false;
    bool declaresT36 = false;
    bool declaresT37 = false;
    std::uint32_t samplesT35 = 0;
    std::uint32_t samplesT36 = 0;
    std::uint32_t samplesT37 = 0;
};

enum class PreNGLightDecodeResult
{
    Decoded,
    MissingWrapperData,
    InvalidNiLightData,
    NonContributingLightData
};

// PostPostLoad reads this to decide whether the SetupGeometry hooks may be
// installed, so it outlives its own cluster.
enum class PreNGPointLightHookState
{
    Failed,
    Prepared,
    Installed,
    InstalledUnverified
};

enum class EnvironmentSwitchSource
{
    kNone,
    kDebugIni
};

struct EnvironmentSwitchState
{
    bool enabled = false;
    EnvironmentSwitchSource source = EnvironmentSwitchSource::kNone;
};

struct EnvironmentUIntState
{
    std::uint32_t value = 0;
    EnvironmentSwitchSource source = EnvironmentSwitchSource::kNone;
    bool present = false;
    bool valid = false;
};
#endif

// ---------------------------------------------------------------------------
// Cross-cluster helper declarations.
//
// These are DECLARATIONS only, and that is the whole point -- see rule 2 above.
// A helper listed here has exactly one definition, in exactly one cluster TU,
// and the clusters that call it get it through this header. Adding a body to
// anything below turns a one-shot latch into one latch per TU.
// ---------------------------------------------------------------------------

// Defined in LLFResources.cpp. Called from the cluster prepass for the two
// constant-buffer Map() failures and from every resource creation site.
bool LogResourceFailure(const char *a_name, HRESULT a_hr);

// Defined in LLFResources.cpp. Called from the cluster prepass to reject a
// non-finite view/projection matrix before it reaches the compute dispatch.
bool IsFiniteMatrix(const DirectX::XMFLOAT4X4 &a_matrix);

#if defined(FALLOUT_PRE_NG)
// Defined in LightLimitFix.cpp (config cluster). Deliberately NOT cached -- see
// the comment at its definition -- so it must not be turned into a latched
// static here or anywhere else.
bool ShouldTimePreNGClusterPrepassGpu();

// Defined in LightLimitFix.cpp (diagnostics cluster), called from
// SetupResources. Each owns its own one-shot `attempted`/`loggedHeld` pair and
// they are deliberately two functions, not one parameterised one -- see
// outline-LightLimitFix.md section 8.
void RunPreNGDFLightContractProbeCompileDiagnostic();
void RunPreNGDFLightFullShadowedCandidateCompileDiagnostic();
#endif

#if defined(FALLOUT_PRE_NG)
// --- LLFConfig.cpp ---------------------------------------------------------
//
// Every one of these resolves a Debug.ini switch into a function-local
// `static const` the first time it is called, logs that resolution once, and
// returns the latched value forever after. That is why they are declarations
// here and definitions in exactly one TU. Give any of them a body in this
// header and its caller's TU gets a second latch and a second log line.

const char *EnvironmentSwitchSourceName(EnvironmentSwitchSource a_source);
EnvironmentSwitchState ReadEnvironmentSwitch(const char *a_name);
EnvironmentUIntState ReadEnvironmentUInt(const char *a_name);
bool IsTruthyEnvironmentSwitch(const char *a_name);

std::uint32_t GetPreNGDFLightLLFAdditiveRefreshInterval();
std::optional<std::uint32_t> GetPreNGSetupGeometryCallBudget();
std::uint32_t GetPreNGSetupGeometryFrameBudget();
std::uint32_t GetPreNGShadowSceneFastReuseRefreshInterval();

bool TryReservePreNGSetupGeometryCall();
bool TryReservePreNGSetupGeometryFrameSample();
bool TryReservePreNGBSLightingSetupGeometryNoLightProbeFrame();
void ExtendPreNGBSLightingSetupGeometryBypassWindow();

bool ShouldInstallPreNGInternalPointLightHook();
bool ShouldUpdatePreNGStrictLightCB();
bool ShouldBindPreNGStrictLightCB();
bool ShouldBindPreNGSetupGeometryStrictLightCB();
bool ShouldPersistPreNGSetupGeometryStrictLightCB();
bool ShouldBindPreNGClusterSRVs();
bool ShouldBindPreNGPrepassResources();
bool ShouldBindPreNGDFLightDrawStateStrictLightCB();
bool ShouldBindPreNGDFLightDrawStateClusterSRVs();
bool ShouldUsePreNGSetupGeometryStrictLightCBProof();
bool ShouldReusePreNGShadowSceneFastReuse();
bool ShouldRunPreNGDFLightResourceNoOpPass();
bool ShouldRunPreNGDFLightFullContractNoOpPass();
bool ShouldRunPreNGDFLightLLFAdditivePass();
bool ShouldRunPreNGDFLightFullContractVisibleLLF();
bool ShouldRunPreNGDFCompositeVisibleLLF();
bool ShouldUsePreNGDFLightDescriptorDemandResources();
bool ShouldUsePreNGDFCompositeDescriptorDemandResources();
bool ShouldUsePreNGBSLightingDescriptorDemandResources();
bool ShouldBindPreNGBSLightingLLFVisibleConsumer();
bool ShouldBindPreNGDFLightForwardVisibleLLF();
bool ShouldAllowPreNGBSLightingConsumerBindInMenu();
bool ShouldBindPreNGBSLightingSetupGeometryResources();
bool ShouldSubmitPreNGClusterPrepassEarly();
bool ShouldHoldPreNGDFLightPreparedState();
bool ShouldRunPreNGClusterPrepassProof();
bool ShouldCompilePreNGDFLightContractProbe();
bool ShouldCompilePreNGDFLightFullShadowedCandidate();

// --- LLFShaderMetadata.cpp -------------------------------------------------
//
// Pure formatting over ShaderCache metadata. Read by the diagnostics snapshot,
// the compile-only probes and the live binding audit.

PreNGPixelShaderEntryState ReadPreNGCurrentPixelShaderEntryState();
bool HasPreNGConstantBufferSlot(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata, std::uint32_t a_slot);
bool HasPreNGTextureSlot(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata, std::uint32_t a_slot);
std::uint32_t GetPreNGTextureSampleCount(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata,
                                         std::uint32_t a_slot);
std::string FormatPreNGShaderBufferSlots(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata);
std::string FormatPreNGShaderTextureSlots(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata);
std::string FormatPreNGShaderTextureSampleCounts(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata);
PreNGShaderSlotEvidence GetPreNGShaderSlotEvidence(
    const std::optional<CommunityShaders::ShaderCache::ShaderMetadata> &a_metadata);
bool HasPreNGFullShadowedDFLightVanillaContract(
    const std::optional<CommunityShaders::ShaderCache::ShaderMetadata> &a_metadata);
std::string FormatPreNGShaderMetadata(const std::optional<CommunityShaders::ShaderCache::ShaderMetadata> &a_metadata);

// --- LLFLightDecode.cpp ----------------------------------------------------
//
// Hashing, the cluster-reuse cache keys and the BSLight wrapper decode. Pure
// functions over game memory; the cluster prepass and the light-collection
// paths are the callers.

bool PreNGClusterBuildInputsMatch(const LightLimitFix::ClusterBuildCacheState &a_cached,
                                  const LightLimitFix::LightBuildingCB &a_current);
void HashPreNGAppendBytes(std::uint64_t &a_hash, const void *a_data, std::size_t a_size);
std::uint64_t HashPreNGBytes(const void *a_data, std::size_t a_size);
LightLimitFix::ClusterPayloadCacheState MakePreNGClusterPayloadCacheState(
    const std::vector<LightLimitFix::LightData> &a_lights, std::uint32_t a_lightCount,
    const DirectX::XMFLOAT4X4 &a_viewTransposed,
    float a_lightsNear, float a_lightsFar, const std::uint32_t (&a_clusterSize)[3]);
bool SamePreNGShadowSceneFastReuseKey(const LightLimitFix::ShadowSceneFastReuseKey &a_lhs,
                                      const LightLimitFix::ShadowSceneFastReuseKey &a_rhs);
bool SamePreNGShadowSceneFastReuseStructure(const LightLimitFix::ShadowSceneFastReuseKey &a_lhs,
                                            const LightLimitFix::ShadowSceneFastReuseKey &a_rhs);
bool ReadPreNGShadowSceneBucketHash(const F4Runtime::PreNGShadowSceneBucket &a_bucket, std::uint64_t &a_hash);
bool MakePreNGShadowSceneFastReuseKey(const F4Runtime::PreNGShadowSceneNodeRef &a_nodeRef,
                                      const F4Runtime::PreNGShadowSceneBuckets &a_buckets,
                                      LightLimitFix::ShadowSceneFastReuseKey &a_key);
PreNGShadowSceneNodeRef GetPreNGWorldShadowSceneNode();
PreNGLightDecodeResult DecodePreNGBSLightWrapper(std::uintptr_t a_wrapperAddress, LightLimitFix::LightData &a_data,
                                                 std::uintptr_t &a_niLightAddress, bool &a_shadowMaskUnreadable,
                                                 bool &a_shadowMaskInvalid, std::uint32_t &a_shadowMaskBit);

// --- LLFPointLightHook.cpp -------------------------------------------------

bool ValidatePreNGPointLightCallsite();
bool VerifyPreNGPointLightHookPatch(std::uintptr_t a_runtimeCall);
const char *PreNGPointLightHookStateName(PreNGPointLightHookState a_state);
bool CanInstallPreNGSetupGeometryHooks(PreNGPointLightHookState a_pointLightHookState);
PreNGPointLightHookState PreparePreNGPointLightHook();

// --- LLFDiagnostics.cpp ----------------------------------------------------

void LogPreNGDiagnosticEnvironmentSnapshot();
void LogPreNGHookReachabilityWatchdog(std::uint64_t a_frame);

// --- LLFPreviewMenu.cpp ----------------------------------------------------
//
// The cluster prepass consults these every frame to decide whether a fullscreen
// preview menu should hold the dispatch, and to name the reason in its log.

bool ShouldDeferPreNGBSLightingResourceProofForMenu();
std::string_view GetPreNGBSLightingLastPreviewMenuReason();
void ExtendPreNGBSLightingResourceProofDescriptorSettle();
const char *GetPreNGBSLightingSetupGeometryPreviewReasonName(std::uint32_t a_reason);
std::uint32_t GetCachedPreNGBSLightingSetupGeometryPreviewReason();

// --- LLFDFLightForward.cpp -------------------------------------------------
//
// Captures the vanilla DFLight camera cb12 on the first batch pass that binds
// it; ClusterBuildingCS reads rows 20..27 out of that copy. The caller is the
// batch-setup thunk in LLFHooks.cpp -- this was file-static in the parent, which
// hid that the call crosses what is now a cluster boundary.
void CapturePreNGDFLightCameraCBOnce();

// --- LLFRuntimeAddresses.cpp -----------------------------------------------
//
// The renderer-state base is resolved by walking TEB -> TLS slot, with a fixed
// fallback address. Both are fixed addresses in the game image, so readability
// is settled at load time -- IsPreNGDFLightRendererStateReadable caches per
// binary and per base for exactly that reason, and re-probing it per frame is
// the VirtualQuery contention that cost 5.33 ms/frame before ad7cab9. Do not
// reintroduce a per-call probe here.

std::uintptr_t GetPreNGDFLightRendererStateBase();
bool IsPreNGDFLightRendererStateReadable(std::uintptr_t a_rendererBase);
void TryBindPreNGBSLightingDeferredDescriptorResources(LightLimitFix &a_feature);
#endif

}
