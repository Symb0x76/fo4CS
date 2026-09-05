#include "Features/LightLimitFix.h"
#include "Core/DebugSwitches.h"
#include <DirectXMath.h>
#include <RE/FO4Runtime.h>
#include <Windows.h>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <bit>
#include <intrin.h>
#include <memory>
#include <mutex>
#include <string_view>
#if defined(FALLOUT_PRE_NG)
#include "RE/Bethesda/IMenu.h"
#include "RE/Bethesda/UI.h"
#endif

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"
#include "Core/ShaderCache.h"
#include "Core/ShaderCompiler.h"
#include "Core/State.h"
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSGraphics.h"
#else
#include "RE/Bethesda/BSGraphics.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSFadeNode.h"
#else
#include "RE/Bethesda/BSFadeNode.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESObjectLIGH.h"
#include "RE/T/TESObjectREFR.h"
#else
#include "RE/Bethesda/TESBoundAnimObjects.h"
#include "RE/Bethesda/TESDataHandler.h"
#include "RE/Bethesda/TESObjectREFRs.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/N/NiLight.h"
#else
#include "RE/NetImmerse/NiLight.h"
#endif

#include "SimpleIni.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <sstream>

namespace
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
// "Last mile": generalize the visible BSLighting consumer bind from the five
// known menu-preview contract descriptors (0x1/0x101/0x111/0x141/0x201) to the
// descriptors normal-world rendering actually uses. WORLD_OBSERVE is learn-only
// (records + logs the observed descriptors, never binds); WORLD_BIND performs
// the bind and is gated behind the master FO4CS_LLF_PRENG_BSLIGHTING_LLF_BIND
// switch plus the payload/menu/cluster-SRV safety gates in TryBind.
constexpr const char *kPreNGBSLightingWorldObserveEnv = "FO4CS_LLF_PRENG_BSLIGHTING_WORLD_OBSERVE";
constexpr const char *kPreNGBSLightingWorldBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_WORLD_BIND";
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
// LLF is not wanted while a preview menu is up. See .codex/docs/current-state.md.
constexpr std::array kPreNGBSLightingResourceProofBlockingMenus{kPreNGBSLightingResourceProofLockpickingMenu,
                                                                std::string_view{"PipboyMenu"},
                                                                std::string_view{"TerminalMenu"},
                                                                std::string_view{"ExamineMenu"},
                                                                std::string_view{"ExamineConfirmMenu"},
                                                                std::string_view{"ContainerMenu"},
                                                                std::string_view{"BarterMenu"},
                                                                std::string_view{"PowerArmorModMenu"}};
std::atomic_uint64_t s_preNGBSLightingResourceProofBypassUntilFrame = 0;
std::atomic_uint32_t s_preNGBSLightingResourceProofBypassLogs = 0;
std::atomic_uint32_t s_preNGBSLightingVisibleConsumerMenuSuppressLogs = 0;
std::atomic_uint32_t s_preNGBSLightingPreviewMenuLastReason = 0;
std::atomic_bool s_preNGBSLightingPreviewMenuResumePending = false;
std::atomic_bool s_preNGBSLightingPreviewMenuConsumerResumePending = false;
// Most recent raw shadow-scene bucket light total (pre-truncation). Preview
// menus (ExamineMenu etc.) latch the decode onto a world node with ~1000+
// lights; after the menu closes the node can stay selected for a few frames
// before the scene returns to its normal handful. Resuming the clustered
// prepass during that overload window causes the residual stutter. We hold
// the prepass until this count drops back below the threshold — a
// state-based resume gate, NOT a permanent light cap (Skyrim CS still
// supports dense scenes; this only avoids the preview-menu transition).
std::atomic_uint32_t s_preNGShadowSceneLastBucketTotal = 0;
constexpr std::uint32_t kPreNGShadowScenePreviewOverloadLights = 256;
constexpr std::uint64_t kPreNGBSLightingSetupGeometryNoLightBypassFrames = 30;
constexpr std::array kPreNGBSLightingSetupGeometryPreviewMenus{
    std::string_view{"PipboyMenu"},         std::string_view{"TerminalMenu"},  std::string_view{"ExamineMenu"},
    std::string_view{"ExamineConfirmMenu"}, std::string_view{"ContainerMenu"}, std::string_view{"BarterMenu"},
    std::string_view{"PowerArmorModMenu"}};
constexpr std::uint32_t kPreNGBSLightingSetupGeometryWorkshopPreviewReason =
    static_cast<std::uint32_t>(kPreNGBSLightingSetupGeometryPreviewMenus.size() + 1);
constexpr std::uint64_t kPreNGBSLightingSetupGeometryPreviewCacheInvalidFrame = static_cast<std::uint64_t>(-1);
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
// Normal-world BSLighting pixel descriptors observed while the clustered
// payload is live. Menu 3D previews only ever exercise the five contract
// descriptors (0x1/0x101/0x111/0x141/0x201); normal world rendering resolves
// the lighting PS through other descriptors, and this set records them so the
// consumer bind can be generalized from the hard-coded contract gate.
std::mutex s_preNGBSLightingWorldDescriptorLock;
std::set<std::uint32_t> s_preNGBSLightingWorldDescriptors;
std::atomic_uint32_t s_preNGBSLightingWorldDescriptorObservations = 0;
std::atomic_uint32_t s_preNGBSLightingWorldBindCount = 0;
#if defined(FALLOUT_PRE_NG)
winrt::com_ptr<ID3D11Buffer> s_preNGDFLightCameraCB;
std::atomic_bool s_preNGDFLightCameraCBCaptured = false;

// The renderer-state base vanilla DFLight reads: TLS[TlsIndex] + 2848
// (falling back to qword_1461DDC68 when the TLS slot is null).
std::uintptr_t GetPreNGDFLightRendererStateBase()
{
    std::uint32_t tlsIndex = 0;
    const RE::FO4Runtime::RuntimeAddressValue kPreNGTlsIndex{ 0x1467347B4 };
    const RE::FO4Runtime::RuntimeAddressValue kPreNGRendererFallback{ 0x1461DDC68 };
    const bool tlsIndexRead = RE::FO4Runtime::ReadValue<std::uint32_t>(kPreNGTlsIndex.address(), tlsIndex);
    const auto teb = __readgsqword(0x30);
    const auto tlsArray = *reinterpret_cast<std::uintptr_t *>(teb + 0x58);
    const auto slot = (tlsArray && tlsIndex < 0x400) ?
        *reinterpret_cast<std::uintptr_t *>(tlsArray + static_cast<std::uintptr_t>(tlsIndex) * 8) : 0;
    const auto base = slot ? *reinterpret_cast<std::uintptr_t *>(slot + 2848) : 0;
    if (!tlsIndexRead)
    {
        return RE::FO4Runtime::ReadPointer(kPreNGRendererFallback.address());
    }
    return base ? base : RE::FO4Runtime::ReadPointer(kPreNGRendererFallback.address());
}
#endif
#endif

std::string GetShaderPath()
{
    return "LightLimitFix\\";
}

bool LogResourceFailure(const char *a_name, HRESULT a_hr)
{
    logger::error("[LightLimitFix] {} failed (hr=0x{:08X})", a_name, static_cast<std::uint32_t>(a_hr));
    return false;
}

bool IsFiniteMatrix(const DirectX::XMFLOAT4X4 &a_matrix)
{
    const auto *values = reinterpret_cast<const float *>(&a_matrix);
    for (std::size_t i = 0; i < 16; ++i)
    {
        if (!std::isfinite(values[i]))
        {
            return false;
        }
    }
    return true;
}
#if defined(FALLOUT_PRE_NG)
bool PreNGClusterBuildInputsMatch(const LightLimitFix::ClusterBuildCacheState &a_cached,
                                  const LightLimitFix::LightBuildingCB &a_current)
{
    if (std::fabs(a_cached.LightsNear - a_current.LightsNear) > kPreNGClusterBuildReuseTolerance ||
        std::fabs(a_cached.LightsFar - a_current.LightsFar) > kPreNGClusterBuildReuseTolerance)
    {
        return false;
    }

    for (std::uint32_t i = 0; i < 4; ++i)
    {
        if (a_cached.ClusterSize[i] != a_current.ClusterSize[i])
        {
            return false;
        }
    }

    // Projection jitter can change the inverse matrix every frame. For the
    // targeted DFLight refresh path, reuse built AABBs while near/far and
    // grid size are stable; SetupResources invalidates resource changes.
    return true;
}

constexpr std::uint64_t kPreNGFNVOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kPreNGFNVPrime = 1099511628211ull;

void HashPreNGAppendBytes(std::uint64_t &a_hash, const void *a_data, std::size_t a_size)
{
    const auto *bytes = static_cast<const std::uint8_t *>(a_data);
    for (std::size_t i = 0; i < a_size; ++i)
    {
        a_hash ^= bytes[i];
        a_hash *= kPreNGFNVPrime;
    }
}

std::uint64_t HashPreNGBytes(const void *a_data, std::size_t a_size)
{
    auto hash = kPreNGFNVOffsetBasis;
    HashPreNGAppendBytes(hash, a_data, a_size);
    return hash;
}

LightLimitFix::ClusterPayloadCacheState MakePreNGClusterPayloadCacheState(
    const std::vector<LightLimitFix::LightData> &a_lights, std::uint32_t a_lightCount,
    const DirectX::XMFLOAT4X4 &a_viewTransposed,
    float a_lightsNear, float a_lightsFar, const std::uint32_t (&a_clusterSize)[3])
{
    LightLimitFix::ClusterPayloadCacheState state{};
    state.LightsNear = a_lightsNear;
    state.LightsFar = a_lightsFar;
    state.ClusterSize[0] = a_clusterSize[0];
    state.ClusterSize[1] = a_clusterSize[1];
    state.ClusterSize[2] = a_clusterSize[2];
    state.ClusterSize[3] = 0;
    state.LightCount = a_lightCount;
    state.LightsHash = a_lights.empty()
                           ? kPreNGFNVOffsetBasis
                           : HashPreNGBytes(a_lights.data(), a_lights.size() * sizeof(LightLimitFix::LightData));
    state.ViewHash = HashPreNGBytes(&a_viewTransposed, sizeof(a_viewTransposed));
    return state;
}

namespace F4Runtime = RE::FO4Runtime;

constexpr std::uint32_t kPreNGBSRenderPassSceneLightFirstIndex =
    F4Runtime::PreNG::BS_RENDER_PASS_SCENE_LIGHT_FIRST_INDEX;
constexpr std::uint32_t kPreNGInvalidShadowLightMaskIndex = F4Runtime::PreNG::INVALID_SHADOW_LIGHT_MASK_INDEX;
constexpr std::uint32_t kPreNGMaxShadowLightMaskBits = F4Runtime::PreNG::MAX_SHADOW_LIGHT_MASK_BITS;
constexpr std::uint32_t kPreNGMaxShadowSceneActiveLights = 8192;
constexpr std::uint32_t kPreNGMaxShadowSceneDecodeLights = kMaxLights;
constexpr float kPreNGLightContributionThreshold = 1.0e-4f;
constexpr float kPreNGLightRadiusThreshold = 1.0e-4f;

bool SamePreNGShadowSceneFastReuseKey(const LightLimitFix::ShadowSceneFastReuseKey &a_lhs,
                                      const LightLimitFix::ShadowSceneFastReuseKey &a_rhs)
{
    return a_lhs.Node == a_rhs.Node && a_lhs.SelectedIndex == a_rhs.SelectedIndex &&
           a_lhs.CurrentIndex == a_rhs.CurrentIndex && a_lhs.CurrentIndexRead == a_rhs.CurrentIndexRead &&
           a_lhs.UsedFallback == a_rhs.UsedFallback && a_lhs.ActiveEntries == a_rhs.ActiveEntries &&
           a_lhs.ShadowEntries == a_rhs.ShadowEntries && a_lhs.ExtraEntries == a_rhs.ExtraEntries &&
           a_lhs.ActiveCount == a_rhs.ActiveCount && a_lhs.ShadowCount == a_rhs.ShadowCount &&
           a_lhs.ExtraCount == a_rhs.ExtraCount && a_lhs.ActiveHash == a_rhs.ActiveHash &&
           a_lhs.ShadowHash == a_rhs.ShadowHash && a_lhs.ExtraHash == a_rhs.ExtraHash;
}

// Structural (pointer/hash-independent) fast-reuse comparison. The per-light
// wrapper pointers folded into ActiveHash/ShadowHash/ExtraHash churn every
// frame even for a static light set (the engine reorders/reallocates the
// wrapper array), so the strict SamePreNGShadowSceneFastReuseKey never
// matches for dense scenes and the decode re-runs every frame (sub-1-FPS in
// 1000+ light interiors). This weaker key matches when the same node exposes
// the same bucket layout and light counts, which together with a hard
// ReuseAge < refreshInterval gate bounds full re-decodes to once per
// refresh interval regardless of pointer churn.
bool SamePreNGShadowSceneFastReuseStructure(const LightLimitFix::ShadowSceneFastReuseKey &a_lhs,
                                            const LightLimitFix::ShadowSceneFastReuseKey &a_rhs)
{
    return a_lhs.Node == a_rhs.Node && a_lhs.SelectedIndex == a_rhs.SelectedIndex &&
           a_lhs.CurrentIndex == a_rhs.CurrentIndex && a_lhs.CurrentIndexRead == a_rhs.CurrentIndexRead &&
           a_lhs.UsedFallback == a_rhs.UsedFallback && a_lhs.ActiveEntries == a_rhs.ActiveEntries &&
           a_lhs.ShadowEntries == a_rhs.ShadowEntries && a_lhs.ExtraEntries == a_rhs.ExtraEntries &&
           a_lhs.ActiveCount == a_rhs.ActiveCount && a_lhs.ShadowCount == a_rhs.ShadowCount &&
           a_lhs.ExtraCount == a_rhs.ExtraCount;
}

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

bool ReadPreNGShadowSceneBucketHash(const F4Runtime::PreNGShadowSceneBucket &a_bucket, std::uint64_t &a_hash)
{
    a_hash = kPreNGFNVOffsetBasis;
    HashPreNGAppendBytes(a_hash, &a_bucket.entries, sizeof(a_bucket.entries));
    HashPreNGAppendBytes(a_hash, &a_bucket.count, sizeof(a_bucket.count));
    for (std::uint32_t i = 0; i < a_bucket.count; ++i)
    {
        std::uintptr_t wrapperAddress = 0;
        const auto entryAddress = a_bucket.entries + (static_cast<std::uintptr_t>(i) * sizeof(std::uintptr_t));
        ReadPreNGRaw(entryAddress, wrapperAddress);
        HashPreNGAppendBytes(a_hash, &wrapperAddress, sizeof(wrapperAddress));
    }
    return true;
}

bool MakePreNGShadowSceneFastReuseKey(const F4Runtime::PreNGShadowSceneNodeRef &a_nodeRef,
                                      const F4Runtime::PreNGShadowSceneBuckets &a_buckets,
                                      LightLimitFix::ShadowSceneFastReuseKey &a_key)
{
    LightLimitFix::ShadowSceneFastReuseKey key{};
    key.Node = a_nodeRef.node;
    key.SelectedIndex = a_nodeRef.selectedIndex;
    key.CurrentIndex = a_nodeRef.currentIndex;
    key.CurrentIndexRead = a_nodeRef.currentIndexRead;
    key.UsedFallback = a_nodeRef.usedFallback;
    key.ActiveEntries = a_buckets.active.entries;
    key.ShadowEntries = a_buckets.shadow.entries;
    key.ExtraEntries = a_buckets.extra.entries;
    key.ActiveCount = a_buckets.active.count;
    key.ShadowCount = a_buckets.shadow.count;
    key.ExtraCount = a_buckets.extra.count;
    if (!ReadPreNGShadowSceneBucketHash(a_buckets.active, key.ActiveHash) ||
        !ReadPreNGShadowSceneBucketHash(a_buckets.shadow, key.ShadowHash) ||
        !ReadPreNGShadowSceneBucketHash(a_buckets.extra, key.ExtraHash))
    {
        return false;
    }

    a_key = key;
    return true;
}

using PreNGPixelShaderEntryState = F4Runtime::PreNGShaderEntryState;

PreNGPixelShaderEntryState ReadPreNGCurrentPixelShaderEntryState()
{
    return F4Runtime::ReadPreNGPixelShaderEntryState();
}

bool HasPreNGConstantBufferSlot(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata, std::uint32_t a_slot)
{
    return a_slot < a_metadata.constantBufferSizes.size() && a_metadata.constantBufferSizes[a_slot] != 0;
}

bool HasPreNGTextureSlot(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata, std::uint32_t a_slot)
{
    return std::find(a_metadata.textureSlots.begin(), a_metadata.textureSlots.end(), a_slot) !=
           a_metadata.textureSlots.end();
}

std::uint32_t GetPreNGTextureSampleCount(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata,
                                         std::uint32_t a_slot)
{
    return a_slot < a_metadata.textureSampleCounts.size() ? a_metadata.textureSampleCounts[a_slot] : 0;
}

std::string FormatPreNGShaderBufferSlots(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata)
{
    std::ostringstream result;
    bool first = true;
    for (std::size_t slot = 0; slot < a_metadata.constantBufferSizes.size(); ++slot)
    {
        const auto size = a_metadata.constantBufferSizes[slot];
        if (size == 0)
        {
            continue;
        }
        if (!first)
        {
            result << ',';
        }
        result << slot << ':' << size;
        first = false;
    }
    return first ? "none" : result.str();
}

std::string FormatPreNGShaderTextureSlots(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata)
{
    std::ostringstream result;
    for (std::size_t index = 0; index < a_metadata.textureSlots.size(); ++index)
    {
        if (index > 0)
        {
            result << ',';
        }
        result << a_metadata.textureSlots[index];
    }
    return a_metadata.textureSlots.empty() ? "none" : result.str();
}

std::string FormatPreNGShaderTextureSampleCounts(const CommunityShaders::ShaderCache::ShaderMetadata &a_metadata)
{
    std::ostringstream result;
    bool first = true;
    for (std::size_t slot = 0; slot < a_metadata.textureSampleCounts.size(); ++slot)
    {
        const auto count = a_metadata.textureSampleCounts[slot];
        if (count == 0)
        {
            continue;
        }
        if (!first)
        {
            result << ',';
        }
        result << slot << ':' << count;
        first = false;
    }
    return first ? "none" : result.str();
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

PreNGShaderSlotEvidence GetPreNGShaderSlotEvidence(
    const std::optional<CommunityShaders::ShaderCache::ShaderMetadata> &a_metadata)
{
    PreNGShaderSlotEvidence result{};
    if (!a_metadata)
    {
        return result;
    }

    result.hasMetadata = true;
    result.declaresCB3 = HasPreNGConstantBufferSlot(*a_metadata, 3);
    result.declaresT35 = HasPreNGTextureSlot(*a_metadata, 35);
    result.declaresT36 = HasPreNGTextureSlot(*a_metadata, 36);
    result.declaresT37 = HasPreNGTextureSlot(*a_metadata, 37);
    result.samplesT35 = GetPreNGTextureSampleCount(*a_metadata, 35);
    result.samplesT36 = GetPreNGTextureSampleCount(*a_metadata, 36);
    result.samplesT37 = GetPreNGTextureSampleCount(*a_metadata, 37);
    return result;
}

bool HasPreNGFullShadowedDFLightVanillaContract(
    const std::optional<CommunityShaders::ShaderCache::ShaderMetadata> &a_metadata)
{
    return a_metadata && a_metadata->constantBufferSizes[2] == 448 && a_metadata->constantBufferSizes[12] == 496 &&
           HasPreNGTextureSlot(*a_metadata, 0) && HasPreNGTextureSlot(*a_metadata, 1) &&
           HasPreNGTextureSlot(*a_metadata, 2) && HasPreNGTextureSlot(*a_metadata, 3) &&
           HasPreNGTextureSlot(*a_metadata, 5) && GetPreNGTextureSampleCount(*a_metadata, 0) == 1 &&
           GetPreNGTextureSampleCount(*a_metadata, 1) == 1 && GetPreNGTextureSampleCount(*a_metadata, 2) == 1 &&
           GetPreNGTextureSampleCount(*a_metadata, 3) == 1 && GetPreNGTextureSampleCount(*a_metadata, 5) == 6;
}

std::string FormatPreNGShaderMetadata(const std::optional<CommunityShaders::ShaderCache::ShaderMetadata> &a_metadata)
{
    if (!a_metadata)
    {
        return "missing";
    }

    return std::format("uid={} asm=0x{:08X} hash=0x{:08X} size={} buffers={} textures={} textureSamples={}",
                       a_metadata->uid, a_metadata->asmHash, a_metadata->hash, a_metadata->size,
                       FormatPreNGShaderBufferSlots(*a_metadata), FormatPreNGShaderTextureSlots(*a_metadata),
                       FormatPreNGShaderTextureSampleCounts(*a_metadata));
}

using PreNGShadowSceneNodeRef = F4Runtime::PreNGShadowSceneNodeRef;

PreNGShadowSceneNodeRef GetPreNGWorldShadowSceneNode()
{
    return F4Runtime::GetPreNGWorldShadowSceneNode();
}

enum class PreNGLightDecodeResult
{
    Decoded,
    MissingWrapperData,
    InvalidNiLightData,
    NonContributingLightData
};

PreNGLightDecodeResult DecodePreNGBSLightWrapper(std::uintptr_t a_wrapperAddress, LightLimitFix::LightData &a_data,
                                                 std::uintptr_t &a_niLightAddress, bool &a_shadowMaskUnreadable,
                                                 bool &a_shadowMaskInvalid, std::uint32_t &a_shadowMaskBit)
{
    a_data = {};
    a_niLightAddress = 0;
    a_shadowMaskUnreadable = false;
    a_shadowMaskInvalid = false;
    a_shadowMaskBit = 0;

    float wrapperFade = 1.0f;
    if (a_wrapperAddress == 0 ||
        !ReadPreNGRawField(F4Runtime::PreNG::BS_LIGHT_WRAPPER_FADE, a_wrapperAddress, wrapperFade) ||
        !ReadPreNGRawField(F4Runtime::PreNG::BS_LIGHT_WRAPPER_NI_LIGHT, a_wrapperAddress, a_niLightAddress) ||
        a_niLightAddress == 0 || !std::isfinite(wrapperFade))
    {
        return PreNGLightDecodeResult::MissingWrapperData;
    }

    F4Runtime::PreNGNiLightData niLight{};
    const bool niLightRead =
        ReadPreNGRawField(F4Runtime::PreNG::NI_LIGHT_DIFFUSE, a_niLightAddress, niLight.diffuse[0]) &&
        ReadPreNGRaw(F4Runtime::PreNG::NI_LIGHT_DIFFUSE.address(a_niLightAddress) + sizeof(float), niLight.diffuse[1]) &&
        ReadPreNGRaw(F4Runtime::PreNG::NI_LIGHT_DIFFUSE.address(a_niLightAddress) + (2 * sizeof(float)), niLight.diffuse[2]) &&
        ReadPreNGRawField(F4Runtime::PreNG::NI_LIGHT_RADIUS, a_niLightAddress, niLight.radius) &&
        ReadPreNGRawField(F4Runtime::PreNG::NI_LIGHT_DIMMER, a_niLightAddress, niLight.dimmer) &&
        ReadPreNGRawField(F4Runtime::PreNG::NI_LIGHT_WORLD_TRANSLATE, a_niLightAddress, niLight.position[0]) &&
        ReadPreNGRaw(F4Runtime::PreNG::NI_LIGHT_WORLD_TRANSLATE.address(a_niLightAddress) + sizeof(float), niLight.position[1]) &&
        ReadPreNGRaw(F4Runtime::PreNG::NI_LIGHT_WORLD_TRANSLATE.address(a_niLightAddress) + (2 * sizeof(float)), niLight.position[2]);
    if (!niLightRead || !std::isfinite(niLight.diffuse[0]) || !std::isfinite(niLight.diffuse[1]) ||
        !std::isfinite(niLight.diffuse[2]) || !std::isfinite(niLight.radius) || !std::isfinite(niLight.dimmer) ||
        !std::isfinite(niLight.position[0]) || !std::isfinite(niLight.position[1]) ||
        !std::isfinite(niLight.position[2]) || niLight.radius <= 0.0f)
    {
        return PreNGLightDecodeResult::InvalidNiLightData;
    }

    const float fade = niLight.dimmer * wrapperFade;
    const float contribution = (niLight.diffuse[0] + niLight.diffuse[1] + niLight.diffuse[2]) * fade;
    if (niLight.radius <= kPreNGLightRadiusThreshold || contribution <= kPreNGLightContributionThreshold)
    {
        return PreNGLightDecodeResult::NonContributingLightData;
    }

    a_data.color.x = niLight.diffuse[0];
    a_data.color.y = niLight.diffuse[1];
    a_data.color.z = niLight.diffuse[2];
    a_data.fade = fade;
    a_data.radius = niLight.radius;
    a_data.invRadius = a_data.radius > 0.0f ? 1.0f / a_data.radius : 0.0f;
    a_data.positionWS[0].data.x = niLight.position[0];
    a_data.positionWS[0].data.y = niLight.position[1];
    a_data.positionWS[0].data.z = niLight.position[2];
    a_data.lightFlags = static_cast<std::uint32_t>(LightLimitFix::LightFlags::Initialised);

    std::uint32_t shadowMaskIndex = kPreNGInvalidShadowLightMaskIndex;
    if (ReadPreNGRawField(F4Runtime::PreNG::BS_SHADOW_LIGHT_MASK_INDEX, a_wrapperAddress, shadowMaskIndex))
    {
        if (shadowMaskIndex != kPreNGInvalidShadowLightMaskIndex && shadowMaskIndex < kPreNGMaxShadowLightMaskBits)
        {
            a_data.lightFlags |= static_cast<std::uint32_t>(LightLimitFix::LightFlags::Shadow);
            a_data.shadowLightIndex = shadowMaskIndex;
            a_shadowMaskBit = (1u << shadowMaskIndex);
        }
        else if (shadowMaskIndex != kPreNGInvalidShadowLightMaskIndex)
        {
            a_shadowMaskInvalid = true;
        }
    }
    else
    {
        a_shadowMaskUnreadable = true;
    }

    return PreNGLightDecodeResult::Decoded;
}

bool ValidatePreNGPointLightCallsite()
{
    const auto validation = F4Runtime::ValidatePreNGPointLightCallsite();
    if (!validation.vfuncReadable || !validation.callContextReadable)
    {
        logger::warn("[LightLimitFix] PreNG point-light callsite validation skipped: unreadable memory base=0x{:X} "
                     "vfuncReadable={} callReadable={} vfunc[7]=0x{:X} callContext=0x{:X}",
                     validation.imageBase, validation.vfuncReadable, validation.callContextReadable,
                     validation.vfuncEntry, validation.callContext);
        return false;
    }

    if (validation.matches)
    {
        logger::info("[LightLimitFix] PreNG point-light callsite validated base=0x{:X} setup=0x{:X} call=0x{:X} "
                     "target=0x{:X} vtable=0x{:X} vfunc[7]=0x{:X}->0x{:X}",
                     validation.imageBase, validation.setup, validation.call, validation.callPatch.callTarget,
                     validation.vtable, validation.vfuncEntry, validation.observedVFunc);
        return true;
    }

    logger::warn(
        "[LightLimitFix] PreNG point-light callsite mismatch base=0x{:X} vfunc[7]=0x{:X}->0x{:X} expectedSetup=0x{:X} "
        "call=0x{:X} opcode=0x{:02X} rel32=0x{:08X} observedTarget=0x{:X} expectedTarget=0x{:X} contextMatch={}",
        validation.imageBase, validation.vfuncEntry, validation.observedVFunc, validation.setup, validation.call,
        static_cast<std::uint32_t>(validation.callPatch.opcode), static_cast<std::uint32_t>(validation.callPatch.rel32),
        validation.callPatch.callTarget, validation.target, validation.contextMatches);
    return false;
}

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

const char *EnvironmentSwitchSourceName(EnvironmentSwitchSource a_source)
{
    switch (a_source)
    {
    case EnvironmentSwitchSource::kDebugIni:
        return "debug-ini";
    default:
        return "none";
    }
}

EnvironmentSwitchSource ToEnvironmentSwitchSource(CommunityShaders::DebugSwitches::Source a_source)
{
    return a_source == CommunityShaders::DebugSwitches::Source::kDebugIni ? EnvironmentSwitchSource::kDebugIni
                                                                          : EnvironmentSwitchSource::kNone;
}

EnvironmentSwitchState ReadEnvironmentSwitch(const char *a_name)
{
    const auto state = CommunityShaders::DebugSwitches::ReadSwitch(a_name);
    return {state.enabled, ToEnvironmentSwitchSource(state.source)};
}

EnvironmentUIntState ReadEnvironmentUInt(const char *a_name)
{
    const auto state = CommunityShaders::DebugSwitches::ReadUInt(a_name);
    return {state.value, ToEnvironmentSwitchSource(state.source), state.present, state.valid};
}

std::uint32_t GetPreNGDFLightLLFAdditiveRefreshInterval()
{
    static const std::uint32_t interval = [] {
        const auto state = ReadEnvironmentUInt(kPreNGDFLightLLFAdditiveRefreshIntervalEnv);
        auto resolved = kPreNGDefaultDFLightLLFAdditiveRefreshInterval;
        auto clamped = false;
        if (state.present && state.valid)
        {
            const auto requested = state.value;
            resolved = std::clamp(requested, kPreNGMinDFLightLLFAdditiveRefreshInterval,
                                  kPreNGMaxDFLightLLFAdditiveRefreshInterval);
            clamped = resolved != requested;
        }

        logger::info("[LightLimitFix] PreNG DFLight LLF additive Prepass refresh interval resolved {}={} source={} "
                     "present={} valid={} clamped={} default={} range={}..{}",
                     kPreNGDFLightLLFAdditiveRefreshIntervalEnv, resolved, EnvironmentSwitchSourceName(state.source),
                     state.present, state.valid, clamped, kPreNGDefaultDFLightLLFAdditiveRefreshInterval,
                     kPreNGMinDFLightLLFAdditiveRefreshInterval, kPreNGMaxDFLightLLFAdditiveRefreshInterval);
        return resolved;
    }();
    return interval;
}

std::optional<std::uint32_t> GetPreNGSetupGeometryCallBudget()
{
    static const std::optional<std::uint32_t> budget = []() -> std::optional<std::uint32_t> {
        const auto state = ReadEnvironmentUInt(kPreNGSetupGeometryCallBudgetEnv);
        if (!state.present)
        {
            return std::nullopt;
        }
        if (!state.valid)
        {
            logger::warn("[LightLimitFix] PreNG SetupGeometry call budget ignored {} source={} present={} valid=false",
                         kPreNGSetupGeometryCallBudgetEnv, EnvironmentSwitchSourceName(state.source), state.present);
            return std::nullopt;
        }

        const auto resolved = std::min(state.value, kPreNGMaxSetupGeometryCallBudget);
        logger::info("[LightLimitFix] PreNG SetupGeometry call budget resolved {}={} source={} requested={} max={}",
                     kPreNGSetupGeometryCallBudgetEnv, resolved, EnvironmentSwitchSourceName(state.source), state.value,
                     kPreNGMaxSetupGeometryCallBudget);
        return resolved;
    }();
    return budget;
}

std::uint32_t GetPreNGSetupGeometryFrameBudget()
{
    static const std::uint32_t budget = [] {
        const auto state = ReadEnvironmentUInt(kPreNGSetupGeometryFrameBudgetEnv);
        if (!state.present)
        {
            logger::info("[LightLimitFix] PreNG SetupGeometry frame budget default {}={} max={} source=default",
                         kPreNGSetupGeometryFrameBudgetEnv, kPreNGDefaultSetupGeometryFrameBudget,
                         kPreNGMaxSetupGeometryFrameBudget);
            return kPreNGDefaultSetupGeometryFrameBudget;
        }
        if (!state.valid)
        {
            logger::warn("[LightLimitFix] PreNG SetupGeometry frame budget invalid; using default {}={} source={}",
                         kPreNGSetupGeometryFrameBudgetEnv, kPreNGDefaultSetupGeometryFrameBudget,
                         EnvironmentSwitchSourceName(state.source));
            return kPreNGDefaultSetupGeometryFrameBudget;
        }
        const auto resolved = std::min(state.value, kPreNGMaxSetupGeometryFrameBudget);
        logger::info("[LightLimitFix] PreNG SetupGeometry frame budget resolved {}={} source={} requested={} max={}",
                     kPreNGSetupGeometryFrameBudgetEnv, resolved, EnvironmentSwitchSourceName(state.source),
                     state.value, kPreNGMaxSetupGeometryFrameBudget);
        return resolved;
    }();
    return budget;
}

bool TryReservePreNGSetupGeometryCall()
{
    const auto budget = GetPreNGSetupGeometryCallBudget();
    if (!budget)
    {
        return true;
    }

    static std::atomic_uint32_t setupGeometryBudgetedCalls = 0;
    static std::atomic_bool setupGeometryBudgetLogged = false;
    const auto callIndex = setupGeometryBudgetedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (callIndex <= *budget)
    {
        return true;
    }

    if (!setupGeometryBudgetLogged.exchange(true, std::memory_order_relaxed))
    {
        logger::info("[LightLimitFix] PreNG SetupGeometry call budget reached; holding scene-light decode/upload after "
                     "calls={} budget={} env={}",
                     callIndex - 1, *budget, kPreNGSetupGeometryCallBudgetEnv);
    }
    return false;
}

bool TryReservePreNGSetupGeometryFrameSample()
{
    const auto budget = GetPreNGSetupGeometryFrameBudget();
    if (budget == 0)
    {
        return false;
    }

    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    const auto frame = runtime ? runtime->GetFrameCount() : 0;
    static std::uint64_t sampledFrame = static_cast<std::uint64_t>(-1);
    static std::uint32_t sampledThisFrame = 0;
    if (sampledFrame != frame)
    {
        sampledFrame = frame;
        sampledThisFrame = 0;
    }

    ++sampledThisFrame;
    if (sampledThisFrame <= budget)
    {
        return true;
    }

    static std::atomic_uint32_t skippedSamples = 0;
    const auto skipped = ++skippedSamples;
    if (skipped <= 8 || skipped % 4096 == 0)
    {
        logger::info("[LightLimitFix] PreNG SetupGeometry frame budget held samples={} frame={} accepted={} budget={}",
                     skipped, frame, budget, budget);
    }
    return false;
}

bool TryReservePreNGBSLightingSetupGeometryNoLightProbeFrame()
{
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    const auto frame = runtime ? runtime->GetFrameCount() : 0;
    auto nextFrame = s_preNGBSLightingSetupGeometryNoLightNextProbeFrame.load(std::memory_order_relaxed);
    while (frame >= nextFrame)
    {
        if (s_preNGBSLightingSetupGeometryNoLightNextProbeFrame.compare_exchange_weak(
                nextFrame, frame + 1, std::memory_order_relaxed, std::memory_order_relaxed))
        {
            return true;
        }
    }
    return false;
}

void ExtendPreNGBSLightingSetupGeometryBypassWindow()
{
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    if (!runtime)
    {
        return;
    }

    const auto frame = runtime->GetFrameCount();
    const auto newBypassUntil = frame + kPreNGBSLightingSetupGeometryNoLightBypassFrames;
    auto bypassUntil = s_preNGBSLightingSetupGeometryBypassUntilFrame.load(std::memory_order_relaxed);
    while (bypassUntil < newBypassUntil)
    {
        if (s_preNGBSLightingSetupGeometryBypassUntilFrame.compare_exchange_weak(
                bypassUntil, newBypassUntil, std::memory_order_relaxed, std::memory_order_relaxed))
        {
            return;
        }
    }
}

bool IsTruthyEnvironmentSwitch(const char *a_name);

std::string_view DetectPreNGBSLightingResourceProofMenuBlock()
{
    auto *ui = RE::UI::GetSingleton();
    if (!ui)
    {
        return {};
    }

    for (std::uint32_t i = 0; i < kPreNGBSLightingResourceProofBlockingMenus.size(); ++i)
    {
        const auto menu = kPreNGBSLightingResourceProofBlockingMenus[i];
        if (ui->GetMenuOpen(menu.data()))
        {
            s_preNGBSLightingPreviewMenuLastReason.store(i + 1, std::memory_order_relaxed);
            return menu;
        }
    }

    return {};
}

// Cheap O(1) probe of the active world ShadowSceneNode bucket totals — reads
// only the bucket count fields, never walks/decodes the light pointers. Used
// by the resume gate so it can tell whether the dense preview-menu scene has
// drained without depending on the (proof-gated) full decode updating the
// count, which would deadlock the gate.
std::uint32_t ProbePreNGShadowSceneBucketTotal()
{
    const auto shadowSceneNodeRef = GetPreNGWorldShadowSceneNode();
    if (shadowSceneNodeRef.node == 0)
    {
        return 0;
    }
    F4Runtime::PreNGShadowSceneBuckets buckets{};
    const F4Runtime::PreNGShadowSceneNodeView shadowSceneView{shadowSceneNodeRef.node};
    if (!shadowSceneView.ReadBuckets(buckets))
    {
        return 0;
    }
    const std::uint64_t total = static_cast<std::uint64_t>(buckets.active.count) +
                                static_cast<std::uint64_t>(buckets.shadow.count) +
                                static_cast<std::uint64_t>(buckets.extra.count);
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(total, 0xFFFFFFFFull));
}

std::string_view GetPreNGBSLightingLastPreviewMenuReason()
{
    const auto reason = s_preNGBSLightingPreviewMenuLastReason.load(std::memory_order_relaxed);
    if (reason > 0 && reason <= kPreNGBSLightingResourceProofBlockingMenus.size())
    {
        return kPreNGBSLightingResourceProofBlockingMenus[reason - 1];
    }
    return "unknown-preview-menu";
}

bool ShouldDeferPreNGBSLightingResourceProofForMenu()
{
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    const auto frame = runtime ? runtime->GetFrameCount() : 0;
    const auto menuBlock = DetectPreNGBSLightingResourceProofMenuBlock();

    auto logDefer = [&](const char *a_reason, std::uint64_t a_until) {
        const auto deferralIndex = ++s_preNGBSLightingResourceProofBypassLogs;
        if (deferralIndex <= 8 || (deferralIndex & (deferralIndex - 1)) == 0)
        {
            logger::info(
                "[LightLimitFix] PreNG BSLighting resource proof deferred for UI menu/settle deferrals={} frame={} "
                "until={} reason={} settleFrames={}; clustered Prepass and deferred b3/t35-t37 bind stay held",
                deferralIndex, frame, a_until, a_reason, kPreNGBSLightingResourceProofMenuSettleFrames);
        }
    };

    if (!menuBlock.empty())
    {
        s_preNGBSLightingPreviewMenuResumePending.store(true, std::memory_order_relaxed);
        // Scoped suppression: hold the clustered Prepass / deferred b3-t35-t37
        // bind only while a fullscreen preview menu (Lockpicking/Examine) is
        // actually open. Detection is per-frame and live, so the moment the
        // menu closes this returns false again and the prepass resumes
        // immediately — no post-close settle delay (BOSS: resume clustered
        // prepass right after the menu closes). The separate descriptor-burst
        // path (ExtendPreNGBSLightingResourceProofDescriptorSettle) still uses
        // bypassUntil for its own small buffer. See
        // .codex/docs/preview-menu-prepass-suppression.md.
        logDefer(menuBlock.data(), frame);
        return true;
    }

    // Resume gate: a preview menu may have just closed, but the engine can
    // keep the dense world ShadowSceneNode selected for a few frames before
    // the scene returns to its normal light count. Resuming the clustered
    // prepass during that overload window is what caused the residual
    // post-close stutter. Hold until the raw bucket total drops back to a
    // normal scene size. Uses a cheap live probe (not the proof-gated decode
    // output) so the gate can never deadlock itself. This gates resume on
    // scene state, not a timer, and is not a permanent light cap.
    const auto liveBucketTotal = ProbePreNGShadowSceneBucketTotal();
    s_preNGShadowSceneLastBucketTotal.store(liveBucketTotal, std::memory_order_relaxed);
    static const bool disableOverloadGate = IsTruthyEnvironmentSwitch(kPreNGDisablePreviewOverloadGateEnv);
    if (!disableOverloadGate && liveBucketTotal >= kPreNGShadowScenePreviewOverloadLights)
    {
        logDefer("post-menu-light-overload", frame);
        return true;
    }

    if (runtime)
    {
        const auto bypassUntil = s_preNGBSLightingResourceProofBypassUntilFrame.load(std::memory_order_relaxed);
        if (frame < bypassUntil)
        {
            logDefer("post-BSLighting-resource-settle", bypassUntil);
            return true;
        }
    }

    return false;
}

void ExtendPreNGBSLightingResourceProofDescriptorSettle()
{
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    if (!runtime)
    {
        return;
    }

    const auto frame = runtime->GetFrameCount();
    const auto newBypassUntil = frame + kPreNGBSLightingResourceProofMenuSettleFrames;
    auto bypassUntil = s_preNGBSLightingResourceProofBypassUntilFrame.load(std::memory_order_relaxed);
    while (bypassUntil < newBypassUntil)
    {
        if (s_preNGBSLightingResourceProofBypassUntilFrame.compare_exchange_weak(
                bypassUntil, newBypassUntil, std::memory_order_relaxed, std::memory_order_relaxed))
        {
            break;
        }
    }

    const auto deferralIndex = ++s_preNGBSLightingResourceProofBypassLogs;
    if (deferralIndex <= 8 || (deferralIndex & (deferralIndex - 1)) == 0)
    {
        logger::info("[LightLimitFix] PreNG BSLighting resource proof deferred after descriptor burst deferrals={} "
                     "frame={} until={} settleFrames={}; clustered Prepass and deferred b3/t35-t37 bind stay held",
                     deferralIndex, frame, newBypassUntil, kPreNGBSLightingResourceProofMenuSettleFrames);
    }
}

bool IsTruthyEnvironmentSwitch(const char *a_name)
{
    return ReadEnvironmentSwitch(a_name).enabled;
}

void LogPreNGDiagnosticEnvironmentSnapshot()
{
    static bool logged = false;
    if (logged)
    {
        return;
    }
    logged = true;

    const auto setupGeometryHookState = ReadEnvironmentSwitch(kPreNGSetupGeometryHookOptInEnv);
    const auto setupGeometryBindCBState = ReadEnvironmentSwitch(kPreNGSetupGeometryStrictCBBindEnv);
    const auto setupGeometryPersistCBState = ReadEnvironmentSwitch(kPreNGSetupGeometryPersistStrictCBEnv);
    const auto hookState = ReadEnvironmentSwitch(kPreNGPointLightHookOptInEnv);
    const auto strictCBState = ReadEnvironmentSwitch(kPreNGStrictLightCBDiagnosticEnv);
    const auto bindCBState = ReadEnvironmentSwitch(kPreNGStrictLightCBBindEnv);
    const auto bindClusterSRVState = ReadEnvironmentSwitch(kPreNGClusterSRVBindEnv);
    const auto persistentClusterPrepassState = ReadEnvironmentSwitch(kPreNGPersistentClusterPrepassEnv);
    const auto dflightDrawStateStrictCBBindState = ReadEnvironmentSwitch(kPreNGDFLightDrawStateStrictCBBindEnv);
    const auto dflightDrawStateClusterSRVBindState = ReadEnvironmentSwitch(kPreNGDFLightDrawStateClusterSRVBindEnv);
    const auto dflightResourceNoOpPassState = ReadEnvironmentSwitch(kPreNGDFLightResourceNoOpPassEnv);
    const auto dflightFullContractNoOpPassState = ReadEnvironmentSwitch(kPreNGDFLightFullContractNoOpPassEnv);
    const auto dflightLLFAdditivePassState = ReadEnvironmentSwitch(kPreNGDFLightLLFAdditivePassEnv);
    const auto dflightLegacyAdditiveProofState = ReadEnvironmentSwitch(kPreNGDFLightLegacyAdditiveProofEnv);
    const auto dflightLLFAdditivePersistentState = ReadEnvironmentSwitch(kPreNGDFLightLLFAdditivePersistentEnv);
    const auto dfCompositeResourceBindState = ReadEnvironmentSwitch(kPreNGDFCompositeResourceBindEnv);
    const auto dfCompositeSafeBindState = ReadEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
    const auto dfCompositeVisibleLLFState = ReadEnvironmentSwitch(kPreNGDFCompositeVisibleLLFEnv);
    const auto bsLightingResourceBindState = ReadEnvironmentSwitch(kPreNGBSLightingResourceBindEnv);
    const auto bsLightingSetupGeometryResourceBindState =
        ReadEnvironmentSwitch(kPreNGBSLightingSetupGeometryResourceBindEnv);
    const auto bsLightingContractCompileState = ReadEnvironmentSwitch(kPreNGBSLightingContractCompileEnv);
    const auto bsLightingConsumerCompileState = ReadEnvironmentSwitch(kPreNGBSLightingConsumerCompileEnv);
    const auto bsLightingDescriptorObserveState = ReadEnvironmentSwitch(kPreNGBSLightingDescriptorObserveEnv);
    const auto bsLightingVanillaBindState = ReadEnvironmentSwitch(kPreNGBSLightingVanillaBindEnv);
    const auto bsLightingLLFBindState = ReadEnvironmentSwitch(kPreNGBSLightingLLFBindEnv);
    const auto shaderObjectMetadataState = ReadEnvironmentSwitch(kPreNGShaderObjectMetadataEnv);
    const auto tracePSState = ReadEnvironmentSwitch(kPreNGTraceLLFPixelEnv);
    const auto dflightContractCompileState = ReadEnvironmentSwitch(kPreNGDFLightContractCompileEnv);
    const auto dflightCandidateCompileState = ReadEnvironmentSwitch(kPreNGDFLightCandidateCompileEnv);

    logger::info(
        "[LightLimitFix] PreNG diagnostic env snapshot {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} "
        "{}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} sources "
        "setupGeometryHook={} setupGeometryBindCB={} setupGeometryPersistCB={} hook={} strictCB={} bindCB={} "
        "bindSRV={} persistentPrepass={} dflightDrawStateStrictCB={} dflightDrawStateClusterSRV={} "
        "dflightResourceNoOp={} dflightFullContractNoOp={} dflightLLFAdditive={} dflightLegacyAdditiveProof={} "
        "dflightLLFAdditivePersistent={} dfCompositeResourceBind={} dfCompositeSafeBind={} dfCompositeVisibleLLF={} "
        "bsLightingResourceBind={} bsLightingSetupGeometryResourceBind={} shaderObjectMetadata={} tracePS={} "
        "dflightContractCompile={} dflightCandidateCompile={}",
        kPreNGSetupGeometryHookOptInEnv, setupGeometryHookState.enabled ? "on" : "off",
        kPreNGSetupGeometryStrictCBBindEnv, setupGeometryBindCBState.enabled ? "on" : "off",
        kPreNGSetupGeometryPersistStrictCBEnv, setupGeometryPersistCBState.enabled ? "on" : "off",
        kPreNGPointLightHookOptInEnv, hookState.enabled ? "on" : "off", kPreNGStrictLightCBDiagnosticEnv,
        strictCBState.enabled ? "on" : "off", kPreNGStrictLightCBBindEnv, bindCBState.enabled ? "on" : "off",
        kPreNGClusterSRVBindEnv, bindClusterSRVState.enabled ? "on" : "off", kPreNGPersistentClusterPrepassEnv,
        persistentClusterPrepassState.enabled ? "on" : "off", kPreNGDFLightDrawStateStrictCBBindEnv,
        dflightDrawStateStrictCBBindState.enabled ? "on" : "off", kPreNGDFLightDrawStateClusterSRVBindEnv,
        dflightDrawStateClusterSRVBindState.enabled ? "on" : "off", kPreNGDFLightResourceNoOpPassEnv,
        dflightResourceNoOpPassState.enabled ? "on" : "off", kPreNGDFLightFullContractNoOpPassEnv,
        dflightFullContractNoOpPassState.enabled ? "on" : "off", kPreNGDFLightLLFAdditivePassEnv,
        dflightLLFAdditivePassState.enabled ? "on" : "off", kPreNGDFLightLegacyAdditiveProofEnv,
        dflightLegacyAdditiveProofState.enabled ? "on" : "off", kPreNGDFLightLLFAdditivePersistentEnv,
        dflightLLFAdditivePersistentState.enabled ? "on" : "off", kPreNGDFCompositeResourceBindEnv,
        dfCompositeResourceBindState.enabled ? "on" : "off", kPreNGDFCompositeSafeBindEnv,
        dfCompositeSafeBindState.enabled ? "on" : "off", kPreNGDFCompositeVisibleLLFEnv,
        dfCompositeVisibleLLFState.enabled ? "on" : "off", kPreNGBSLightingResourceBindEnv,
        bsLightingResourceBindState.enabled ? "on" : "off", kPreNGBSLightingSetupGeometryResourceBindEnv,
        bsLightingSetupGeometryResourceBindState.enabled ? "on" : "off", kPreNGShaderObjectMetadataEnv,
        shaderObjectMetadataState.enabled ? "on" : "off", kPreNGTraceLLFPixelEnv, tracePSState.enabled ? "on" : "off",
        kPreNGDFLightContractCompileEnv, dflightContractCompileState.enabled ? "on" : "off",
        kPreNGDFLightCandidateCompileEnv, dflightCandidateCompileState.enabled ? "on" : "off",
        EnvironmentSwitchSourceName(setupGeometryHookState.source),
        EnvironmentSwitchSourceName(setupGeometryBindCBState.source),
        EnvironmentSwitchSourceName(setupGeometryPersistCBState.source), EnvironmentSwitchSourceName(hookState.source),
        EnvironmentSwitchSourceName(strictCBState.source), EnvironmentSwitchSourceName(bindCBState.source),
        EnvironmentSwitchSourceName(bindClusterSRVState.source),
        EnvironmentSwitchSourceName(persistentClusterPrepassState.source),
        EnvironmentSwitchSourceName(dflightDrawStateStrictCBBindState.source),
        EnvironmentSwitchSourceName(dflightDrawStateClusterSRVBindState.source),
        EnvironmentSwitchSourceName(dflightResourceNoOpPassState.source),
        EnvironmentSwitchSourceName(dflightFullContractNoOpPassState.source),
        EnvironmentSwitchSourceName(dflightLLFAdditivePassState.source),
        EnvironmentSwitchSourceName(dflightLegacyAdditiveProofState.source),
        EnvironmentSwitchSourceName(dflightLLFAdditivePersistentState.source),
        EnvironmentSwitchSourceName(dfCompositeResourceBindState.source),
        EnvironmentSwitchSourceName(dfCompositeSafeBindState.source),
        EnvironmentSwitchSourceName(dfCompositeVisibleLLFState.source),
        EnvironmentSwitchSourceName(bsLightingResourceBindState.source),
        EnvironmentSwitchSourceName(bsLightingSetupGeometryResourceBindState.source),
        EnvironmentSwitchSourceName(shaderObjectMetadataState.source), EnvironmentSwitchSourceName(tracePSState.source),
        EnvironmentSwitchSourceName(dflightContractCompileState.source),
        EnvironmentSwitchSourceName(dflightCandidateCompileState.source));

    const auto gpuTimingState = ReadEnvironmentSwitch(kPreNGGpuTimingEnv);
    logger::info("[LightLimitFix] PreNG cluster GPU timing resolved {}={} source={}", kPreNGGpuTimingEnv,
                 gpuTimingState.enabled ? "on" : "off", EnvironmentSwitchSourceName(gpuTimingState.source));

    logger::info(
        "[LightLimitFix] PreNG BSLighting proof env snapshot {}={} {}={} {}={} {}={} {}={} {}={} {}={} {}={} sources "
        "bsLightingContractCompile={} bsLightingConsumerCompile={} bsLightingDescriptorObserve={} "
        "bsLightingResourceBind={} bindStrictCB={} bindClusterSRV={} bsLightingVanillaBind={} bsLightingLLFBind={}",
        kPreNGBSLightingContractCompileEnv, bsLightingContractCompileState.enabled ? "on" : "off",
        kPreNGBSLightingConsumerCompileEnv, bsLightingConsumerCompileState.enabled ? "on" : "off",
        kPreNGBSLightingDescriptorObserveEnv, bsLightingDescriptorObserveState.enabled ? "on" : "off",
        kPreNGBSLightingResourceBindEnv, bsLightingResourceBindState.enabled ? "on" : "off", kPreNGStrictLightCBBindEnv,
        bindCBState.enabled ? "on" : "off", kPreNGClusterSRVBindEnv, bindClusterSRVState.enabled ? "on" : "off",
        kPreNGBSLightingVanillaBindEnv, bsLightingVanillaBindState.enabled ? "on" : "off", kPreNGBSLightingLLFBindEnv,
        bsLightingLLFBindState.enabled ? "on" : "off",
        EnvironmentSwitchSourceName(bsLightingContractCompileState.source),
        EnvironmentSwitchSourceName(bsLightingConsumerCompileState.source),
        EnvironmentSwitchSourceName(bsLightingDescriptorObserveState.source),
        EnvironmentSwitchSourceName(bsLightingResourceBindState.source),
        EnvironmentSwitchSourceName(bindCBState.source), EnvironmentSwitchSourceName(bindClusterSRVState.source),
        EnvironmentSwitchSourceName(bsLightingVanillaBindState.source),
        EnvironmentSwitchSourceName(bsLightingLLFBindState.source));
}

bool ShouldInstallPreNGInternalPointLightHook()
{
    if constexpr (kPreNGEnableInternalPointLightHook)
    {
        return true;
    }

    return IsTruthyEnvironmentSwitch(kPreNGPointLightHookOptInEnv);
}

bool ShouldUpdatePreNGStrictLightCB()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGStrictLightCBDiagnosticEnv);
    return enabled;
}

bool ShouldBindPreNGStrictLightCB()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGStrictLightCBBindEnv);
    return enabled;
}

bool ShouldBindPreNGSetupGeometryStrictLightCB()
{
    static const bool enabled =
        IsTruthyEnvironmentSwitch(kPreNGSetupGeometryStrictCBBindEnv) && ShouldBindPreNGStrictLightCB();
    return enabled;
}

bool ShouldPersistPreNGSetupGeometryStrictLightCB()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGSetupGeometryPersistStrictCBEnv);
    return enabled;
}

bool ShouldBindPreNGClusterSRVs()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGClusterSRVBindEnv);
    return enabled;
}

bool ShouldBindPreNGPrepassResources()
{
    static const bool enabled = [] {
        const auto state = ReadEnvironmentSwitch(kPreNGPrepassResourceBindEnv);
        const bool resolved = state.source == EnvironmentSwitchSource::kNone || state.enabled;
        logger::info("[LightLimitFix] PreNG Prepass resource bind resolved {}={} source={} default=on",
                     kPreNGPrepassResourceBindEnv, resolved ? "on" : "off", EnvironmentSwitchSourceName(state.source));
        return resolved;
    }();
    return enabled;
}

bool ShouldBindPreNGDFLightDrawStateStrictLightCB()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGDFLightDrawStateStrictCBBindEnv);
    return enabled;
}

bool ShouldBindPreNGDFLightDrawStateClusterSRVs()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGDFLightDrawStateClusterSRVBindEnv);
    return enabled;
}

bool ShouldUsePreNGSetupGeometryStrictLightCBProof()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGSetupGeometryHookOptInEnv) &&
                                (ShouldUpdatePreNGStrictLightCB() || ShouldBindPreNGStrictLightCB());
    return enabled;
}

bool ShouldReusePreNGShadowSceneFastReuse()
{
    static const bool enabled = [] {
        const auto state = ReadEnvironmentSwitch(kPreNGShadowSceneFastReuseEnv);
        const bool resolved = state.source == EnvironmentSwitchSource::kNone || state.enabled;
        logger::info("[LightLimitFix] PreNG ShadowScene fast reuse resolved {}={} source={} default=on",
                     kPreNGShadowSceneFastReuseEnv, resolved ? "on" : "off", EnvironmentSwitchSourceName(state.source));
        return resolved;
    }();
    return enabled;
}

std::uint32_t GetPreNGShadowSceneFastReuseRefreshInterval()
{
    static const std::uint32_t interval = [] {
        const auto state = ReadEnvironmentUInt(kPreNGShadowSceneFastReuseRefreshIntervalEnv);
        auto resolved = kPreNGDefaultShadowSceneFastReuseRefreshInterval;
        auto clamped = false;
        if (state.present && state.valid)
        {
            resolved = state.value;
            if (resolved < kPreNGMinShadowSceneFastReuseRefreshInterval)
            {
                resolved = kPreNGMinShadowSceneFastReuseRefreshInterval;
                clamped = true;
            }
            else if (resolved > kPreNGMaxShadowSceneFastReuseRefreshInterval)
            {
                resolved = kPreNGMaxShadowSceneFastReuseRefreshInterval;
                clamped = true;
            }
        }

        logger::info("[LightLimitFix] PreNG ShadowScene fast reuse refresh interval resolved {}={} source={} "
                     "present={} valid={} clamped={} default={} range={}..{}",
                     kPreNGShadowSceneFastReuseRefreshIntervalEnv, resolved, EnvironmentSwitchSourceName(state.source),
                     state.present, state.valid, clamped, kPreNGDefaultShadowSceneFastReuseRefreshInterval,
                     kPreNGMinShadowSceneFastReuseRefreshInterval, kPreNGMaxShadowSceneFastReuseRefreshInterval);
        return resolved;
    }();
    return interval;
}

bool ShouldRunPreNGDFLightResourceNoOpPass()
{
    return IsTruthyEnvironmentSwitch(kPreNGDFLightResourceNoOpPassEnv);
}

bool ShouldRunPreNGDFLightFullContractNoOpPass()
{
    return IsTruthyEnvironmentSwitch(kPreNGDFLightFullContractNoOpPassEnv);
}

bool ShouldRunPreNGDFLightLLFAdditivePass()
{
    static const bool enabled = [] {
        const bool requested = IsTruthyEnvironmentSwitch(kPreNGDFLightLLFAdditivePassEnv);
        const bool legacyProofAllowed = IsTruthyEnvironmentSwitch(kPreNGDFLightLegacyAdditiveProofEnv);
        if (requested && !legacyProofAllowed)
        {
            logger::warn(
                "[LightLimitFix] PreNG DFLight LLF additive pass held; this legacy proof path is not the Skyrim-CS LLF "
                "direction. Keep it off for normal development, or set {}=1 only to reproduce the old additive proof.",
                kPreNGDFLightLegacyAdditiveProofEnv);
        }
        else if (requested)
        {
            logger::warn("[LightLimitFix] PreNG DFLight LLF additive legacy proof active; this is not the final "
                         "Skyrim-CS route and should not be used for performance or visual validation.");
        }
        return requested && legacyProofAllowed;
    }();
    return enabled;
}

bool ShouldRunPreNGDFLightFullContractVisibleLLF()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGDFLightFullContractVisibleLLFEnv);
    return enabled;
}

bool ShouldRunPreNGDFCompositeVisibleLLF()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv) &&
                                IsTruthyEnvironmentSwitch(kPreNGDFCompositeVisibleLLFEnv);
    return enabled;
}

bool ShouldUsePreNGDFLightDescriptorDemandResources()
{
    static const bool enabled = [] {
        const bool requested = IsTruthyEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerEnv);
        const bool unsafeOverride = IsTruthyEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
        return requested && unsafeOverride;
    }();
    return enabled;
}

bool ShouldUsePreNGDFCompositeDescriptorDemandResources()
{
    static const bool enabled =
        IsTruthyEnvironmentSwitch(kPreNGDFCompositeResourceBindEnv) || ShouldRunPreNGDFCompositeVisibleLLF();
    return enabled;
}

bool ShouldUsePreNGBSLightingDescriptorDemandResources()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGBSLightingResourceBindEnv);
    return enabled;
}

// True when the visible BSLighting LLF consumer bind gate is active. Used to
// keep the clustered prepass running every frame (Skyrim-CS per-frame model)
// instead of only inside the bounded proof window, so currentLightCount and
// the b3/t35-t37 payload stay valid for the per-draw consumer bind. Without
// this the consumer is always queried after the proof window has cleared
// currentLightCount to 0 and stays held (clustered-payload-pending).
bool ShouldBindPreNGBSLightingLLFVisibleConsumer()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGBSLightingLLFBindEnv);
    return enabled;
}

bool ShouldObservePreNGBSLightingWorldDescriptors()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGBSLightingWorldObserveEnv);
    return enabled;
}

bool ShouldBindPreNGBSLightingWorldConsumer()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGBSLightingWorldBindEnv);
    return enabled;
}

// FO4 BSLighting pixel descriptors carry the lighting marker in bit 0 (every
// known contract descriptor ends in 0x1 and NormalizeLightingPixelDescriptor
// ORs it back in). Normal-world draws use variants of the contract set that the
// menu-preview shader lookups never expose; this cheap marker lets us recognise
// them without a live-game descriptor dump.
bool IsPlausiblePreNGBSLightingPixelDescriptor(std::uint32_t a_descriptor)
{
    return (a_descriptor & 0x1u) != 0;
}

void ObservePreNGBSLightingWorldDescriptor(std::uint32_t a_pixelDescriptor)
{
    bool first = false;
    std::uint32_t observation = 0;
    std::size_t learned = 0;
    {
        std::scoped_lock lock(s_preNGBSLightingWorldDescriptorLock);
        first = s_preNGBSLightingWorldDescriptors.insert(a_pixelDescriptor).second;
        observation = s_preNGBSLightingWorldDescriptorObservations.fetch_add(1, std::memory_order_relaxed) + 1;
        learned = s_preNGBSLightingWorldDescriptors.size();
    }

    if (first || observation <= 16 || observation % 512 == 0)
    {
        auto *runtime = CommunityShaders::Runtime::GetSingleton();
        logger::info("[LightLimitFix] PreNG BSLighting normal-world pixel descriptor observed descriptor=0x{:X} "
                     "first={} observations={} learned={} frame={}",
                     a_pixelDescriptor, first, observation, learned, runtime ? runtime->GetFrameCount() : 0);
    }
}

std::size_t GetLearnedPreNGBSLightingWorldDescriptorCount()
{
    std::scoped_lock lock(s_preNGBSLightingWorldDescriptorLock);
    return s_preNGBSLightingWorldDescriptors.size();
}

bool ShouldBindPreNGDFLightForwardVisibleLLF()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGDFLightForwardLLFBindEnv);
    return enabled;
}

bool ShouldAllowPreNGBSLightingConsumerBindInMenu()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGBSLightingLLFBindMenuEnv);
    return enabled;
}

bool ShouldBindPreNGBSLightingSetupGeometryResources()
{
    static const bool enabled = IsTruthyEnvironmentSwitch(kPreNGBSLightingSetupGeometryResourceBindEnv);
    return enabled;
}

bool ShouldTimePreNGClusterPrepassGpu()
{
    // Not cached: the switch may be added to Debug.ini after the process has
    // already started (first access loads the ini), and a static const bool
    // captured here would permanently lock the first-read value.
    return IsTruthyEnvironmentSwitch(kPreNGGpuTimingEnv);
}

bool ShouldSubmitPreNGClusterPrepassEarly()
{
    // Default OFF: submit the clustered compute from Main_RenderWorld_Start
    // (Prepass). The shadow-map (EarlyPrepass) route stays opt-in: the
    // Main_RenderShadowMaps relocation (entry base+0x2850B1B) is not a live CALL
    // target — detouring it still never fires (0 hits), unlike World_Start
    // (8.19M hits) — so routing the LLF prepass there silently disables light
    // collection. Keep the World_Start route as the reliable default.
    static const bool enabled = [] {
        const auto state = ReadEnvironmentSwitch(kPreNGPrepassEarlyHookEnv);
        const bool resolved = state.enabled;
        logger::info("[LightLimitFix] PreNG clustered prepass early-submit resolved {}={} source={} default=off",
                     kPreNGPrepassEarlyHookEnv, resolved ? "on" : "off", EnvironmentSwitchSourceName(state.source));
        return resolved;
    }();
    return enabled;
}

void LogPreNGHookReachabilityWatchdog(std::uint64_t a_frame)
{
    if (a_frame < 600)
    {
        return;
    }

    const bool pointRequested = ShouldInstallPreNGInternalPointLightHook();
    const bool setupResourceRequested = ShouldBindPreNGBSLightingSetupGeometryResources();
    const bool batchHookInstalled = s_preNGBSLightingBatchSetupHookInstalled.load(std::memory_order_acquire);
    if (!pointRequested && !setupResourceRequested && !batchHookInstalled)
    {
        return;
    }

    // Periodic (every 600 frames) instead of once-only: lets us see whether the
    // BSLighting SetupGeometry / point-light / batched hooks actually fire during
    // normal world rendering, not just during menu 3D previews.
    if (a_frame % 600 != 0)
    {
        return;
    }

    logger::info("[LightLimitFix] PreNG hook reachability watchdog frame={} pointRequested={} pointInstalled={} "
                 "pointPatchVerified={} pointCalls={} setupResourceRequested={} setupInstalled={} setupCalls={} "
                 "setupBypassCalls={} batchHookInstalled={} batchCalls={} worldDescriptors={} worldBinds={} worldObs={}; "
                 "zero-call hooks mean this run has not exercised the verified BSLighting/point-light/batch route yet, "
                 "so visible LLF remains held",
                 a_frame, pointRequested, s_preNGPointLightHookInstalled.load(std::memory_order_acquire),
                 s_preNGPointLightHookPatchVerified.load(std::memory_order_acquire),
                 s_preNGPointLightHookCallCount.load(std::memory_order_relaxed), setupResourceRequested,
                 s_preNGBSLightingSetupGeometryHookInstalled.load(std::memory_order_acquire),
                 s_preNGBSLightingSetupGeometryHookCallCount.load(std::memory_order_relaxed),
                 s_preNGBSLightingSetupGeometryBypassCallCount.load(std::memory_order_relaxed),
                 batchHookInstalled, s_preNGBSLightingBatchSetupHookCallCount.load(std::memory_order_relaxed),
                 GetLearnedPreNGBSLightingWorldDescriptorCount(),
                 s_preNGBSLightingWorldBindCount.load(std::memory_order_relaxed),
                 s_preNGBSLightingWorldDescriptorObservations.load(std::memory_order_relaxed));
}

bool ShouldHoldPreNGDFLightPreparedState()
{
    return ShouldRunPreNGDFLightResourceNoOpPass() || ShouldRunPreNGDFLightFullContractNoOpPass() ||
           ShouldBindPreNGDFLightDrawStateClusterSRVs() || ShouldRunPreNGDFLightLLFAdditivePass();
}

bool ShouldRunPreNGClusterPrepassProof()
{
    const bool dflightLLFAdditiveRequested = ShouldRunPreNGDFLightLLFAdditivePass();
    const bool prepassResourceBindRequested = ShouldBindPreNGPrepassResources();
    const bool strictCBProofRequested = prepassResourceBindRequested &&
                                        (ShouldUpdatePreNGStrictLightCB() || ShouldBindPreNGStrictLightCB()) &&
                                        !ShouldUsePreNGSetupGeometryStrictLightCBProof();
    const bool prepassClusterSRVProofRequested = prepassResourceBindRequested && ShouldBindPreNGClusterSRVs();
    const bool fullContractDescriptorObserved =
        s_preNGDFLightLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
    const bool dfCompositeDescriptorObserved =
        s_preNGDFCompositeLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
    const bool bsLightingDescriptorObserved =
        s_preNGBSLightingLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
    const bool descriptorResourceSubGateRequested = ShouldBindPreNGStrictLightCB() || ShouldBindPreNGClusterSRVs();
    const bool bsLightingResourceProofGateRequested = ShouldUsePreNGBSLightingDescriptorDemandResources() &&
                                                      bsLightingDescriptorObserved &&
                                                      descriptorResourceSubGateRequested;
    // Preview-menu suppression is now scoped (settle window), applied below
    // via bsLightingResourceProofMenuDeferred rather than a permanent latch.
    const bool bsLightingResourceProofRequested = bsLightingResourceProofGateRequested;
    // Skyrim-parity step 1: menu suppression removed (BOSS). Skyrim CS runs the
    // clustered prepass every frame regardless of menus; the preview-menu defer
    // is no longer applied.
    const bool bsLightingResourceProofMenuDeferred = false;
    const bool descriptorDemandRequested =
        (ShouldUsePreNGDFLightDescriptorDemandResources() || fullContractDescriptorObserved ||
         (ShouldUsePreNGDFCompositeDescriptorDemandResources() && dfCompositeDescriptorObserved) ||
         (bsLightingResourceProofRequested && !bsLightingResourceProofMenuDeferred)) &&
        descriptorResourceSubGateRequested;
    const bool proofRequested = strictCBProofRequested || prepassClusterSRVProofRequested ||
                                descriptorDemandRequested || ShouldBindPreNGDFLightDrawStateClusterSRVs() ||
                                ShouldRunPreNGDFLightResourceNoOpPass() ||
                                ShouldRunPreNGDFLightFullContractNoOpPass() || dflightLLFAdditiveRequested;
    if (!proofRequested)
    {
        static bool loggedHeld = false;
        if (!loggedHeld)
        {
            logger::info(
                "[LightLimitFix] PreNG clustered Prepass held; strict CB proof is served by SetupGeometry when {}=1. "
                "{} and {} are resource sub-gates and no longer start Prepass without a Prepass bind or descriptor "
                "consumer; enable {}, {}, {}, {}, {}, or {} for Prepass/DFLight/DFComposite contract proof work, or "
                "{}=1 plus a BSLighting descriptor observation for BSLighting resource proof work. {}=1 only extends "
                "an on-demand DFLight persistent proof and no longer starts Prepass by itself. The old DFLight "
                "additive path requires {}=1 plus {}=1 and is not the Skyrim-CS LLF direction.",
                kPreNGSetupGeometryHookOptInEnv, kPreNGStrictLightCBBindEnv, kPreNGClusterSRVBindEnv,
                kPreNGPrepassResourceBindEnv, kPreNGDFLightDrawStateClusterSRVBindEnv, kPreNGDFLightResourceNoOpPassEnv,
                kPreNGDFLightFullContractNoOpPassEnv, kPreNGDFLightFullShadowedDescriptorConsumerEnv,
                kPreNGDFCompositeResourceBindEnv, kPreNGBSLightingResourceBindEnv, kPreNGPersistentClusterPrepassEnv,
                kPreNGDFLightLegacyAdditiveProofEnv, kPreNGDFLightLLFAdditivePassEnv);
            loggedHeld = true;
        }
        return false;
    }

    // Skyrim-parity step 1 (BOSS): once any prepass/consumer demand is active,
    // run the clustered prepass EVERY frame — no proof-window frame budget, no
    // persistent-vs-finite distinction, no refresh interval. Skyrim CS
    // dispatches build+cull every frame unconditionally; matching that removes
    // the periodic-resubmit / proof-window-expiry behavior. The former
    // frame-budget / persistent / refresh-interval tail was removed here.
    return true;
}

bool ShouldCompilePreNGDFLightContractProbe()
{
    return IsTruthyEnvironmentSwitch(kPreNGDFLightContractCompileEnv);
}

bool ShouldCompilePreNGDFLightFullShadowedCandidate()
{
    return IsTruthyEnvironmentSwitch(kPreNGDFLightCandidateCompileEnv);
}

void TryBindPreNGBSLightingDeferredDescriptorResources(LightLimitFix &a_feature)
{
    if (!ShouldUsePreNGBSLightingDescriptorDemandResources() ||
        !s_preNGBSLightingLLFConsumerDescriptorObserved.load(std::memory_order_relaxed) ||
        s_preNGBSLightingDeferredResourceProofComplete.load(std::memory_order_relaxed) ||
        !a_feature.HasPreNGBSLightingDescriptorConsumerData())
    {
        return;
    }

    if (ShouldDeferPreNGBSLightingResourceProofForMenu())
    {
        return;
    }

    const auto vertexDescriptor = s_preNGBSLightingLLFConsumerLastVertexDescriptor.load(std::memory_order_relaxed);
    const auto pixelDescriptor = s_preNGBSLightingLLFConsumerLastPixelDescriptor.load(std::memory_order_relaxed);
    const auto vanillaFound = s_preNGBSLightingLLFConsumerLastFound.load(std::memory_order_relaxed);
    const auto vanillaPixelShader = s_preNGBSLightingLLFConsumerLastVanillaPixelShader.load(std::memory_order_relaxed);

    const auto resourceState = a_feature.BindPreNGBSLightingDescriptorResourcesToPixelShader();

    static std::atomic_uint32_t deferredBindCount = 0;
    const auto bindIndex = ++deferredBindCount;
    if (bindIndex <= 8 || bindIndex % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG BSLighting deferred descriptor resources attempted binds={} vsDesc=0x{:X} "
                     "psDesc=0x{:X} vanillaFound={} vanillaPS=0x{:X} strictCB={} clusterSRVs={} lights={}; audit "
                     "records whether current PS still matches the observed BSLighting PS",
                     bindIndex, vertexDescriptor, pixelDescriptor, vanillaFound, vanillaPixelShader,
                     resourceState.strictCBBound, resourceState.clusterSRVsBound, resourceState.lightCount);
        a_feature.TracePreNGActiveLightingBindings("descriptor-bslighting-resource-bind-deferred-prepass",
                                                   static_cast<std::int32_t>(F4Runtime::PreNG::BS_LIGHTING_SHADER_TYPE),
                                                   vertexDescriptor, pixelDescriptor, vanillaFound, vanillaPixelShader);
    }

    if (resourceState.clusterSRVsBound &&
        !s_preNGBSLightingDeferredResourceProofComplete.exchange(true, std::memory_order_relaxed))
    {
        logger::info("[LightLimitFix] PreNG BSLighting deferred resource-only proof reached t35-t37 completion "
                     "after clustered payload upload; future deferred proof binds are held until a visible-safe "
                     "consumer is implemented");
    }
}

void RunPreNGDFLightCompileOnlyDiagnostic(const char *a_label, const char *a_envName, const char *a_source,
                                          bool a_enabled, std::atomic_bool &a_attempted, bool &a_loggedHeld)
{
    if (!a_enabled)
    {
        if (!a_loggedHeld)
        {
            logger::info("[LightLimitFix] PreNG DFLight {} compile held; set {}=1 to compile {} in-game; shader "
                         "replacement and binding remain held",
                         a_label, a_envName, a_source);
            a_loggedHeld = true;
        }
        return;
    }

    if (a_attempted.exchange(true))
    {
        return;
    }

    auto compiled =
        CommunityShaders::ShaderCompiler::GetSingleton()->CompileFromFile(a_source, "ps_5_0", nullptr, "main");
    if (!compiled)
    {
        logger::warn("[LightLimitFix] PreNG DFLight {} compile failed source={} replacement=held bind=held", a_label,
                     a_source);
        return;
    }

    auto *shaderCache = CommunityShaders::ShaderCache::GetSingleton();
    auto metadata = shaderCache ? shaderCache->GetMetadataForBytecode(CommunityShaders::ShaderStage::Pixel,
                                                                      compiled->data(), compiled->size())
                                : std::nullopt;
    const auto evidence = GetPreNGShaderSlotEvidence(metadata);

    const auto cb2Bytes = metadata ? metadata->constantBufferSizes[2] : 0;
    const auto cb12Bytes = metadata ? metadata->constantBufferSizes[12] : 0;
    const auto cb3Bytes = metadata ? metadata->constantBufferSizes[3] : 0;
    const auto t0Samples = metadata ? GetPreNGTextureSampleCount(*metadata, 0) : 0;
    const auto t1Samples = metadata ? GetPreNGTextureSampleCount(*metadata, 1) : 0;
    const auto t2Samples = metadata ? GetPreNGTextureSampleCount(*metadata, 2) : 0;
    const auto t3Samples = metadata ? GetPreNGTextureSampleCount(*metadata, 3) : 0;
    const auto t5Samples = metadata ? GetPreNGTextureSampleCount(*metadata, 5) : 0;

    const bool fullShadowedVanilla = HasPreNGFullShadowedDFLightVanillaContract(metadata);
    const bool llfContract = evidence.hasMetadata && evidence.declaresCB3 && evidence.declaresT35 &&
                             evidence.declaresT36 && evidence.declaresT37 && evidence.samplesT35 > 0 &&
                             evidence.samplesT36 > 0 && evidence.samplesT37 > 0 && cb3Bytes > 0;

    logger::info("[LightLimitFix] PreNG DFLight {} compile result source={} bytecode={} metadata={} buffers={} "
                 "textures={} samples={} replacement=held bind=held",
                 a_label, a_source, compiled->size(), FormatPreNGShaderMetadata(metadata),
                 metadata ? FormatPreNGShaderBufferSlots(*metadata) : "none",
                 metadata ? FormatPreNGShaderTextureSlots(*metadata) : "none",
                 metadata ? FormatPreNGShaderTextureSampleCounts(*metadata) : "none");

    logger::info("[LightLimitFix] PreNG DFLight {} evidence vanillaFullShadowed={} cb2Bytes={} cb12Bytes={} "
                 "slots(t0={},t1={},t2={},t3={},t5={}) samples(t0={},t1={},t2={},t3={},t5={}) llfComplete={} cb3={} "
                 "cb3Bytes={} t35={} t36={} t37={} loads(t35={},t36={},t37={}) replacement=held bind=held",
                 a_label, fullShadowedVanilla, cb2Bytes, cb12Bytes, metadata && HasPreNGTextureSlot(*metadata, 0),
                 metadata && HasPreNGTextureSlot(*metadata, 1), metadata && HasPreNGTextureSlot(*metadata, 2),
                 metadata && HasPreNGTextureSlot(*metadata, 3), metadata && HasPreNGTextureSlot(*metadata, 5),
                 t0Samples, t1Samples, t2Samples, t3Samples, t5Samples, llfContract, evidence.declaresCB3, cb3Bytes,
                 evidence.declaresT35, evidence.declaresT36, evidence.declaresT37, evidence.samplesT35,
                 evidence.samplesT36, evidence.samplesT37);
}

void RunPreNGDFLightContractProbeCompileDiagnostic()
{
    static std::atomic_bool attempted = false;
    static bool loggedHeld = false;
    RunPreNGDFLightCompileOnlyDiagnostic("contract probe", kPreNGDFLightContractCompileEnv,
                                         kPreNGDFLightContractProbeSource, ShouldCompilePreNGDFLightContractProbe(),
                                         attempted, loggedHeld);
}

void RunPreNGDFLightFullShadowedCandidateCompileDiagnostic()
{
    static std::atomic_bool attempted = false;
    static bool loggedHeld = false;
    RunPreNGDFLightCompileOnlyDiagnostic("full-shadowed candidate", kPreNGDFLightCandidateCompileEnv,
                                         kPreNGDFLightFullShadowedCandidateSource,
                                         ShouldCompilePreNGDFLightFullShadowedCandidate(), attempted, loggedHeld);
}

struct PreNGPointLightSetupCall
{
    static std::int64_t thunk(std::uintptr_t a_pixelShader, RE::BSRenderPass *a_pass, DirectX::XMMATRIX *a_transform,
                              std::int32_t a_lightCount, std::int32_t a_shadowArg, float a_worldScale,
                              std::int32_t a_unknown)
    {
        const auto callIndex = s_preNGPointLightHookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
        std::uint32_t collected = 0;
        bool strictCBUploaded = false;
        bool strictCBBound = false;
        bool clusterSRVsBound = false;
        bool previewMenuSuppressed = false;
        std::uint32_t requestedLightCount = 0;
        LightLimitFix *self = nullptr;
        if (globals::features::lightLimitFix.loaded && a_lightCount > 0)
        {
            self = &globals::features::lightLimitFix;
            previewMenuSuppressed = self->ShouldSuppressPreNGBSLightingVisibleConsumerForMenu();
            if (!previewMenuSuppressed)
            {
                requestedLightCount = static_cast<std::uint32_t>(a_lightCount);
                collected = self->CollectLightsFromPreNGSceneLights(
                    a_pass, requestedLightCount, a_shadowArg > 0 ? static_cast<std::uint32_t>(a_shadowArg) : 0);
            }
        }

        const auto result =
            func(a_pixelShader, a_pass, a_transform, a_lightCount, a_shadowArg, a_worldScale, a_unknown);
        const auto currentPixelShader = ReadPreNGCurrentPixelShaderEntryState();

        if (self && !previewMenuSuppressed)
        {
            strictCBUploaded = false;
            strictCBBound = false;
            clusterSRVsBound = self->BindPreNGClusterSRVsToPixelShader(a_pass, requestedLightCount, strictCBBound);
        }

        const bool logThisCall = callIndex <= 8 || callIndex % 512 == 0;
        if (self && logThisCall)
        {
            self->TracePreNGActiveLightingBindings("point-light-hook", -1, currentPixelShader.id, currentPixelShader.id,
                                                   currentPixelShader.d3dObject != 0, currentPixelShader.d3dObject);
        }

        if (logThisCall)
        {
            logger::info("[LightLimitFix] PreNG internal point-light hook reached calls={} constantGroup=0x{:X} "
                         "pass=0x{:X} requested={} collected={} strictCB={} b3={} t35t37={} "
                         "previewMenuSuppressed={} bindOrder=post-vanilla shadowArg={} "
                         "worldScale={:.3f} unknown={} currentPSEntry=0x{:X} currentPSD3D=0x{:X} currentPSId=0x{:X} "
                         "currentPSEntryReadable={} currentPSSlots(88={},89={},94={},96={})",
                         callIndex, a_pixelShader, reinterpret_cast<std::uintptr_t>(a_pass), a_lightCount, collected,
                         strictCBUploaded ? "uploaded" : "held", strictCBBound ? "bound" : "held",
                         clusterSRVsBound ? "bound" : "held", previewMenuSuppressed, a_shadowArg,
                         a_worldScale, a_unknown, currentPixelShader.entry, currentPixelShader.d3dObject,
                         currentPixelShader.id, currentPixelShader.entryReadable,
                         static_cast<std::uint32_t>(currentPixelShader.slot88),
                         static_cast<std::uint32_t>(currentPixelShader.slot89),
                         static_cast<std::uint32_t>(currentPixelShader.slot94),
                         static_cast<std::uint32_t>(currentPixelShader.slot96));
        }

        return result;
    }

    static inline REL::Relocation<decltype(thunk)> func;
};

bool VerifyPreNGPointLightHookPatch(std::uintptr_t a_runtimeCall)
{
    const auto patch = F4Runtime::ReadPreNGCallPatch(a_runtimeCall);
    const auto branchTarget = patch.readable ? F4Runtime::ResolvePreNGAbsoluteJumpTarget(patch.callTarget) : 0;
    const auto thunkTarget = reinterpret_cast<std::uintptr_t>(&PreNGPointLightSetupCall::thunk);
    const auto originalTarget = F4Runtime::PreNG::POINT_LIGHT_TARGET.address();
    const bool directToThunk = patch.callTarget == thunkTarget;
    const bool branchToThunk = branchTarget == thunkTarget;
    const bool verified = patch.readable && patch.opcode == 0xE8 && (directToThunk || branchToThunk);

    if (verified)
    {
        logger::warn("[LightLimitFix] PreNG internal point-light hook patch verified call=0x{:X} callTarget=0x{:X} "
                     "branchTarget=0x{:X} thunk=0x{:X} original=0x{:X} rel32=0x{:08X}",
                     a_runtimeCall, patch.callTarget, branchTarget, thunkTarget, originalTarget,
                     static_cast<std::uint32_t>(patch.rel32));
    }
    else
    {
        logger::warn(
            "[LightLimitFix] PreNG internal point-light hook patch verification failed call=0x{:X} readable={} "
            "opcode=0x{:02X} callTarget=0x{:X} branchTarget=0x{:X} thunk=0x{:X} original=0x{:X} rel32=0x{:08X}",
            a_runtimeCall, patch.readable, static_cast<std::uint32_t>(patch.opcode), patch.callTarget, branchTarget,
            thunkTarget, originalTarget, static_cast<std::uint32_t>(patch.rel32));
    }

    return verified;
}

enum class PreNGPointLightHookState
{
    Failed,
    Prepared,
    Installed,
    InstalledUnverified
};

const char *PreNGPointLightHookStateName(PreNGPointLightHookState a_state)
{
    switch (a_state)
    {
    case PreNGPointLightHookState::Prepared:
        return "prepared";
    case PreNGPointLightHookState::Installed:
        return "installed";
    case PreNGPointLightHookState::InstalledUnverified:
        return "installed-unverified";
    case PreNGPointLightHookState::Failed:
    default:
        return "failed";
    }
}

bool CanInstallPreNGSetupGeometryHooks(PreNGPointLightHookState a_pointLightHookState)
{
    return a_pointLightHookState == PreNGPointLightHookState::Prepared ||
           a_pointLightHookState == PreNGPointLightHookState::Installed;
}

PreNGPointLightHookState PreparePreNGPointLightHook()
{
    if (!ValidatePreNGPointLightCallsite())
    {
        s_preNGPointLightHookInstalled.store(false, std::memory_order_release);
        s_preNGPointLightHookPatchVerified.store(false, std::memory_order_release);
        logger::warn("[LightLimitFix] PreNG internal point-light hook not prepared; callsite validation failed");
        return PreNGPointLightHookState::Failed;
    }

    const auto runtimeCall = F4Runtime::PreNG::POINT_LIGHT_CALL.address();
    if (ShouldInstallPreNGInternalPointLightHook())
    {
        stl::write_thunk_call<PreNGPointLightSetupCall>(runtimeCall);
        const bool patchVerified = VerifyPreNGPointLightHookPatch(runtimeCall);
        s_preNGPointLightHookInstalled.store(true, std::memory_order_release);
        s_preNGPointLightHookPatchVerified.store(patchVerified, std::memory_order_release);
        logger::warn("[LightLimitFix] PreNG internal point-light hook installed at call=0x{:X}; diagnostic opt-in is "
                     "active; patchVerified={}",
                     runtimeCall, patchVerified);
        return patchVerified ? PreNGPointLightHookState::Installed : PreNGPointLightHookState::InstalledUnverified;
    }

    s_preNGPointLightHookInstalled.store(false, std::memory_order_release);
    s_preNGPointLightHookPatchVerified.store(false, std::memory_order_release);

    logger::info("[LightLimitFix] PreNG internal point-light hook prepared at call=0x{:X}; install gate is off (set "
                 "{}=1 for diagnostic activation)",
                 runtimeCall, kPreNGPointLightHookOptInEnv);
    return PreNGPointLightHookState::Prepared;
}
#endif
} // namespace

void LightLimitFix::LoadSettings()
{
    constexpr auto kSection = "Settings";
    constexpr auto kVizEnabled = "bEnableLightsVisualisation";
    constexpr auto kVizMode = "uLightsVisualisationMode";

    CSimpleIniA ini;
    ini.SetUnicode();

    const auto path = GetSettingsPath();
    std::error_code ec;
    if (std::filesystem::exists(path, ec))
    {
        ini.LoadFile(path.string().c_str());
    }

    settings.EnableLightsVisualisation = ini.GetBoolValue(kSection, kVizEnabled, settings.EnableLightsVisualisation);
    settings.LightsVisualisationMode = static_cast<std::uint32_t>(
        ini.GetLongValue(kSection, kVizMode, static_cast<long>(settings.LightsVisualisationMode)));
}

void LightLimitFix::SaveSettings()
{
    constexpr auto kSection = "Settings";
    constexpr auto kVizEnabled = "bEnableLightsVisualisation";
    constexpr auto kVizMode = "uLightsVisualisationMode";

    CSimpleIniA ini;
    ini.SetUnicode();

    ini.SetBoolValue(kSection, kVizEnabled, settings.EnableLightsVisualisation);
    ini.SetLongValue(kSection, kVizMode, static_cast<long>(settings.LightsVisualisationMode));

    const auto path = GetSettingsPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    ini.SaveFile(path.string().c_str());
}

void LightLimitFix::RestoreDefaultSettings()
{
    settings = {};
}

void LightLimitFix::DrawSettings()
{
    if (ImGui::CollapsingHeader("Light Limit Fix"))
    {
        int changed = 0;
        changed |= ImGui::Checkbox("Lights Visualisation", &settings.EnableLightsVisualisation) ? 1 : 0;

        const char *modes[] = {"Clusters", "Lights", "Both"};
        int mode = static_cast<int>(settings.LightsVisualisationMode);
        if (ImGui::Combo("Visualisation Mode", &mode, modes, IM_ARRAYSIZE(modes)))
        {
            settings.LightsVisualisationMode = static_cast<std::uint32_t>(std::clamp(mode, 0, IM_ARRAYSIZE(modes) - 1));
            changed = 1;
        }

        ImGui::Text("Lights: %u", currentLightCount);
        ImGui::Text("Clusters: %ux%ux%u", clusterSize[0], clusterSize[1], clusterSize[2]);

        if (changed)
        {
            SaveSettings();
        }
    }
}

LightLimitFix::PerFrame LightLimitFix::GetCommonBufferData()
{
    PerFrame perFrame{};
    perFrame.EnableLightsVisualisation = settings.EnableLightsVisualisation;
    perFrame.LightsVisualisationMode = settings.LightsVisualisationMode;
    perFrame.CameraNear = CameraNear;
    perFrame.CameraFar = CameraFar;
    perFrame.ClusterSize[0] = clusterSize[0];
    perFrame.ClusterSize[1] = clusterSize[1];
    perFrame.ClusterSize[2] = clusterSize[2];
    return perFrame;
}

void LightLimitFix::SetupResources()
{
    auto *device = CommunityShaders::Runtime::GetSingleton()->GetDevice();
    if (!device)
    {
        logger::warn("[LightLimitFix] SetupResources: D3D11 device not available");
        return;
    }
#if defined(FALLOUT_PRE_NG)
    clusterBuildCacheValid = false;
    clusterBuildCache = {};
    clusterPayloadCacheValid = false;
    clusterPayloadCache = {};
    shadowSceneFastReuseValid = false;
    shadowSceneFastReuse = {};
#endif

    // com_ptr auto-releases previous resources on reassignment — no manual ClearShaderCache needed

    auto shaderPath = GetShaderPath();
#if defined(FALLOUT_PRE_NG)
    RunPreNGDFLightContractProbeCompileDiagnostic();
    RunPreNGDFLightFullShadowedCandidateCompileDiagnostic();
#endif

    auto compileOrLoad = [&](const char *a_name, winrt::com_ptr<ID3D11ComputeShader> &a_out) {
        auto compiled = CommunityShaders::ShaderCompiler::GetSingleton()->CompileFromFile(shaderPath + a_name);
        if (!compiled)
        {
            logger::warn("[LightLimitFix] Failed to compile: {}{}", shaderPath, a_name);
            return false;
        }
        const auto hr = device->CreateComputeShader(compiled->data(), compiled->size(), nullptr, a_out.put());
        if (FAILED(hr))
        {
            return LogResourceFailure(a_name, hr);
        }
        return true;
    };

    if (!compileOrLoad("clusterBuildingCS.hlsl", clusterBuildingCS) ||
        !compileOrLoad("clusterCullingCS.hlsl", clusterCullingCS))
    {
        logger::warn("[LightLimitFix] GPU resources pending - compute shaders not available");
        return;
    }

    auto createBuffer = [&](const char *a_name, const D3D11_BUFFER_DESC &a_desc, winrt::com_ptr<ID3D11Buffer> &a_out) {
        const auto hr = device->CreateBuffer(&a_desc, nullptr, a_out.put());
        if (FAILED(hr))
        {
            return LogResourceFailure(a_name, hr);
        }
        return true;
    };

    auto createSRV = [&](const char *a_name, ID3D11Resource *a_resource, const D3D11_SHADER_RESOURCE_VIEW_DESC &a_desc,
                         winrt::com_ptr<ID3D11ShaderResourceView> &a_out) {
        const auto hr = device->CreateShaderResourceView(a_resource, &a_desc, a_out.put());
        if (FAILED(hr))
        {
            return LogResourceFailure(a_name, hr);
        }
        return true;
    };

    auto createUAV = [&](const char *a_name, ID3D11Resource *a_resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC &a_desc,
                         winrt::com_ptr<ID3D11UnorderedAccessView> &a_out) {
        const auto hr = device->CreateUnorderedAccessView(a_resource, &a_desc, a_out.put());
        if (FAILED(hr))
        {
            return LogResourceFailure(a_name, hr);
        }
        return true;
    };

    // Constant buffers
    {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.ByteWidth = sizeof(LightBuildingCB);
        if (!createBuffer("CreateBuffer(lightBuildingCB)", desc, lightBuildingCB))
            return;
    }
    {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.ByteWidth = sizeof(LightCullingCB);
        if (!createBuffer("CreateBuffer(lightCullingCB)", desc, lightCullingCB))
            return;
    }

    // Lights structured buffers (triple-buffered dynamic + Map(DISCARD):
    // a single buffer forces the driver to serialize with the previous frame's
    // consumer draws, stalling the CPU ~12ms and lowering GPU utilisation).
#if defined(FALLOUT_PRE_NG)
    for (std::uint32_t i = 0; i < kPreNGLightsBufferFrames; ++i)
    {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(LightData);
        desc.ByteWidth = static_cast<UINT>(kMaxLights * sizeof(LightData));
        if (!createBuffer("CreateBuffer(lightsBuffer)", desc, lightsBuffers[i]))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = kMaxLights;
        if (!createSRV("CreateShaderResourceView(lightsSRV)", lightsBuffers[i].get(), srvDesc, lightsSRVs[i]))
            return;
    }
#else
    {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(LightData);
        desc.ByteWidth = static_cast<UINT>(kMaxLights * sizeof(LightData));
        if (!createBuffer("CreateBuffer(lightsBuffer)", desc, lightsBuffer))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = kMaxLights;
        if (!createSRV("CreateShaderResourceView(lightsSRV)", lightsBuffer.get(), srvDesc, lightsSRV))
            return;
    }
#endif

    // Clusters structured buffer
    {
        std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(ClusterAABB);
        desc.ByteWidth = static_cast<UINT>(clusterCount * sizeof(ClusterAABB));
        if (!createBuffer("CreateBuffer(clustersBuffer)", desc, clustersBuffer))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = clusterCount;
        if (!createSRV("CreateShaderResourceView(clustersSRV)", clustersBuffer.get(), srvDesc, clustersSRV))
            return;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = clusterCount;
        if (!createUAV("CreateUnorderedAccessView(clustersUAV)", clustersBuffer.get(), uavDesc, clustersUAV))
            return;
    }

    // Light index counter
    {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(std::uint32_t);
        desc.ByteWidth = sizeof(std::uint32_t);
        if (!createBuffer("CreateBuffer(lightIndexCounterBuffer)", desc, lightIndexCounterBuffer))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = 1;
        if (!createSRV("CreateShaderResourceView(lightIndexCounterSRV)", lightIndexCounterBuffer.get(), srvDesc,
                       lightIndexCounterSRV))
            return;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = 1;
        if (!createUAV("CreateUnorderedAccessView(lightIndexCounterUAV)", lightIndexCounterBuffer.get(), uavDesc,
                       lightIndexCounterUAV))
            return;
    }

    // Light index list
    {
        std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(std::uint32_t);
        desc.ByteWidth = static_cast<UINT>(clusterCount * kClusterMaxLights * sizeof(std::uint32_t));
        if (!createBuffer("CreateBuffer(lightIndexListBuffer)", desc, lightIndexListBuffer))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
        if (!createSRV("CreateShaderResourceView(lightIndexListSRV)", lightIndexListBuffer.get(), srvDesc,
                       lightIndexListSRV))
            return;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
        if (!createUAV("CreateUnorderedAccessView(lightIndexListUAV)", lightIndexListBuffer.get(), uavDesc,
                       lightIndexListUAV))
            return;
    }

    // Light grid
    {
        std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(LightGrid);
        desc.ByteWidth = static_cast<UINT>(clusterCount * sizeof(LightGrid));
        if (!createBuffer("CreateBuffer(lightGridBuffer)", desc, lightGridBuffer))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = clusterCount;
        if (!createSRV("CreateShaderResourceView(lightGridSRV)", lightGridBuffer.get(), srvDesc, lightGridSRV))
            return;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = clusterCount;
        if (!createUAV("CreateUnorderedAccessView(lightGridUAV)", lightGridBuffer.get(), uavDesc, lightGridUAV))
            return;
    }

    if (!HasResources())
    {
        logger::error("[LightLimitFix] GPU resource creation finished with incomplete resources");
        return;
    }

    logger::info("[LightLimitFix] GPU resources created ({} clusters, {} max lights)",
                 clusterSize[0] * clusterSize[1] * clusterSize[2], kMaxLights);
}

void LightLimitFix::DataLoaded()
{
#if defined(FALLOUT_POST_AE)
    auto *setting = RE::GameSettingCollection::GetSingleton()->GetSetting("iMagicLightMaxCount");
    if (setting)
    {
        setting->SetInt(0x7FFFFFFF);
        logger::info("[LightLimitFix] Unlocked magic light limit");
    }
#endif
}

void LightLimitFix::PostPostLoad()
{
#if defined(FALLOUT_PRE_NG)
    LogPreNGDiagnosticEnvironmentSnapshot();
    if (ShouldBindPreNGBSLightingLLFVisibleConsumer() || ShouldBindPreNGDFLightForwardVisibleLLF())
    {
        // Normal-world batched per-item lighting setup. This is the hook that
        // fires during gameplay where SetupGeometry (vfunc 7) stays silent.
        // Serves both the legacy BSLighting consumer bind and the DFLight
        // forward clustered replacement.
        InstallPreNGBSLightingBatchHook();
    }
    const auto pointLightHookState = PreparePreNGPointLightHook();
    const auto setupGeometryHookState = ReadEnvironmentSwitch(kPreNGSetupGeometryHookOptInEnv);
    const auto bsLightingSetupGeometryResourceBindState =
        ReadEnvironmentSwitch(kPreNGBSLightingSetupGeometryResourceBindEnv);
    const bool setupGeometryRequested =
        setupGeometryHookState.enabled || bsLightingSetupGeometryResourceBindState.enabled;
    const bool setupGeometryAllowed = CanInstallPreNGSetupGeometryHooks(pointLightHookState);
    if (setupGeometryRequested && setupGeometryAllowed)
    {
        Hooks::Install(false);
        logger::info("[LightLimitFix] PreNG BSLightingShader SetupGeometry hook installed setupGeometryHookEnv={} "
                     "setupGeometryHook={} source={} bsLightingSetupGeometryResourceBindEnv={} "
                     "bsLightingResourceHook={} source={} pointLightState={}; scene-light decoder active with frame "
                     "budget {}, shader binding still uses explicit b3/t35-t37 gates",
                     kPreNGSetupGeometryHookOptInEnv, setupGeometryHookState.enabled ? "on" : "off",
                     EnvironmentSwitchSourceName(setupGeometryHookState.source),
                     kPreNGBSLightingSetupGeometryResourceBindEnv,
                     bsLightingSetupGeometryResourceBindState.enabled ? "on" : "off",
                     EnvironmentSwitchSourceName(bsLightingSetupGeometryResourceBindState.source),
                     PreNGPointLightHookStateName(pointLightHookState), GetPreNGSetupGeometryFrameBudget());
        return;
    }

    if (setupGeometryRequested && !setupGeometryAllowed)
    {
        logger::warn("[LightLimitFix] PreNG SetupGeometry hooks held despite {}=1 source={} pointLightState={}; "
                     "callsite evidence is not trusted",
                     kPreNGSetupGeometryHookOptInEnv, EnvironmentSwitchSourceName(setupGeometryHookState.source),
                     PreNGPointLightHookStateName(pointLightHookState));
    }
    else
    {
        switch (pointLightHookState)
        {
        case PreNGPointLightHookState::Installed:
            logger::warn(
                "[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal point-light "
                "hook diagnostic active and patch verified; set {}=1 for controlled SetupGeometry evidence",
                kPreNGSetupGeometryHookOptInEnv);
            break;
        case PreNGPointLightHookState::InstalledUnverified:
            logger::warn("[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal "
                         "point-light hook patch unverified; diagnostic evidence is not trusted");
            break;
        case PreNGPointLightHookState::Prepared:
            logger::info("[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal "
                         "point-light hook remains gated; set {}=1 for controlled SetupGeometry evidence",
                         kPreNGSetupGeometryHookOptInEnv);
            break;
        case PreNGPointLightHookState::Failed:
            logger::warn("[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal "
                         "point-light hook not prepared");
            break;
        }
    }
    return;
#elif defined(FALLOUT_POST_AE)
    // PostAE does not yet have a verified BSRenderPass light-data layout.
    // Keep the vfunc untouched until that runtime contract is established.
    logger::info("[LightLimitFix] PostAE SetupGeometry hook held; BSRenderPass light-data layout is unverified");
    return;
#else
    Hooks::Install();
#endif
}

#if defined(FALLOUT_PRE_NG)
std::uint32_t LightLimitFix::BeginPreNGClusterGpuTimer(ID3D11DeviceContext *a_context, ID3D11Device *a_device)
{
    if (!ShouldTimePreNGClusterPrepassGpu() || !a_context || !a_device)
    {
        return UINT32_MAX;
    }

    if (!preNGClusterGpuTimersReady)
    {
        D3D11_QUERY_DESC disjointDesc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        D3D11_QUERY_DESC tsDesc{D3D11_QUERY_TIMESTAMP, 0};
        bool ok = true;
        for (auto &timer : preNGClusterGpuTimers)
        {
            if (FAILED(a_device->CreateQuery(&disjointDesc, timer.disjoint.put())) ||
                FAILED(a_device->CreateQuery(&tsDesc, timer.begin.put())) ||
                FAILED(a_device->CreateQuery(&tsDesc, timer.end.put())))
            {
                ok = false;
                break;
            }
        }
        if (!ok)
        {
            static bool loggedFailure = false;
            if (!loggedFailure)
            {
                logger::warn("[LightLimitFix] PreNG GPU timing query creation failed; timing disabled this run");
                loggedFailure = true;
            }
            return UINT32_MAX;
        }
        preNGClusterGpuTimersReady = true;
    }

    const auto slot = preNGClusterGpuTimerIndex;
    auto &timer = preNGClusterGpuTimers[slot];
    a_context->Begin(timer.disjoint.get());
    a_context->End(timer.begin.get()); // timestamp queries record via End()
    return slot;
}

void LightLimitFix::EndPreNGClusterGpuTimer(ID3D11DeviceContext *a_context, std::uint32_t a_slot)
{
    if (a_slot >= kPreNGGpuTimerFrames || !a_context)
    {
        return;
    }
    auto &timer = preNGClusterGpuTimers[a_slot];
    a_context->End(timer.end.get());
    a_context->End(timer.disjoint.get());
    timer.pending = true;
    // Rotate to the other slot for the next timed frame so this frame's data has a
    // full frame to become available before we GetData it.
    preNGClusterGpuTimerIndex = (a_slot + 1) % kPreNGGpuTimerFrames;
}

void LightLimitFix::ResolvePreNGClusterGpuTimer(ID3D11DeviceContext *a_context, std::uint32_t a_slot,
                                                std::uint32_t a_frameNumber, std::uint32_t a_lightCount)
{
    if (a_slot >= kPreNGGpuTimerFrames || !a_context)
    {
        return;
    }

    // Read the OTHER buffer (previous timed frame) so GetData does not stall.
    const auto readSlot = (a_slot + 1) % kPreNGGpuTimerFrames;
    auto &timer = preNGClusterGpuTimers[readSlot];
    if (!timer.pending)
    {
        return;
    }

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
    if (a_context->GetData(timer.disjoint.get(), &disjointData, sizeof(disjointData), 0) != S_OK)
    {
        return; // not ready yet; try next timed frame
    }

    std::uint64_t beginTs = 0;
    std::uint64_t endTs = 0;
    if (a_context->GetData(timer.begin.get(), &beginTs, sizeof(beginTs), 0) != S_OK ||
        a_context->GetData(timer.end.get(), &endTs, sizeof(endTs), 0) != S_OK)
    {
        return;
    }

    timer.pending = false;

    if (disjointData.Disjoint || disjointData.Frequency == 0 || endTs <= beginTs)
    {
        return; // timing invalid this frame
    }

    const double ms = static_cast<double>(endTs - beginTs) * 1000.0 / static_cast<double>(disjointData.Frequency);
    logger::info("[LightLimitFix] PreNG cluster compute GPU time frame={} lights={} clusters={} gpuMs={:.3f}",
                 a_frameNumber, a_lightCount, clusterSize[0] * clusterSize[1] * clusterSize[2], ms);
}
#endif

// Dispatcher: the clustered compute can be submitted either from the default
// Main_RenderWorld_Start site (Prepass) or, when FO4CS_LLF_PRENG_PREPASS_EARLY_HOOK
// is set, from the Main_RenderShadowMaps phase (EarlyPrepass) so the dispatch
// overlaps the engine's shadow GPU batch and avoids the frame-start idle pocket
// that triggers the FrameGen-interop downclock. Exactly one site runs per frame.
void LightLimitFix::Prepass()
{
#if defined(FALLOUT_PRE_NG)
    if (ShouldSubmitPreNGClusterPrepassEarly())
    {
        return; // handled in EarlyPrepass() this run
    }
#endif
    RunClusterPrepass();
}

void LightLimitFix::EarlyPrepass()
{
#if defined(FALLOUT_PRE_NG)
    if (!ShouldSubmitPreNGClusterPrepassEarly())
    {
        return; // handled in Prepass() this run
    }
    RunClusterPrepass();
#endif
}

void LightLimitFix::RunClusterPrepass()
{
    const auto frameNumber = ++diagFrameCounter;

#if defined(FALLOUT_PRE_NG)
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    if (!runtime)
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: runtime unavailable");
        }
        return;
    }
    if (runtime->GetFrameCount() < kPreNGStableFrame)
    {
        if (frameNumber == 1)
        {
            logger::info("[LightLimitFix] PreNG Prepass waiting for stable frame gate ({})", kPreNGStableFrame);
        }
        return;
    }
    LogPreNGHookReachabilityWatchdog(runtime->GetFrameCount());
#endif

    if (!HasResources())
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: GPU resources are incomplete");
        }
        return;
    }

    auto *rendererData = fo4cs::GetRendererData();
    if (!rendererData)
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: renderer data unavailable");
        }
        return;
    }
    auto *context = reinterpret_cast<ID3D11DeviceContext *>(rendererData->context);
    if (!context)
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: D3D11 context unavailable");
        }
        return;
    }

    auto clearComputeBindings = [&] {
        ID3D11ShaderResourceView *nullSRVs[2]{};
        context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView *nullUAVs[3]{};
        context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
        ID3D11Buffer *nullCB = nullptr;
        context->CSSetConstantBuffers(0, 1, &nullCB);
        context->CSSetShader(nullptr, nullptr, 0);
    };
    auto clearPixelClusterSRVs = [&] {
        ID3D11ShaderResourceView *nullSRVs[3]{};
        context->PSSetShaderResources(35, ARRAYSIZE(nullSRVs), nullSRVs);
    };
    auto clearPixelLLFBindings = [&] {
        clearPixelClusterSRVs();
        ID3D11Buffer *nullCB = nullptr;
        context->PSSetConstantBuffers(3, 1, &nullCB);
    };

#if defined(FALLOUT_PRE_NG)
    // The consumer fallback alone is insufficient for 3D preview menus: the
    // clustered compute and b3/t35-t37 bindings otherwise continue to run every
    // frame behind vanilla BSLighting. Stop the whole LLF workload while the menu
    // is live, then resume only after the existing post-menu scene-state gate.
    if (ShouldDeferPreNGBSLightingResourceProofForMenu())
    {
        clearComputeBindings();
        clearPixelLLFBindings();
        seenLights.clear();
        seenThisPass.clear();
        seenCBHashes.clear();
        frameLights.clear();

        static std::atomic_uint32_t previewMenuPrepassHoldCount = 0;
        const auto holdIndex = ++previewMenuPrepassHoldCount;
        if (holdIndex <= 8 || (holdIndex & (holdIndex - 1)) == 0)
        {
            logger::info("[LightLimitFix] PreNG clustered Prepass held for UI preview holds={} frame={} reason={}; "
                         "compute and b3/t35-t37 cleared",
                         holdIndex, runtime->GetFrameCount(), GetPreNGBSLightingLastPreviewMenuReason());
        }
        return;
    }

    if (s_preNGBSLightingPreviewMenuResumePending.exchange(false, std::memory_order_relaxed))
    {
        s_preNGBSLightingPreviewMenuConsumerResumePending.store(true, std::memory_order_relaxed);
        logger::info("[LightLimitFix] PreNG UI preview closed; clustered Prepass resumed frame={} lastMenu={}; "
                     "awaiting llfConsumerComplete=true world bind",
                     runtime->GetFrameCount(), GetPreNGBSLightingLastPreviewMenuReason());
    }

    // Skyrim-parity step 1 (BOSS): no persistent/throttle distinction — the prepass
    // dispatches build+cull and binds t35-t37/b3 every frame via the plain path
    // below. If the proof gate says "don't run", clear and bail.
    if (!ShouldRunPreNGClusterPrepassProof())
    {
        clearPixelLLFBindings();
        seenLights.clear();
        seenThisPass.clear();
        seenCBHashes.clear();
        frameLights.clear();
        if (ShouldHoldPreNGDFLightPreparedState() && currentLightCount > 0)
        {
            static std::atomic_uint32_t holdCount = 0;
            const auto holdIndex = ++holdCount;
            if (holdIndex <= 8 || holdIndex % 512 == 0)
            {
                logger::info("[LightLimitFix] PreNG clustered Prepass prepared state retained for DFLight proof pass "
                             "holds={} lights={}",
                             holdIndex, currentLightCount);
            }
        }
        else
        {
            currentLightCount = 0;
            clusterPayloadCacheValid = false;
            clusterPayloadCache = {};
            shadowSceneFastReuseValid = false;
            shadowSceneFastReuse = {};
        }
        return;
    }
#endif

    const auto &gState = RE::BSGraphics::State::GetSingleton();
    const auto &camView = gState.cameraState.camViewData;

    DirectX::XMFLOAT4X4 projInvTransposed;
    {
        DirectX::XMMATRIX proj =
            DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4 *>(camView.projMat));
        DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
        DirectX::XMStoreFloat4x4(&projInvTransposed, DirectX::XMMatrixTranspose(invProj));
    }
    if (!IsFiniteMatrix(projInvTransposed))
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: camera projection matrix is not invertible");
        }
        return;
    }

    DirectX::XMFLOAT4X4 viewTransposed;
    DirectX::XMFLOAT4X4 viewMatrix;
    {
        viewMatrix = *reinterpret_cast<const DirectX::XMFLOAT4X4 *>(camView.viewMat);
        DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&viewMatrix);
        DirectX::XMStoreFloat4x4(&viewTransposed, DirectX::XMMatrixTranspose(view));
    }
    if (!IsFiniteMatrix(viewTransposed))
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: camera view matrix is invalid");
        }
        return;
    }

#if defined(FALLOUT_PRE_NG)
    // TEMP RE DUMP: log every camera matrix row once so the cluster-building
    // projection convention can be compared against vanilla DFLight's dual
    // inverse-projection rows (cb12[20..27]).
    if (frameNumber == 1 || frameNumber % 600 == 0)
    {
        const auto *viewData = std::addressof(camView);
        auto dumpMatrix = [&](const char *a_name, const __m128 *a_rows) {
            const auto *floats = reinterpret_cast<const float *>(a_rows);
            logger::info("[LightLimitFix] PreNG camera matrix dump frame={} name={} "
                         "m00={:.6f} m01={:.6f} m02={:.6f} m03={:.6f} m10={:.6f} m11={:.6f} m12={:.6f} m13={:.6f} "
                         "m20={:.6f} m21={:.6f} m22={:.6f} m23={:.6f} m30={:.6f} m31={:.6f} m32={:.6f} m33={:.6f}",
                         frameNumber, a_name,
                         floats[0], floats[1], floats[2], floats[3],
                         floats[4], floats[5], floats[6], floats[7],
                         floats[8], floats[9], floats[10], floats[11],
                         floats[12], floats[13], floats[14], floats[15]);
        };
        dumpMatrix("viewMat", viewData->viewMat);
        dumpMatrix("projMat", viewData->projMat);
        dumpMatrix("viewProjMat", viewData->viewProjMat);
        dumpMatrix("viewProjUnjittered", viewData->viewProjUnjittered);
        dumpMatrix("currentViewProjUnjittered", viewData->currentViewProjUnjittered);
        dumpMatrix("inv1stPersonProjMat", viewData->inv1stPersonProjMat);
        logger::info("[LightLimitFix] PreNG camera matrix dump frame={} near={} far={}",
                     frameNumber, CameraNear, CameraFar);
    }
#endif

#if defined(FALLOUT_PRE_NG)
    if (ShouldBindPreNGDFLightForwardVisibleLLF())
    {
        // Keeps the DFLight forward consumer cb3 (inverse-view + cluster z
        // domain) fresh for the per-frame clustered pass replacement. The
        // cluster z domain now matches vanilla cb12: z in [1, 333333].
        UpdatePreNGDFLightForwardCameraCB(viewMatrix, 1.0f, 333333.0f);
    }
#endif

#if defined(FALLOUT_PRE_NG)
    std::vector<LightData> preNGSceneLightFallback;
    preNGSceneLightFallback.swap(frameLights);

    seenLights.clear();
    seenThisPass.clear();

    if (CollectLightsFromPreNGShadowScene() == 0)
    {
        if (!preNGSceneLightFallback.empty())
        {
            frameLights.swap(preNGSceneLightFallback);

            static std::atomic_uint32_t fallbackUseCount = 0;
            const auto fallbackIndex = ++fallbackUseCount;
            if (fallbackIndex <= 8 || fallbackIndex % 512 == 0)
            {
                logger::info("[LightLimitFix] PreNG scene-light fallback feeds clustered prepass uses={} lights={}",
                             fallbackIndex, static_cast<std::uint32_t>(frameLights.size()));
            }
        }
        else
        {
            CollectLightsFromScene();
        }
    }
#else
    if (!seenLights.empty())
    {
        CollectLightsFromBSLight();
    }
    else
    {
        CollectLightsFromScene();
    }
#endif

    currentLightCount = static_cast<std::uint32_t>(frameLights.size());
#if defined(FALLOUT_PRE_NG)
    const auto preNGClusterPayloadCurrent =
        MakePreNGClusterPayloadCacheState(frameLights, currentLightCount, viewTransposed, CameraNear, CameraFar,
                                          clusterSize);
    // Skyrim-parity step 1 (BOSS): never reuse the cached payload — dispatch the
    // cluster build+cull compute EVERY frame like Skyrim CS. The ViewHash/inputs
    // reuse was the mechanism that, combined with periodic resubmit, produced the
    // GPU power-state event; Skyrim avoids it by simply always dispatching.
#endif

    if (frameNumber % 300 == 0)
    {
        logger::info("[LightLimitFix] frame={} lights={} clusters={}x{}x{} near={:.1f} far={:.0f}", frameNumber,
                     currentLightCount, clusterSize[0], clusterSize[1], clusterSize[2], CameraNear, CameraFar);
    }

    seenLights.clear();
    seenCBHashes.clear();

#if defined(FALLOUT_PRE_NG)
    // Skyrim-parity: always run the compute submission block (no payload-reuse skip).
#endif
    {

#if defined(FALLOUT_PRE_NG)
        auto *timingDevice = reinterpret_cast<ID3D11Device *>(rendererData->device);
        const auto gpuTimerSlot = BeginPreNGClusterGpuTimer(context, timingDevice);
#endif

        if (currentLightCount > 0)
        {
            // Replicate the EXACT transform vanilla sub_1428C37A0 uses for
            // cb2[1]: read the renderer-base camera position (+8736) and the
            // view rows (base + 7024 + 114..117 * 16), then
            // viewPos = (lightWorld - camWorld) * viewRows (row-vector, with
            // perspective divide). This is the only source guaranteed to match
            // the space of vanilla cb2[1].
            const auto rendererBase = GetPreNGDFLightRendererStateBase();
            DirectX::XMFLOAT4X4 viewRows{};
            bool viewRowsValid = false;
            DirectX::XMFLOAT3 camPos{};
            bool camPosValid = false;
            if (rendererBase != 0 && F4Runtime::IsReadableAddress(rendererBase + 7024 + 114 * 16, 4 * 16) &&
                F4Runtime::IsReadableAddress(rendererBase + 8736, sizeof(float) * 3))
            {
                std::memcpy(&viewRows, reinterpret_cast<const void *>(rendererBase + 7024 + 114 * 16), sizeof(viewRows));
                std::memcpy(&camPos, reinterpret_cast<const void *>(rendererBase + 8736), sizeof(float) * 3);
                viewRowsValid = true;
                camPosValid = true;
            }
            if (!viewRowsValid || !camPosValid)
            {
                // Fallback: previous camViewData-based rotation, so lights keep
                // a consistent (if not vanilla-exact) space rather than garbage.
                const auto &gfxState = RE::BSGraphics::State::GetSingleton();
                viewRows = *reinterpret_cast<const DirectX::XMFLOAT4X4 *>(gfxState.cameraState.camViewData.viewMat);
                const auto &p = gfxState.cameraState.posAdjust;
                camPos = DirectX::XMFLOAT3{ p.x, p.y, p.z };
            }

            preNGDFLightLastSnapshotCameraPos = DirectX::XMFLOAT4{ camPos.x, camPos.y, camPos.z, 0.0f };
            preNGDFLightLastSnapshotViewRows = viewRows;
            preNGDFLightLastSnapshotViewValid = viewRowsValid && camPosValid;

            DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&viewRows);
            DirectX::XMVECTOR camPosV = DirectX::XMLoadFloat3(&camPos);
            for (auto &light : frameLights)
            {
                DirectX::XMFLOAT3 worldPos{
                    light.positionWS[0].data.x,
                    light.positionWS[0].data.y,
                    light.positionWS[0].data.z };
                DirectX::XMVECTOR rel = DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&worldPos), camPosV);
                DirectX::XMVECTOR viewPos = DirectX::XMVector3TransformCoord(rel, view);
                DirectX::XMStoreFloat3(
                    reinterpret_cast<DirectX::XMFLOAT3 *>(&light.positionWS[1].data),
                    viewPos);
                light.positionWS[1].pad = 0;
            }

            const auto lightUploadBytes = static_cast<UINT>(currentLightCount * sizeof(LightData));
#if defined(FALLOUT_PRE_NG)
            currentLightsBufferIndex = (currentLightsBufferIndex + 1) % kPreNGLightsBufferFrames;
            auto &lightsBuffer = lightsBuffers[currentLightsBufferIndex];
            D3D11_MAPPED_SUBRESOURCE lightMapped{};
            if (SUCCEEDED(context->Map(lightsBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &lightMapped)))
            {
                std::memcpy(lightMapped.pData, frameLights.data(), lightUploadBytes);
                context->Unmap(lightsBuffer.get(), 0);
            }
#else
            D3D11_MAPPED_SUBRESOURCE lightMapped{};
            if (SUCCEEDED(context->Map(lightsBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &lightMapped)))
            {
                std::memcpy(lightMapped.pData, frameLights.data(), lightUploadBytes);
                context->Unmap(lightsBuffer.get(), 0);
            }
#endif
        }

#if defined(FALLOUT_PRE_NG)
        if (currentLightCount > 0)
        {
            static std::atomic_uint32_t nonZeroClusterUploadCount = 0;
            const auto uploadIndex = ++nonZeroClusterUploadCount;
            if (uploadIndex <= 8 || uploadIndex % 512 == 0)
            {
                logger::info("[LightLimitFix] PreNG clustered prepass uploaded uploads={} frame={} lights={} clusters={}",
                             uploadIndex, frameNumber, currentLightCount,
                             clusterSize[0] * clusterSize[1] * clusterSize[2]);
            }
        }
#endif

        LightBuildingCB buildingCBData{};
        buildingCBData.LightsNear = 1.0f;
        buildingCBData.LightsFar = 333333.0f;
        buildingCBData.pad0[0] = buildingCBData.pad0[1] = 0;
        buildingCBData.ClusterSize[0] = clusterSize[0];
        buildingCBData.ClusterSize[1] = clusterSize[1];
        buildingCBData.ClusterSize[2] = clusterSize[2];
        buildingCBData.ClusterSize[3] = 0;
        std::memcpy(&buildingCBData.CameraProjInverse, &projInvTransposed, sizeof(projInvTransposed));

        bool rebuildClusterAABBs = true;
#if defined(FALLOUT_PRE_NG)
        rebuildClusterAABBs =
            !clusterBuildCacheValid || !PreNGClusterBuildInputsMatch(clusterBuildCache, buildingCBData);
        // The vanilla camera cb12 (rows 20..27) is captured on the FIRST DFLight
        // batch pass, which happens AFTER the first Prepass. Building AABBs
        // before that capture would reconstruct corners from an all-zero b12 and
        // then get cached as "valid", poisoning every later frame. Defer the
        // build until the capture exists, and never cache the empty result.
        if (rebuildClusterAABBs && !s_preNGDFLightCameraCB)
        {
            rebuildClusterAABBs = false;
            clusterBuildCacheValid = false;
        }
#endif

        if (rebuildClusterAABBs)
        {
            D3D11_MAPPED_SUBRESOURCE mapped;
            const auto hr = context->Map(lightBuildingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(hr))
            {
                LogResourceFailure("Map(lightBuildingCB)", hr);
                clearComputeBindings();
                return;
            }
            std::memcpy(mapped.pData, &buildingCBData, sizeof(buildingCBData));
            context->Unmap(lightBuildingCB.get(), 0);

            context->CSSetShader(clusterBuildingCS.get(), nullptr, 0);
            ID3D11Buffer *cbPtr = lightBuildingCB.get();
            context->CSSetConstantBuffers(0, 1, &cbPtr);
#if defined(FALLOUT_PRE_NG)
            if (s_preNGDFLightCameraCB)
            {
                ID3D11Buffer *cameraCB = s_preNGDFLightCameraCB.get();
                context->CSSetConstantBuffers(12, 1, &cameraCB);
            }
#endif
            ID3D11UnorderedAccessView *buildingUAVs[] = {clustersUAV.get()};
            context->CSSetUnorderedAccessViews(0, 1, buildingUAVs, nullptr);
            context->Dispatch(clusterSize[0], clusterSize[1], clusterSize[2]);
#if defined(FALLOUT_PRE_NG)
            clusterBuildCache.LightsNear = buildingCBData.LightsNear;
            clusterBuildCache.LightsFar = buildingCBData.LightsFar;
            for (std::uint32_t i = 0; i < 4; ++i)
            {
                clusterBuildCache.ClusterSize[i] = buildingCBData.ClusterSize[i];
            }
            clusterBuildCacheValid = true;

            static std::atomic_uint32_t clusterBuildRebuildCount = 0;
            const auto rebuildIndex = ++clusterBuildRebuildCount;
            if (rebuildIndex <= 8 || rebuildIndex % 128 == 0)
            {
                logger::info("[LightLimitFix] PreNG clustered Prepass rebuilt cluster AABBs rebuilds={} frame={} "
                             "clusters={} near={:.3f} far={:.1f}",
                             rebuildIndex, frameNumber, clusterSize[0] * clusterSize[1] * clusterSize[2], CameraNear,
                             CameraFar);
            }
#endif
            clearComputeBindings();
        }
#if defined(FALLOUT_PRE_NG)
        else
        {
            static std::atomic_uint32_t clusterBuildReuseCount = 0;
            const auto reuseIndex = ++clusterBuildReuseCount;
            if (reuseIndex <= 8 || reuseIndex % 128 == 0)
            {
                logger::info("[LightLimitFix] PreNG clustered Prepass reused cluster AABBs reuses={} frame={} "
                             "clusters={} stableKey=near/far/cluster-size tolerance={}",
                             reuseIndex, frameNumber, clusterSize[0] * clusterSize[1] * clusterSize[2],
                             kPreNGClusterBuildReuseTolerance);
            }
        }
#endif

        {
            D3D11_MAPPED_SUBRESOURCE mapped;
            const auto hr = context->Map(lightCullingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(hr))
            {
                LogResourceFailure("Map(lightCullingCB)", hr);
                clearComputeBindings();
                return;
            }
            auto *cb = static_cast<LightCullingCB *>(mapped.pData);
            cb->LightCount = currentLightCount;
            cb->pad[0] = cb->pad[1] = cb->pad[2] = 0;
            cb->ClusterSize[0] = clusterSize[0];
            cb->ClusterSize[1] = clusterSize[1];
            cb->ClusterSize[2] = clusterSize[2];
            cb->ClusterSize[3] = 0;
            // Culling must use the SAME camera the positionWS[1] fill just used.
            // The TLS rows can advance between those two points, which puts the
            // light grid in a different space than both cb2[1] and the payload.
            // Reuse the snapshot taken above; fall back to a fresh read only if
            // that snapshot was never populated.
            {
                DirectX::XMFLOAT4X4 viewRows = preNGDFLightLastSnapshotViewRows;
                DirectX::XMFLOAT3 camPos{
                    preNGDFLightLastSnapshotCameraPos.x,
                    preNGDFLightLastSnapshotCameraPos.y,
                    preNGDFLightLastSnapshotCameraPos.z };
                bool valid = preNGDFLightLastSnapshotViewValid;
                if (!valid)
                {
                    const auto rendererBase = GetPreNGDFLightRendererStateBase();
                    valid = rendererBase != 0 &&
                        F4Runtime::IsReadableAddress(rendererBase + 7024 + 114 * 16, 4 * 16) &&
                        F4Runtime::IsReadableAddress(rendererBase + 8736, sizeof(float) * 3);
                    if (valid)
                    {
                        std::memcpy(&viewRows, reinterpret_cast<const void *>(rendererBase + 7024 + 114 * 16), sizeof(viewRows));
                        std::memcpy(&camPos, reinterpret_cast<const void *>(rendererBase + 8736), sizeof(camPos));
                    }
                }
                if (!valid)
                {
                    const auto &gfxState = RE::BSGraphics::State::GetSingleton();
                    viewRows = *reinterpret_cast<const DirectX::XMFLOAT4X4 *>(gfxState.cameraState.camViewData.viewMat);
                    const auto &p = gfxState.cameraState.posAdjust;
                    camPos = DirectX::XMFLOAT3{ p.x, p.y, p.z };
                }
                DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&viewRows);
                DirectX::XMFLOAT4X4 viewUpload{};
                DirectX::XMStoreFloat4x4(&viewUpload, DirectX::XMMatrixTranspose(view));
                std::memcpy(&cb->CameraView, &viewUpload, sizeof(viewUpload));
                cb->CameraPos = DirectX::XMFLOAT4{ camPos.x, camPos.y, camPos.z, 0.0f };
            }
            context->Unmap(lightCullingCB.get(), 0);

            ID3D11ShaderResourceView *cullingSRVs[] = {clustersSRV.get(), GetCurrentLightsSRV()};
            context->CSSetShaderResources(0, 2, cullingSRVs);

            ID3D11UnorderedAccessView *cullingUAVs[] = {lightIndexCounterUAV.get(), lightIndexListUAV.get(),
                                                        lightGridUAV.get()};
            context->CSSetUnorderedAccessViews(0, 3, cullingUAVs, nullptr);

            context->CSSetShader(clusterCullingCS.get(), nullptr, 0);
            ID3D11Buffer *cullCBPtr = lightCullingCB.get();
            context->CSSetConstantBuffers(0, 1, &cullCBPtr);

            context->Dispatch((clusterSize[0] + NUMTHREAD_X - 1) / NUMTHREAD_X,
                              (clusterSize[1] + NUMTHREAD_Y - 1) / NUMTHREAD_Y,
                              (clusterSize[2] + NUMTHREAD_Z - 1) / NUMTHREAD_Z);
        }

        clearComputeBindings();

#if defined(FALLOUT_PRE_NG)
        if (gpuTimerSlot != UINT32_MAX)
        {
            EndPreNGClusterGpuTimer(context, gpuTimerSlot);
            ResolvePreNGClusterGpuTimer(context, gpuTimerSlot, frameNumber, currentLightCount);
        }
        clusterPayloadCache = preNGClusterPayloadCurrent;
        clusterPayloadCacheValid = true;
#endif
    }
    frameLights.clear();

#if defined(FALLOUT_PRE_NG)
    if (ShouldBindPreNGPrepassResources())
    {
        if (ShouldBindPreNGClusterSRVs())
        {
            ID3D11ShaderResourceView *views[3]{GetCurrentLightsSRV(), lightIndexListSRV.get(), lightGridSRV.get()};
            context->PSSetShaderResources(35, ARRAYSIZE(views), views);

            static std::atomic_uint32_t prepassBindCount = 0;
            const auto prepassBindIndex = ++prepassBindCount;
            if (prepassBindIndex <= 8 || prepassBindIndex % 512 == 0)
            {
                logger::info("[LightLimitFix] PreNG cluster SRVs bound to PS t35-t37 from Prepass "
                             "binds={} frame={} lights={} clusters={}",
                             prepassBindIndex, frameNumber, currentLightCount,
                             clusterSize[0] * clusterSize[1] * clusterSize[2]);
            }
        }
    }
    TryBindPreNGBSLightingDeferredDescriptorResources(*this);
#else
    if (frameNumber >= 3 && currentLightCount > 0)
    {
        ID3D11ShaderResourceView *views[3]{GetCurrentLightsSRV(), lightIndexListSRV.get(), lightGridSRV.get()};
        context->PSSetShaderResources(35, ARRAYSIZE(views), views);

        if (frameNumber % 300 == 0)
        {
            logger::info("[LightLimitFix] SRVs bound to PS slots t35-t37 ({} lights, {} clusters)", currentLightCount,
                         clusterSize[0] * clusterSize[1] * clusterSize[2]);
        }
    }
#endif
}

bool LightLimitFix::HasResources() const
{
#if defined(FALLOUT_PRE_NG)
    for (std::uint32_t i = 0; i < kPreNGLightsBufferFrames; ++i)
    {
        if (!lightsBuffers[i] || !lightsSRVs[i])
        {
            return false;
        }
    }
#endif
    return clusterBuildingCS && clusterCullingCS && lightBuildingCB && lightCullingCB &&
#if !defined(FALLOUT_PRE_NG)
           lightsBuffer && lightsSRV &&
#endif
           clustersBuffer && clustersSRV && clustersUAV && lightIndexCounterBuffer && lightIndexCounterSRV &&
           lightIndexCounterUAV && lightIndexListBuffer && lightIndexListSRV && lightIndexListUAV && lightGridBuffer &&
           lightGridSRV && lightGridUAV;
}

ID3D11ShaderResourceView *LightLimitFix::GetCurrentLightsSRV()
{
#if defined(FALLOUT_PRE_NG)
    return lightsSRVs[currentLightsBufferIndex % kPreNGLightsBufferFrames].get();
#else
    return GetCurrentLightsSRV();
#endif
}

#if defined(FALLOUT_PRE_NG)
bool LightLimitFix::HasPreNGDFLightDescriptorConsumerData() const
{
    return HasResources() && currentLightCount > 0;
}

bool LightLimitFix::HasPreNGDFCompositeDescriptorConsumerData() const
{
    return HasResources() && currentLightCount > 0;
}

bool LightLimitFix::HasPreNGBSLightingDescriptorConsumerData() const
{
    return HasResources() && currentLightCount > 0;
}

bool LightLimitFix::ShouldSuppressPreNGBSLightingVisibleConsumerForMenu() const
{
    if (ShouldAllowPreNGBSLightingConsumerBindInMenu())
    {
        return false;
    }

    const auto menuBlock = DetectPreNGBSLightingResourceProofMenuBlock();
    if (menuBlock.empty())
    {
        return false;
    }

    // Some preview menus do not execute the world EarlyPrepass/Prepass hooks at
    // all. Arm recovery from the consumer/point-light suppression path as well,
    // otherwise the menu can be correctly protected but the post-menu recovery
    // chain never emits its "resumed" and "recovery complete" proof markers.
    s_preNGBSLightingPreviewMenuResumePending.store(true, std::memory_order_relaxed);

    const auto suppressIndex = ++s_preNGBSLightingVisibleConsumerMenuSuppressLogs;
    if (suppressIndex <= 8 || (suppressIndex & (suppressIndex - 1)) == 0)
    {
        auto *runtime = CommunityShaders::Runtime::GetSingleton();
        const auto frame = runtime ? runtime->GetFrameCount() : 0;
        logger::info("[LightLimitFix] PreNG BSLighting visible consumer held for UI menu suppressions={} frame={} "
                     "reason={}; vanilla BSLighting preserved",
                     suppressIndex, frame, menuBlock);
    }

    return true;
}

void LightLimitFix::NotifyPreNGBSLightingVisibleConsumerResumeComplete()
{
    if (!s_preNGBSLightingPreviewMenuConsumerResumePending.exchange(false, std::memory_order_relaxed))
    {
        return;
    }

    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    logger::info("[LightLimitFix] PreNG UI preview LLF recovery complete frame={} lastMenu={} llfConsumerComplete=true "
                 "clusterSRVs=true lights={}",
                 runtime ? runtime->GetFrameCount() : 0, GetPreNGBSLightingLastPreviewMenuReason(), currentLightCount);
}

void LightLimitFix::NotifyPreNGDFLightLLFConsumerDescriptorObserved(std::uint32_t a_vertexDescriptor,
                                                                    std::uint32_t a_pixelDescriptor, bool a_found,
                                                                    std::uintptr_t a_pixelShader)
{
    s_preNGDFLightLLFConsumerDescriptorObserved.store(true, std::memory_order_relaxed);

    const auto observation = ++s_preNGDFLightLLFConsumerDescriptorObservations;
    if (observation <= 8 || observation % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG DFLight LLF consumer descriptor observed observations={} vsDesc=0x{:X} "
                     "psDesc=0x{:X} vanillaFound={} ownedPS=0x{:X}; clustered Prepass demand can start on the next "
                     "frame while resource sub-gates remain enabled",
                     observation, a_vertexDescriptor, a_pixelDescriptor, a_found, a_pixelShader);
    }
}

bool LightLimitFix::HasPreNGDFLightLLFConsumerDescriptorObserved() const
{
    return s_preNGDFLightLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
}

void LightLimitFix::NotifyPreNGDFCompositeLLFConsumerDescriptorObserved(std::uint32_t a_vertexDescriptor,
                                                                        std::uint32_t a_pixelDescriptor, bool a_found,
                                                                        std::uintptr_t a_vanillaPixelShader,
                                                                        std::uintptr_t a_ownedPixelShader)
{
    s_preNGDFCompositeLLFConsumerDescriptorObserved.store(true, std::memory_order_relaxed);

    const auto observation = ++s_preNGDFCompositeLLFConsumerDescriptorObservations;
    if (observation <= 8 || observation % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG DFComposite LLF consumer descriptor observed observations={} vsDesc=0x{:X} "
                     "psDesc=0x{:X} vanillaFound={} vanillaPS=0x{:X} ownedPS=0x{:X}; clustered Prepass demand can "
                     "start on the next frame while resource sub-gates remain enabled",
                     observation, a_vertexDescriptor, a_pixelDescriptor, a_found, a_vanillaPixelShader,
                     a_ownedPixelShader);
    }
}

bool LightLimitFix::HasPreNGDFCompositeLLFConsumerDescriptorObserved() const
{
    return s_preNGDFCompositeLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
}

void LightLimitFix::NotifyPreNGBSLightingLLFConsumerDescriptorObserved(std::uint32_t a_vertexDescriptor,
                                                                       std::uint32_t a_pixelDescriptor, bool a_found,
                                                                       std::uintptr_t a_vanillaPixelShader)
{
    s_preNGBSLightingLLFConsumerDescriptorObserved.store(true, std::memory_order_relaxed);
    s_preNGBSLightingLLFConsumerLastVertexDescriptor.store(a_vertexDescriptor, std::memory_order_relaxed);
    s_preNGBSLightingLLFConsumerLastPixelDescriptor.store(a_pixelDescriptor, std::memory_order_relaxed);
    s_preNGBSLightingLLFConsumerLastFound.store(a_found, std::memory_order_relaxed);
    s_preNGBSLightingLLFConsumerLastVanillaPixelShader.store(a_vanillaPixelShader, std::memory_order_relaxed);
    ShouldDeferPreNGBSLightingResourceProofForMenu();
    ExtendPreNGBSLightingResourceProofDescriptorSettle();

    const auto observation = ++s_preNGBSLightingLLFConsumerDescriptorObservations;
    if (observation <= 8 || observation % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG BSLighting LLF resource descriptor observed observations={} vsDesc=0x{:X} "
                     "psDesc=0x{:X} vanillaFound={} vanillaPS=0x{:X}; finite clustered Prepass demand can start after "
                     "BSLighting descriptor/menu settle while resource sub-gates remain enabled, unless the descriptor "
                     "path is suppressed after LockpickingMenu",
                     observation, a_vertexDescriptor, a_pixelDescriptor, a_found, a_vanillaPixelShader);
    }
}

bool LightLimitFix::HasPreNGBSLightingLLFConsumerDescriptorObserved() const
{
    return s_preNGBSLightingLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
}
#endif

void LightLimitFix::Reset()
{
    if (!HasResources())
        return;

    auto *rendererData = fo4cs::GetRendererData();
    if (!rendererData)
        return;
    auto *context = reinterpret_cast<ID3D11DeviceContext *>(rendererData->context);
    if (!context)
        return;

    ID3D11ShaderResourceView *nullViews[3]{};
    context->PSSetShaderResources(35, 3, nullViews);
}

void LightLimitFix::CollectLightsFromPass(RE::BSRenderPass *a_pass)
{
    if (!a_pass)
        return;

    const RE::FO4Runtime::PreNGBSRenderPassView passView{a_pass};
    auto *lightData = passView.GetShaderPropertyLightData();
    if (!lightData || lightData->lightList.empty())
        return;

    for (auto *light : lightData->lightList)
    {
        if (!light || seenLights.contains(light))
            continue;
        seenLights.insert(light);
        seenThisPass.push_back(light);
    }
}

#if defined(FALLOUT_PRE_NG)
std::uint32_t LightLimitFix::CollectLightsFromPreNGSceneLights(RE::BSRenderPass *a_pass,
                                                               std::uint32_t a_requestedLightCount,
                                                               std::uint32_t a_shadowArg)
{
    if (!a_pass)
    {
        return 0;
    }

    const auto passAddress = reinterpret_cast<std::uintptr_t>(a_pass);
    const F4Runtime::PreNGBSRenderPassView passView{a_pass};

    std::uintptr_t sceneLightsAddress = 0;
    std::uint8_t rawLightCount = 0;
    if (!passView.ReadSceneLights(sceneLightsAddress) || !passView.ReadRawLightCount(rawLightCount) ||
        sceneLightsAddress == 0 || rawLightCount <= kPreNGBSRenderPassSceneLightFirstIndex)
    {
        return 0;
    }

    // FO4 vanilla passes (pass->numLights - 1) into the point-light writer and
    // reads physical sceneLights entries starting at index 1.
    auto availableLightCount = static_cast<std::uint32_t>(rawLightCount - kPreNGBSRenderPassSceneLightFirstIndex);
    if (a_requestedLightCount < availableLightCount)
    {
        availableLightCount = a_requestedLightCount;
    }

    std::uint32_t collected = 0;
    std::uint32_t missingEntryCount = 0;
    std::uint32_t missingWrapperDataCount = 0;
    std::uint32_t invalidNiLightDataCount = 0;
    std::uint32_t inactiveLightDataCount = 0;
    std::uint32_t duplicateLightCount = 0;
    std::uint32_t unreadableShadowMaskCount = 0;
    std::uint32_t invalidShadowMaskCount = 0;

    for (std::uint32_t i = 0; i < availableLightCount && frameLights.size() < kMaxLights; ++i)
    {
        std::uintptr_t wrapperAddress = 0;
        if (!passView.ReadSceneLightWrapper(i, wrapperAddress) || wrapperAddress == 0)
        {
            ++missingEntryCount;
            continue;
        }

        LightData data{};
        std::uintptr_t niLightAddress = 0;
        bool shadowMaskUnreadable = false;
        bool shadowMaskInvalid = false;
        std::uint32_t shadowMaskBit = 0;
        const auto decodeResult = DecodePreNGBSLightWrapper(wrapperAddress, data, niLightAddress, shadowMaskUnreadable,
                                                            shadowMaskInvalid, shadowMaskBit);
        if (decodeResult == PreNGLightDecodeResult::MissingWrapperData)
        {
            ++missingWrapperDataCount;
            continue;
        }
        if (decodeResult == PreNGLightDecodeResult::InvalidNiLightData)
        {
            ++invalidNiLightDataCount;
            continue;
        }
        if (decodeResult == PreNGLightDecodeResult::NonContributingLightData)
        {
            ++inactiveLightDataCount;
            continue;
        }

        if (shadowMaskUnreadable)
        {
            ++unreadableShadowMaskCount;
        }
        else if (shadowMaskInvalid)
        {
            ++invalidShadowMaskCount;
        }

        auto *lightKey = reinterpret_cast<RE::BSLight *>(niLightAddress);
        if (frameLights.size() >= kMaxLights || seenLights.contains(lightKey))
        {
            ++duplicateLightCount;
            continue;
        }

        seenLights.insert(lightKey);
        seenThisPass.push_back(lightKey);
        frameLights.push_back(data);
        ++collected;
    }

    static std::atomic_uint32_t decodeDiagCount = 0;
    if (availableLightCount > 0 && (collected > 0 || missingEntryCount > 0 || missingWrapperDataCount > 0 ||
                                    invalidNiLightDataCount > 0 || inactiveLightDataCount > 0))
    {
        const auto diagIndex = ++decodeDiagCount;
        if (diagIndex <= 8 || diagIndex % 512 == 0)
        {
            logger::info(
                "[LightLimitFix] PreNG scene-light decode pass=0x{:X} table=0x{:X} raw={} requested={} available={} "
                "collected={} shadowArg={} skips(entry={}, wrapper={}, niLight={}, "
                "inactive={}, duplicate={}, shadowMaskUnreadable={}, shadowMaskInvalid={})",
                passAddress, sceneLightsAddress, static_cast<std::uint32_t>(rawLightCount), a_requestedLightCount,
                availableLightCount, collected, a_shadowArg, missingEntryCount, missingWrapperDataCount,
                invalidNiLightDataCount, inactiveLightDataCount, duplicateLightCount, unreadableShadowMaskCount,
                invalidShadowMaskCount);
        }
    }

    return collected;
}
#endif

std::uint32_t LightLimitFix::CollectLightsFromPreNGShadowScene()
{
    std::uintptr_t activeLightsAddress = 0;
    std::uintptr_t activeShadowLightsAddress = 0;
    std::uintptr_t activeExtraLightsAddress = 0;
    std::uint32_t activeLightCount = 0;
    std::uint32_t activeShadowLightCount = 0;
    std::uint32_t activeExtraLightCount = 0;
    const auto shadowSceneNodeRef = GetPreNGWorldShadowSceneNode();
    const auto shadowSceneNode = shadowSceneNodeRef.node;

    auto logFailure = [&](const char *a_reason) {
        shadowSceneFastReuseValid = false;
        shadowSceneFastReuse = {};
        static std::atomic_uint32_t failureCount = 0;
        const auto failureIndex = ++failureCount;
        if (failureIndex <= 8 || failureIndex % 512 == 0)
        {
            logger::warn(
                "[LightLimitFix] PreNG shadow-scene light decode held failures={} reason={} node=0x{:X} "
                "selectedIndex={} currentIndex={} currentIndexRead={} fallback={} active=(ptr=0x{:X}, count={}) "
                "shadow=(ptr=0x{:X}, count={}) extra=(ptr=0x{:X}, count={})",
                failureIndex, a_reason, shadowSceneNode, static_cast<std::uint32_t>(shadowSceneNodeRef.selectedIndex),
                static_cast<std::uint32_t>(shadowSceneNodeRef.currentIndex), shadowSceneNodeRef.currentIndexRead,
                shadowSceneNodeRef.usedFallback, activeLightsAddress, activeLightCount, activeShadowLightsAddress,
                activeShadowLightCount, activeExtraLightsAddress, activeExtraLightCount);
        }
    };

    if (shadowSceneNode == 0)
    {
        logFailure("world-shadow-scene-node-unavailable");
        return 0;
    }

    F4Runtime::PreNGShadowSceneBuckets buckets{};
    const F4Runtime::PreNGShadowSceneNodeView shadowSceneView{shadowSceneNode};
    if (!shadowSceneView.ReadBuckets(buckets))
    {
        logFailure("arrays-unreadable");
        return 0;
    }
    activeLightsAddress = buckets.active.entries;
    activeLightCount = buckets.active.count;
    activeShadowLightsAddress = buckets.shadow.entries;
    activeShadowLightCount = buckets.shadow.count;
    activeExtraLightsAddress = buckets.extra.entries;
    activeExtraLightCount = buckets.extra.count;

    if (activeLightCount > kPreNGMaxShadowSceneActiveLights ||
        activeShadowLightCount > kPreNGMaxShadowSceneActiveLights ||
        activeExtraLightCount > kPreNGMaxShadowSceneActiveLights)
    {
        logFailure("count-out-of-range");
        return 0;
    }

    if ((activeLightCount > 0 && activeLightsAddress == 0) ||
        (activeShadowLightCount > 0 && activeShadowLightsAddress == 0) ||
        (activeExtraLightCount > 0 && activeExtraLightsAddress == 0))
    {
        logFailure("array-null");
        return 0;
    }

    const auto decodeActiveLightCount = std::min(activeLightCount, kPreNGMaxShadowSceneDecodeLights);
    const auto decodeActiveShadowLightCount = std::min(activeShadowLightCount, kPreNGMaxShadowSceneDecodeLights);
    const auto decodeActiveExtraLightCount = std::min(activeExtraLightCount, kPreNGMaxShadowSceneDecodeLights);
    const auto totalBucketCount = static_cast<std::uint64_t>(activeLightCount) +
                                  static_cast<std::uint64_t>(activeShadowLightCount) +
                                  static_cast<std::uint64_t>(activeExtraLightCount);
    const bool shadowSceneDecodeTruncated =
        activeLightCount != decodeActiveLightCount || activeShadowLightCount != decodeActiveShadowLightCount ||
        activeExtraLightCount != decodeActiveExtraLightCount || totalBucketCount > kPreNGMaxShadowSceneDecodeLights;
    if (shadowSceneDecodeTruncated)
    {
        static std::atomic_uint32_t truncationCount = 0;
        const auto truncationIndex = ++truncationCount;
        if (truncationIndex <= 8 || truncationIndex % 512 == 0)
        {
            logger::info(
                "[LightLimitFix] PreNG shadow-scene decode truncating oversized buckets truncations={} active={}=>{} "
                "shadow={}=>{} extra={}=>{} total={} decodeCapacity={} hardMax={}; LLF payload remains bounded",
                truncationIndex, activeLightCount, decodeActiveLightCount, activeShadowLightCount,
                decodeActiveShadowLightCount, activeExtraLightCount, decodeActiveExtraLightCount, totalBucketCount,
                kPreNGMaxShadowSceneDecodeLights, kPreNGMaxShadowSceneActiveLights);
        }
    }

    ShadowSceneFastReuseKey fastReuseKey{};
    const bool fastReuseEnabled = ShouldReusePreNGShadowSceneFastReuse();
    const auto fastReuseRefreshInterval = fastReuseEnabled ? GetPreNGShadowSceneFastReuseRefreshInterval() : 0;
    bool fastReuseKeyReady = false;
    if (fastReuseEnabled)
    {
        fastReuseKeyReady = MakePreNGShadowSceneFastReuseKey(shadowSceneNodeRef, buckets, fastReuseKey);
        if (!fastReuseKeyReady)
        {
            shadowSceneFastReuseValid = false;
            shadowSceneFastReuse = {};
        }
        else if (shadowSceneFastReuseValid &&
                 SamePreNGShadowSceneFastReuseStructure(shadowSceneFastReuse.Key, fastReuseKey) &&
                 shadowSceneFastReuse.ReuseAge < fastReuseRefreshInterval)
        {
            frameLights = shadowSceneFastReuse.Lights;
            ++shadowSceneFastReuse.ReuseAge;

            static std::atomic_uint32_t fastReuseCount = 0;
            const auto reuseIndex = ++fastReuseCount;
            if (reuseIndex <= 8 || reuseIndex % 128 == 0)
            {
                logger::info("[LightLimitFix] PreNG ShadowScene fast-reused decoded lights reuses={} age={} "
                             "refreshInterval={} node=0x{:X} active=(ptr=0x{:X}, count={}, hash=0x{:016X}) "
                             "shadow=(ptr=0x{:X}, count={}, hash=0x{:016X}) extra=(ptr=0x{:X}, count={}, "
                             "hash=0x{:016X}) lights={} lightsHash=0x{:016X}",
                             reuseIndex, shadowSceneFastReuse.ReuseAge, fastReuseRefreshInterval,
                             shadowSceneFastReuse.Key.Node, shadowSceneFastReuse.Key.ActiveEntries,
                             shadowSceneFastReuse.Key.ActiveCount, shadowSceneFastReuse.Key.ActiveHash,
                             shadowSceneFastReuse.Key.ShadowEntries, shadowSceneFastReuse.Key.ShadowCount,
                             shadowSceneFastReuse.Key.ShadowHash, shadowSceneFastReuse.Key.ExtraEntries,
                             shadowSceneFastReuse.Key.ExtraCount, shadowSceneFastReuse.Key.ExtraHash,
                             shadowSceneFastReuse.LightCount, shadowSceneFastReuse.LightsHash);
            }
            return shadowSceneFastReuse.LightCount;
        }
    }

    std::uint32_t collected = 0;
    std::uint32_t missingEntryCount = 0;
    std::uint32_t missingWrapperDataCount = 0;
    std::uint32_t invalidNiLightDataCount = 0;
    std::uint32_t inactiveLightDataCount = 0;
    std::uint32_t duplicateLightCount = 0;
    std::uint32_t unreadableShadowMaskCount = 0;
    std::uint32_t invalidShadowMaskCount = 0;

    auto decodeBucket = [&](const F4Runtime::PreNGShadowSceneBucket &a_bucket, std::uint32_t a_count) {
        for (std::uint32_t i = 0; i < a_count && frameLights.size() < kMaxLights; ++i)
        {
            std::uintptr_t wrapperAddress = 0;
            const auto entryAddress = a_bucket.entries + (static_cast<std::uintptr_t>(i) * sizeof(std::uintptr_t));
            ReadPreNGRaw(entryAddress, wrapperAddress);
            if (wrapperAddress == 0)
            {
                ++missingEntryCount;
                continue;
            }

            LightData data{};
            std::uintptr_t niLightAddress = 0;
            bool shadowMaskUnreadable = false;
            bool shadowMaskInvalid = false;
            std::uint32_t shadowMaskBit = 0;
            const auto decodeResult = DecodePreNGBSLightWrapper(wrapperAddress, data, niLightAddress,
                                                                shadowMaskUnreadable, shadowMaskInvalid, shadowMaskBit);
            if (decodeResult == PreNGLightDecodeResult::MissingWrapperData)
            {
                ++missingWrapperDataCount;
                continue;
            }
            if (decodeResult == PreNGLightDecodeResult::InvalidNiLightData)
            {
                ++invalidNiLightDataCount;
                continue;
            }
            if (decodeResult == PreNGLightDecodeResult::NonContributingLightData)
            {
                ++inactiveLightDataCount;
                continue;
            }

            if (shadowMaskUnreadable)
            {
                ++unreadableShadowMaskCount;
            }
            else if (shadowMaskInvalid)
            {
                ++invalidShadowMaskCount;
            }

            auto *lightKey = reinterpret_cast<RE::BSLight *>(niLightAddress);
            if (frameLights.size() >= kMaxLights || seenLights.contains(lightKey))
            {
                ++duplicateLightCount;
                continue;
            }

            seenLights.insert(lightKey);
            seenThisPass.push_back(lightKey);
            frameLights.push_back(data);
            ++collected;
        }
    };

    decodeBucket(buckets.active, decodeActiveLightCount);
    decodeBucket(buckets.shadow, decodeActiveShadowLightCount);
    decodeBucket(buckets.extra, decodeActiveExtraLightCount);

    static std::atomic_uint32_t decodeDiagCount = 0;
    const auto diagIndex = ++decodeDiagCount;
    if (diagIndex <= 8 || diagIndex % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG shadow-scene light decode node=0x{:X} selectedIndex={} currentIndex={} "
                     "currentIndexRead={} fallback={} active=(ptr=0x{:X}, count={}, decode={}) shadow=(ptr=0x{:X}, "
                     "count={}, decode={}) extra=(ptr=0x{:X}, count={}, decode={}) truncated={} collected={} "
                     "skips(entry={}, wrapper={}, niLight={}, inactive={}, duplicate={}, "
                     "shadowMaskUnreadable={}, shadowMaskInvalid={})",
                     shadowSceneNode, static_cast<std::uint32_t>(shadowSceneNodeRef.selectedIndex),
                     static_cast<std::uint32_t>(shadowSceneNodeRef.currentIndex), shadowSceneNodeRef.currentIndexRead,
                     shadowSceneNodeRef.usedFallback, activeLightsAddress, activeLightCount, decodeActiveLightCount,
                     activeShadowLightsAddress, activeShadowLightCount, decodeActiveShadowLightCount,
                     activeExtraLightsAddress, activeExtraLightCount, decodeActiveExtraLightCount,
                     shadowSceneDecodeTruncated, collected, missingEntryCount, missingWrapperDataCount,
                     invalidNiLightDataCount, inactiveLightDataCount, duplicateLightCount, unreadableShadowMaskCount,
                     invalidShadowMaskCount);
    }

    if (fastReuseEnabled)
    {
        const auto cachedLightCount = static_cast<std::uint32_t>(frameLights.size());
        if (fastReuseKeyReady && cachedLightCount > 0)
        {
            const auto lightsHash = frameLights.empty()
                                        ? kPreNGFNVOffsetBasis
                                        : HashPreNGBytes(frameLights.data(), frameLights.size() * sizeof(LightData));
            const bool stableWithPrevious =
                shadowSceneFastReuseValid && SamePreNGShadowSceneFastReuseKey(shadowSceneFastReuse.Key, fastReuseKey) &&
                shadowSceneFastReuse.LightCount == cachedLightCount &&
                shadowSceneFastReuse.LightsHash == lightsHash;

            auto stableDecodeCount = stableWithPrevious ? shadowSceneFastReuse.StableDecodeCount : 0u;
            if (stableDecodeCount != 0xFFFFFFFFu)
            {
                ++stableDecodeCount;
            }

            shadowSceneFastReuse.Key = fastReuseKey;
            shadowSceneFastReuse.Lights = frameLights;
            shadowSceneFastReuse.LightCount = cachedLightCount;
            shadowSceneFastReuse.LightsHash = lightsHash;
            shadowSceneFastReuse.StableDecodeCount = stableDecodeCount;
            shadowSceneFastReuse.ReuseAge = 0;
            shadowSceneFastReuseValid = true;

            static std::atomic_uint32_t fastReuseCaptureCount = 0;
            const auto captureIndex = ++fastReuseCaptureCount;
            if (captureIndex <= 8 || captureIndex % 512 == 0)
            {
                logger::info("[LightLimitFix] PreNG ShadowScene fast-reuse captured decode captures={} "
                             "stableDecodes={} refreshInterval={} node=0x{:X} lights={} "
                             "lightsHash=0x{:016X}",
                             captureIndex, shadowSceneFastReuse.StableDecodeCount, fastReuseRefreshInterval,
                             shadowSceneFastReuse.Key.Node, shadowSceneFastReuse.LightCount,
                             shadowSceneFastReuse.LightsHash);
            }
        }
        else
        {
            shadowSceneFastReuseValid = false;
            shadowSceneFastReuse = {};
        }
    }

    return collected;
}

bool LightLimitFix::BindPreNGClusterSRVsToPixelShader(RE::BSRenderPass *a_pass, std::uint32_t a_requestedLightCount,
                                                      bool a_strictCBBound)
{
    (void)a_strictCBBound;
    if (!ShouldBindPreNGClusterSRVs())
    {
        return false;
    }

    auto logBindFailure = [&](const char *a_reason) {
        static std::atomic_uint32_t failureCount = 0;
        const auto failureIndex = ++failureCount;
        if (failureIndex <= 8 || failureIndex % 512 == 0)
        {
            logger::warn("[LightLimitFix] PreNG cluster SRV t35-t37 bind held failures={} reason={} pass=0x{:X} "
                         "requested={} clusterLights={}",
                         failureIndex, a_reason, reinterpret_cast<std::uintptr_t>(a_pass), a_requestedLightCount,
                         currentLightCount);
        }
    };

    if (currentLightCount == 0)
    {
        logBindFailure("cluster-prepass-not-ready");
        return false;
    }

    if (!HasResources())
    {
        logBindFailure("gpu-resources-incomplete");
        return false;
    }

    auto *rendererData = fo4cs::GetRendererData();
    if (!rendererData)
    {
        logBindFailure("renderer-data-unavailable");
        return false;
    }
    auto *context = reinterpret_cast<ID3D11DeviceContext *>(rendererData->context);
    if (!context)
    {
        logBindFailure("context-unavailable");
        return false;
    }

    ID3D11ShaderResourceView *views[3]{GetCurrentLightsSRV(), lightIndexListSRV.get(), lightGridSRV.get()};
    context->PSSetShaderResources(35, ARRAYSIZE(views), views);

    static std::atomic_uint32_t bindCount = 0;
    const auto bindIndex = ++bindCount;
    if (bindIndex <= 8 || bindIndex % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG cluster SRVs bound to PS t35-t37 binds={} pass=0x{:X} requested={} "
                     "clusterLights={} clusters={}",
                     bindIndex, reinterpret_cast<std::uintptr_t>(a_pass), a_requestedLightCount,
                     currentLightCount, clusterSize[0] * clusterSize[1] * clusterSize[2]);
    }

    if (currentLightCount > 0)
    {
        static std::atomic_uint32_t nonZeroBindCount = 0;
        const auto nonZeroBindIndex = ++nonZeroBindCount;
        if (nonZeroBindIndex <= 8 || nonZeroBindIndex % 512 == 0)
        {
            logger::info("[LightLimitFix] PreNG cluster SRVs nonzero bind proof nonzeroBinds={} binds={} pass=0x{:X} "
                         "requested={} clusterLights={} clusters={}",
                         nonZeroBindIndex, bindIndex, reinterpret_cast<std::uintptr_t>(a_pass), a_requestedLightCount,
                         currentLightCount, clusterSize[0] * clusterSize[1] * clusterSize[2]);
        }
    }

    return true;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDescriptorResourcesToPixelShader(
    const char *a_sourceName)
{
    PreNGDFLightResourceBindingState state{};
    state.lightCount = currentLightCount;
    state.strictLightCount = 0;
    state.shadowBitMask = 0;
    state.strictCBBound = false;
    const char *sourceName = a_sourceName ? a_sourceName : "descriptor";

    state.clusterSRVsBound = BindPreNGClusterSRVsToPixelShader(nullptr, currentLightCount, false);

    static std::atomic_uint32_t descriptorResourceBindCount = 0;
    const auto bindIndex = ++descriptorResourceBindCount;
    if (bindIndex <= 8 || bindIndex % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG {} resources bound binds={} lights={} strictCB={} clusterSRVs={}",
                     sourceName, bindIndex, state.lightCount, state.strictCBBound, state.clusterSRVsBound);
    }

    return state;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightDescriptorResourcesToPixelShader()
{
    return BindPreNGDescriptorResourcesToPixelShader("DFLight descriptor");
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFCompositeDescriptorResourcesToPixelShader()
{
    return BindPreNGDescriptorResourcesToPixelShader("DFComposite descriptor");
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGBSLightingDescriptorResourcesToPixelShader()
{
    return BindPreNGDescriptorResourcesToPixelShader("BSLighting descriptor");
}

#if defined(FALLOUT_PRE_NG)
const char *GetPreNGBSLightingSetupGeometryPreviewReasonName(std::uint32_t a_reason)
{
    if (a_reason > 0 && a_reason <= kPreNGBSLightingSetupGeometryPreviewMenus.size())
    {
        return kPreNGBSLightingSetupGeometryPreviewMenus[a_reason - 1].data();
    }

    if (a_reason == kPreNGBSLightingSetupGeometryWorkshopPreviewReason)
    {
        return "WorkshopMenu3D";
    }

    return "none";
}

std::uint32_t DetectPreNGBSLightingSetupGeometryPreviewReason()
{
    auto *ui = RE::UI::GetSingleton();
    if (!ui)
    {
        return 0;
    }

    for (std::uint32_t i = 0; i < kPreNGBSLightingSetupGeometryPreviewMenus.size(); ++i)
    {
        if (ui->GetMenuOpen(kPreNGBSLightingSetupGeometryPreviewMenus[i].data()))
        {
            return i + 1;
        }
    }

    const auto workshopMenu = ui->GetMenu<RE::WorkshopMenu>();
    if (workshopMenu.get() && workshopMenu->OnStack())
    {
        const bool hasWorkshop3DPreview =
            workshopMenu->displayGeometry.get() || !workshopMenu->displayItemModels.empty() ||
            workshopMenu->inv3DModelManager.itemBase || workshopMenu->inv3DModelManager.tempRef;
        if (hasWorkshop3DPreview)
        {
            return kPreNGBSLightingSetupGeometryWorkshopPreviewReason;
        }
    }

    return 0;
}

std::uint32_t GetCachedPreNGBSLightingSetupGeometryPreviewReason()
{
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    if (!runtime)
    {
        return DetectPreNGBSLightingSetupGeometryPreviewReason();
    }

    const auto frame = runtime->GetFrameCount();
    const auto cachedFrame = s_preNGBSLightingSetupGeometryPreviewCacheFrame.load(std::memory_order_acquire);
    if (cachedFrame == frame)
    {
        return s_preNGBSLightingSetupGeometryPreviewCacheReason.load(std::memory_order_acquire);
    }

    const auto reason = DetectPreNGBSLightingSetupGeometryPreviewReason();
    s_preNGBSLightingSetupGeometryPreviewCacheReason.store(reason, std::memory_order_release);
    s_preNGBSLightingSetupGeometryPreviewCacheFrame.store(frame, std::memory_order_release);
    return reason;
}
#endif

bool LightLimitFix::ShouldProcessPreNGBSLightingSetupGeometryProof() const
{
    if (ShouldBindPreNGSetupGeometryStrictLightCB() || ShouldPersistPreNGSetupGeometryStrictLightCB() ||
        ShouldUpdatePreNGStrictLightCB())
    {
        return true;
    }

    if (!ShouldBindPreNGBSLightingSetupGeometryResources())
    {
        return true;
    }

    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    if (!runtime)
    {
        return true;
    }

    const auto frame = runtime->GetFrameCount();
    const auto bypassUntil = s_preNGBSLightingSetupGeometryBypassUntilFrame.load(std::memory_order_relaxed);
    if (frame < bypassUntil)
    {
        const auto bypassIndex = ++s_preNGBSLightingSetupGeometryBypassLogs;
        if (bypassIndex <= 8 || (bypassIndex & (bypassIndex - 1)) == 0)
        {
            logger::info(
                "[LightLimitFix] PreNG BSLighting SetupGeometry proof bypass active bypasses={} frame={} until={}",
                bypassIndex, frame, bypassUntil);
        }
        return false;
    }

    return true;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGBSLightingSetupGeometryResources(
    RE::BSRenderPass *a_pass)
{
    PreNGDFLightResourceBindingState state{};
    state.lightCount = currentLightCount;
    state.strictLightCount = 0;
    state.shadowBitMask = 0;

    if (!ShouldBindPreNGBSLightingSetupGeometryResources())
    {
        return state;
    }
    if (ShouldSuppressPreNGBSLightingVisibleConsumerForMenu())
    {
        return state;
    }

    static std::atomic_bool setupGeometryResourceProofComplete = false;
    if (setupGeometryResourceProofComplete.load(std::memory_order_relaxed))
    {
        return state;
    }

    currentLightCount = static_cast<std::uint32_t>(frameLights.size());
    state.lightCount = currentLightCount;
    state.strictLightCount = 0;
    state.shadowBitMask = 0;
    if (currentLightCount == 0)
    {
        state.strictLightCount = 0;
        state.shadowBitMask = 0;
        ExtendPreNGBSLightingSetupGeometryBypassWindow();
        if (TryReservePreNGBSLightingSetupGeometryNoLightProbeFrame())
        {
            static std::atomic_uint32_t noLightFrameCount = 0;
            const auto noLightFrameIndex = ++noLightFrameCount;
            if (noLightFrameIndex <= 8 || (noLightFrameIndex & (noLightFrameIndex - 1)) == 0)
            {
                logger::info(
                    "[LightLimitFix] PreNG BSLighting SetupGeometry resource proof held for no-light frame frames={} "
                    "pass=0x{:X}; descriptor/resource demand remains idle until scene lights are collected",
                    noLightFrameIndex, reinterpret_cast<std::uintptr_t>(a_pass));
            }
        }
        return state;
    }

    const auto currentPixelShader = ReadPreNGCurrentPixelShaderEntryState();
    NotifyPreNGBSLightingLLFConsumerDescriptorObserved(0, currentPixelShader.id, currentPixelShader.d3dObject != 0,
                                                       currentPixelShader.d3dObject);

    if (!HasPreNGBSLightingDescriptorConsumerData())
    {
        static std::atomic_uint32_t pendingCount = 0;
        const auto pendingIndex = ++pendingCount;
        if (pendingIndex <= 8 || pendingIndex % 512 == 0)
        {
            logger::info("[LightLimitFix] PreNG BSLighting SetupGeometry resource bind pending payload pending={} "
                         "pass=0x{:X} currentPS=0x{:X} currentPSId=0x{:X} lights={}",
                         pendingIndex, reinterpret_cast<std::uintptr_t>(a_pass), currentPixelShader.d3dObject,
                         currentPixelShader.id, currentLightCount);
        }
        return state;
    }

    state = BindPreNGDescriptorResourcesToPixelShader("BSLighting SetupGeometry");

    static std::atomic_uint32_t auditCount = 0;
    const auto auditIndex = ++auditCount;
    if (auditIndex <= 8 || auditIndex % 512 == 0)
    {
        TracePreNGActiveLightingBindings("bslighting-setup-geometry-resource-bind",
                                         static_cast<std::int32_t>(F4Runtime::PreNG::BS_LIGHTING_SHADER_TYPE), 0,
                                         currentPixelShader.id, currentPixelShader.d3dObject != 0,
                                         currentPixelShader.d3dObject);
    }

    if (state.clusterSRVsBound &&
        !setupGeometryResourceProofComplete.exchange(true, std::memory_order_relaxed))
    {
        logger::info("[LightLimitFix] PreNG BSLighting SetupGeometry resource-only proof reached t35-t37 completion "
                     "on current BSLighting path; future SetupGeometry resource binds are held until a visible-safe "
                     "consumer is implemented");
    }

    return state;
}

// Per-draw visible-consumer bind driven by BSLighting SetupGeometry or the
// normal-world batched per-item setup (sub_1428C37A0). Swaps the current pixel
// shader to the ShaderCache consumer PS and re-asserts t35-t37.
void LightLimitFix::TryBindPreNGBSLightingVisibleConsumerFromSetupGeometry(
    RE::BSShader *a_shader,
    const char *a_sourceName)
{
    if (!ShouldBindPreNGBSLightingLLFVisibleConsumer() || !a_shader)
    {
        return;
    }
    if (a_shader->shaderType != static_cast<std::int32_t>(F4Runtime::PreNG::BS_LIGHTING_SHADER_TYPE))
    {
        return;
    }
    if (!HasPreNGBSLightingDescriptorConsumerData())
    {
        return;
    }
    if (ShouldSuppressPreNGBSLightingVisibleConsumerForMenu())
    {
        return;
    }

    const auto pixelState = ReadPreNGCurrentPixelShaderEntryState();
    const auto pixelDescriptor = pixelState.id;
    const bool isContractDescriptor = F4Runtime::PreNG::IsBSLightingContractPixelDescriptor(pixelDescriptor);
    const bool isWorldDescriptor =
        !isContractDescriptor &&
        ShouldBindPreNGBSLightingWorldConsumer() &&
        IsPlausiblePreNGBSLightingPixelDescriptor(pixelDescriptor);
    if (!isContractDescriptor && !isWorldDescriptor)
    {
        // "Last mile" learn-only path: normal-world BSLighting draws resolve the
        // lighting PS through descriptors outside the five menu-preview contract
        // values. Record them so the next run can generalize the bind; do not
        // bind on this run.
        if (ShouldObservePreNGBSLightingWorldDescriptors() &&
            IsPlausiblePreNGBSLightingPixelDescriptor(pixelDescriptor))
        {
            ObservePreNGBSLightingWorldDescriptor(pixelDescriptor);
        }
        return;
    }

    auto *consumerShader =
        CommunityShaders::ShaderCache::GetSingleton()->GetPixelShader(*a_shader, pixelDescriptor);
    const auto consumerPSD3D = consumerShader ? reinterpret_cast<std::uintptr_t>(consumerShader->shader) : 0;
    const auto pixelEntry = reinterpret_cast<std::uintptr_t>(consumerShader);
    if (!consumerShader || consumerPSD3D == 0 || pixelEntry == 0)
    {
        return;
    }

    const auto vertexEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
    const auto hullEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
    const auto domainEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
    if (vertexEntry == 0)
    {
        return;
    }

    const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
    const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
    if (!F4Runtime::IsReadableAddress(bindAddr, 16) || !F4Runtime::IsWritableAddress(pixelGlobal, sizeof(std::uintptr_t)))
    {
        return;
    }
    if (!F4Runtime::WriteValue(pixelGlobal, pixelEntry))
    {
        return;
    }

    using PreNGBindShadersFn = void *(*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
    auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
    bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);

    BindPreNGDescriptorResourcesToPixelShader("BSLighting SetupGeometry consumer");

    static std::atomic_uint32_t bindCount = 0;
    const auto bindIndex = ++bindCount;
    if (isWorldDescriptor)
    {
        s_preNGBSLightingWorldBindCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (bindIndex <= 8 || (bindIndex & (bindIndex - 1)) == 0)
    {
        logger::info("[LightLimitFix] PreNG BSLighting LLF consumer bound via {} binds={} shaderType={} "
                     "descriptor=0x{:X} llfConsumerComplete=true worldBind={} lights={}",
                     a_sourceName, bindIndex, static_cast<std::int32_t>(a_shader->shaderType), pixelDescriptor,
                     isWorldDescriptor, currentLightCount);
    }
}

#if defined(FALLOUT_PRE_NG)
namespace
{
    struct PreNGDFLightForwardZeroShaderState
    {
        bool attempted = false;
        winrt::com_ptr<ID3D11PixelShader> shader;
        RE::BSGraphics::PixelShader entry{};
    };
    std::mutex s_preNGDFLightForwardZeroLock;
    PreNGDFLightForwardZeroShaderState s_preNGDFLightForwardZeroState;
}

RE::BSGraphics::PixelShader *GetPreNGDFLightForwardZeroPixelShader()
{
    std::scoped_lock lock(s_preNGDFLightForwardZeroLock);
    auto &state = s_preNGDFLightForwardZeroState;
    if (state.shader && state.entry.shader)
    {
        return std::addressof(state.entry);
    }
    if (state.attempted)
    {
        return nullptr;
    }
    state.attempted = true;

    auto *device = CommunityShaders::Runtime::GetSingleton()->GetDevice();
    if (!device)
    {
        logger::warn("[LightLimitFix] PreNG DFLight forward zero PS create failed reason=device-unavailable");
        return nullptr;
    }

    auto bytecode = CommunityShaders::ShaderCompiler::GetSingleton()->CompileFromFile(
        "LightLimitFix/DFLightZeroOutputPS.hlsl", "ps_5_0", nullptr, "main");
    if (!bytecode)
    {
        logger::warn("[LightLimitFix] PreNG DFLight forward zero PS create failed reason=compile-failed");
        return nullptr;
    }

    ID3D11PixelShader *shader = nullptr;
    const auto hr = device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, &shader);
    if (FAILED(hr) || !shader)
    {
        logger::warn("[LightLimitFix] PreNG DFLight forward zero PS create failed bytecode={} hr=0x{:08X}",
                     bytecode->size(), static_cast<std::uint32_t>(hr));
        return nullptr;
    }

    state.shader.attach(shader);
    state.entry.id = F4Runtime::PreNG::DF_LIGHT_FORWARD_PIXEL_DESCRIPTOR_8004;
    state.entry.shader = state.shader.get();
    logger::info("[LightLimitFix] PreNG DFLight forward zero PS created psD3D=0x{:X}",
                 reinterpret_cast<std::uintptr_t>(state.shader.get()));
    return std::addressof(state.entry);
}

void LightLimitFix::UpdatePreNGDFLightForwardCameraCB(
    const DirectX::XMFLOAT4X4 &a_viewMatrix,
    float a_cameraNear,
    float a_cameraFar)
{
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    auto *device = runtime ? runtime->GetDevice() : nullptr;
    if (!device)
    {
        return;
    }

    if (!dflightForwardCB)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 96;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&desc, nullptr, dflightForwardCB.put())))
        {
            logger::warn("[LightLimitFix] PreNG DFLight forward cb3 create failed");
            return;
        }
    }

    auto *rendererData = fo4cs::GetRendererData();
    auto *context = rendererData ? reinterpret_cast<ID3D11DeviceContext *>(rendererData->context) : nullptr;
    if (!context)
    {
        return;
    }

    DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&a_viewMatrix);
    DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
    DirectX::XMFLOAT4X4 invViewTransposed;
    DirectX::XMStoreFloat4x4(&invViewTransposed, DirectX::XMMatrixTranspose(invView));

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(dflightForwardCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        struct PreNGDFLightForwardCBData
        {
            float invView[4][4];
            float cameraNear;
            float cameraFar;
            float pad[2];
        };
        auto *data = static_cast<PreNGDFLightForwardCBData *>(mapped.pData);
        std::memcpy(data->invView, &invViewTransposed, sizeof(invViewTransposed));
        data->cameraNear = a_cameraNear;
        data->cameraFar = a_cameraFar;
        data->pad[0] = 0.0f;
        data->pad[1] = 0.0f;
        context->Unmap(dflightForwardCB.get(), 0);
    }
}

#if defined(FALLOUT_PRE_NG)
namespace
{
    struct PreNGDFLightCB12CaptureState
    {
        bool captured = false;
        winrt::com_ptr<ID3D11Buffer> staging;
    };
    std::mutex s_preNGDFLightCB12CaptureLock;
    PreNGDFLightCB12CaptureState s_preNGDFLightCB12CaptureState;

    // Capture the vanilla DFLight camera cb12 (slot 12) on the first batch pass
    // where it is bound. ClusterBuildingCS reads rows 20..27 from this copy.
    void CapturePreNGDFLightCameraCBOnce()
    {
        if (s_preNGDFLightCameraCBCaptured.load(std::memory_order_acquire))
        {
            return;
        }
        std::scoped_lock lock(s_preNGDFLightCB12CaptureLock);
        auto &state = s_preNGDFLightCB12CaptureState;
        if (state.captured)
        {
            return;
        }

        auto *rendererData = fo4cs::GetRendererData();
        auto *context = rendererData ? reinterpret_cast<ID3D11DeviceContext *>(rendererData->context) : nullptr;
        auto *device = rendererData ? reinterpret_cast<ID3D11Device *>(rendererData->device) : nullptr;
        if (!context || !device)
        {
            return;
        }

        ID3D11Buffer *cb12 = nullptr;
        context->PSGetConstantBuffers(12, 1, &cb12);
        if (!cb12)
        {
            return;
        }

        D3D11_BUFFER_DESC desc{};
        cb12->GetDesc(&desc);
        cb12->Release();
        if (desc.ByteWidth == 0 || desc.ByteWidth > 4096)
        {
            return;
        }

        D3D11_BUFFER_DESC stagingDesc{};
        stagingDesc.ByteWidth = desc.ByteWidth;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, state.staging.put())))
        {
            return;
        }

        ID3D11Buffer *cb12Again = nullptr;
        context->PSGetConstantBuffers(12, 1, &cb12Again);
        if (!cb12Again)
        {
            return;
        }
        context->CopyResource(state.staging.get(), cb12Again);
        if (!s_preNGDFLightCameraCBCaptured.exchange(true, std::memory_order_relaxed))
        {
            s_preNGDFLightCameraCB.copy_from(cb12Again);
        }
        cb12Again->Release();
        state.captured = true;
    }

}
#endif


void LightLimitFix::HandlePreNGDFLightForwardBatchPostCall(RE::BSShader *a_shader)
{
    if (!ShouldBindPreNGDFLightForwardVisibleLLF() || !a_shader)
    {
        return;
    }
    if (a_shader->shaderType != static_cast<std::int32_t>(F4Runtime::PreNG::DF_LIGHTING_SHADER_TYPE))
    {
        return;
    }
    if (GetCachedPreNGBSLightingSetupGeometryPreviewReason() != 0)
    {
        // Menu 3D previews keep the vanilla BSLighting path; do not double-light.
        return;
    }

    const auto pixelState = ReadPreNGCurrentPixelShaderEntryState();
    if (!F4Runtime::PreNG::IsDFLightForwardPixelDescriptor(pixelState.id))
    {
        return;
    }

#if defined(FALLOUT_PRE_NG)
    // Never replace a vanilla pass while the camera cb12 used to build cluster
    // AABBs has not been captured yet; the clusters would be garbage/empty and
    // every vanilla point-light pass would be zeroed or mis-culled.
    if (!s_preNGDFLightCameraCB)
    {
        return;
    }
#endif

    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    if (!runtime)
    {
        return;
    }

    static std::atomic_uint64_t s_lastDFLightConsumerBoundFrame = UINT64_MAX;
    static std::atomic_uint64_t s_lastDFLightConsumerAttemptFrame = UINT64_MAX;
    const auto boundFrame = s_lastDFLightConsumerBoundFrame.load(std::memory_order_relaxed);
    auto attemptFrame = s_lastDFLightConsumerAttemptFrame.load(std::memory_order_relaxed);
    const auto frame = runtime->GetFrameCount();

    RE::BSGraphics::PixelShader *pixelEntry = nullptr;
    bool bindConsumer = false;
    PreNGDFLightResourceBindingState consumerResourceState{};
    if (boundFrame == frame)
    {
        // Consumer already emitted this frame's clustered list; zero the
        // remaining vanilla point-light passes so they do not double-count.
        pixelEntry = GetPreNGDFLightForwardZeroPixelShader();
        if (!pixelEntry)
        {
            return;
        }
    }
    else if (attemptFrame != frame)
    {
        // First eligible item of the frame. Try once per frame; on failure the
        // whole frame stays vanilla (never zero passes without a consumer).
        if (!s_lastDFLightConsumerAttemptFrame.compare_exchange_strong(
                attemptFrame, frame, std::memory_order_relaxed))
        {
            return;
        }

        // Preflight: replacing vanilla point-light passes only makes sense when
        // the clustered payload is actually live. A held prepass (menu preview,
        // shadow-scene overload gate) leaves currentLightCount = 0; in that
        // case keep every pass vanilla so the scene is never blacked out.
        if (currentLightCount == 0)
        {
            static std::atomic_uint32_t emptyPayloadHoldCount = 0;
            const auto holdIndex = ++emptyPayloadHoldCount;
            if (holdIndex <= 8 || (holdIndex & (holdIndex - 1)) == 0)
            {
                logger::info("[LightLimitFix] PreNG DFLight forward replacement held holds={} frame={} "
                             "reason=clustered-payload-empty lights={}",
                             holdIndex, frame, currentLightCount);
            }
            return;
        }
        // Vanilla cb2[1] matches the prepass camera payload; keep the grid in
        // that same space.
        consumerResourceState = BindPreNGDescriptorResourcesToPixelShader("DFLight forward preflight");
        if (!consumerResourceState.clusterSRVsBound)
        {
            static std::atomic_uint32_t resourceHoldCount = 0;
            const auto holdIndex = ++resourceHoldCount;
            if (holdIndex <= 8 || (holdIndex & (holdIndex - 1)) == 0)
            {
                logger::info("[LightLimitFix] PreNG DFLight forward replacement held holds={} frame={} "
                             "reason=cluster-srv-bind-failed lights={}",
                             holdIndex, frame, consumerResourceState.lightCount);
            }
            return;
        }

        pixelEntry = CommunityShaders::ShaderCache::GetSingleton()->GetPixelShader(*a_shader, pixelState.id);
        if (!pixelEntry)
        {
            return;
        }
        bindConsumer = true;
    }
    else
    {
        // Consumer compile/bind already failed earlier this frame; keep vanilla.
        return;
    }

    const auto vertexEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
    const auto hullEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
    const auto domainEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
    const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
    const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
    if (!vertexEntry || !F4Runtime::IsReadableAddress(bindAddr, 16) ||
        !F4Runtime::IsWritableAddress(pixelGlobal, sizeof(std::uintptr_t)))
    {
        return;
    }
    if (!F4Runtime::WriteValue(pixelGlobal, reinterpret_cast<std::uintptr_t>(pixelEntry)))
    {
        return;
    }

    using PreNGBindShadersFn = void *(*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
    auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
    bindShaders(
        F4Runtime::PreNG::RENDERER_STATE.address(),
        vertexEntry,
        hullEntry,
        domainEntry,
        reinterpret_cast<std::uintptr_t>(pixelEntry));

    if (bindConsumer)
    {
        auto *rendererData = fo4cs::GetRendererData();
        auto *context = rendererData ? reinterpret_cast<ID3D11DeviceContext *>(rendererData->context) : nullptr;
        if (context && dflightForwardCB)
        {
            ID3D11Buffer *cb = dflightForwardCB.get();
            context->PSSetConstantBuffers(3, 1, &cb);
        }

        s_lastDFLightConsumerBoundFrame.store(frame, std::memory_order_relaxed);
    }
    else
    {
        // Zero pass: the consumer already emitted this frame's clustered list.
    }
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightDrawStateStrictLightCB(
    ID3D11DeviceContext *a_context)
{
    // The b3 strict-light buffer is removed; this legacy draw-state entry point
    // is retained for signature compatibility and reports the no-op strict state.
    (void)a_context;
    PreNGDFLightResourceBindingState state{};
    state.lightCount = currentLightCount;
    state.strictLightCount = 0;
    state.shadowBitMask = 0;
    state.strictCBBound = false;
    if (!ShouldBindPreNGDFLightDrawStateStrictLightCB())
    {
        return state;
    }
    return state;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightDrawStateClusterSRVs(
    ID3D11DeviceContext *a_context, bool a_strictCBBound)
{
    (void)a_strictCBBound;
    PreNGDFLightResourceBindingState state{};
    state.lightCount = currentLightCount;
    state.strictLightCount = 0;
    state.shadowBitMask = 0;
    state.strictCBBound = false;

    if (!ShouldBindPreNGDFLightDrawStateClusterSRVs())
    {
        return state;
    }

    auto logBindFailure = [&](const char *a_reason) {
        static std::atomic_uint32_t failureCount = 0;
        const auto failureIndex = ++failureCount;
        if (failureIndex <= 8 || failureIndex % 512 == 0)
        {
            logger::warn("[LightLimitFix] PreNG DFLight draw-state cluster SRV t35-t37 bind held failures={} reason={} "
                         "context=0x{:X} lights={}",
                         failureIndex, a_reason, reinterpret_cast<std::uintptr_t>(a_context), state.lightCount);
        }
    };

    if (!a_context)
    {
        logBindFailure("context-unavailable");
        return state;
    }
    if (currentLightCount == 0)
    {
        logBindFailure("no-lights");
        return state;
    }
    if (!HasResources())
    {
        logBindFailure("gpu-resources-incomplete");
        return state;
    }
    if (!GetCurrentLightsSRV() || !lightIndexListSRV || !lightGridSRV)
    {
        logBindFailure("missing-cluster-srvs");
        return state;
    }

    ID3D11ShaderResourceView *views[3]{GetCurrentLightsSRV(), lightIndexListSRV.get(), lightGridSRV.get()};
    a_context->PSSetShaderResources(35, ARRAYSIZE(views), views);
    state.clusterSRVsBound = true;

    static std::atomic_uint32_t bindCount = 0;
    const auto bindIndex = ++bindCount;
    if (bindIndex <= 8 || bindIndex % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG DFLight draw-state cluster SRVs bound to PS t35-t37 binds={} "
                     "context=0x{:X} lights={} clusters={}",
                     bindIndex, reinterpret_cast<std::uintptr_t>(a_context), state.lightCount,
                     clusterSize[0] * clusterSize[1] * clusterSize[2]);
    }

    static std::atomic_uint32_t nonZeroBindCount = 0;
    const auto nonZeroBindIndex = ++nonZeroBindCount;
    if (nonZeroBindIndex <= 8 || nonZeroBindIndex % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG DFLight draw-state cluster SRVs nonzero bind proof nonzeroBinds={} "
                     "binds={} context=0x{:X} lights={} clusters={}",
                     nonZeroBindIndex, bindIndex, reinterpret_cast<std::uintptr_t>(a_context), state.lightCount,
                     clusterSize[0] * clusterSize[1] * clusterSize[2]);
    }

    return state;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightNoOpPassResources(
    ID3D11DeviceContext *a_context, const char *a_passName)
{
    PreNGDFLightResourceBindingState state{};
    state.lightCount = currentLightCount;
    state.strictLightCount = 0;
    state.shadowBitMask = 0;
    state.strictCBBound = false;
    const char *passName = a_passName ? a_passName : "unknown no-op pass";

    auto logBindFailure = [&](const char *a_reason) {
        static std::atomic_uint32_t failureCount = 0;
        const auto failureIndex = ++failureCount;
        if (failureIndex <= 8 || failureIndex % 512 == 0)
        {
            logger::warn("[LightLimitFix] PreNG DFLight {} LLF resource bind held failures={} reason={} context=0x{:X} "
                         "lights={}",
                         passName, failureIndex, a_reason, reinterpret_cast<std::uintptr_t>(a_context),
                         state.lightCount);
        }
    };

    if (!a_context)
    {
        logBindFailure("context-unavailable");
        return state;
    }
    if (!HasResources())
    {
        logBindFailure("gpu-resources-incomplete");
        return state;
    }
    if (!GetCurrentLightsSRV() || !lightIndexListSRV || !lightGridSRV)
    {
        logBindFailure("missing-cluster-srvs");
        return state;
    }
    if (currentLightCount == 0)
    {
        logBindFailure("cluster-prepass-not-ready");
        return state;
    }

    ID3D11ShaderResourceView *views[3]{GetCurrentLightsSRV(), lightIndexListSRV.get(), lightGridSRV.get()};
    a_context->PSSetShaderResources(35, ARRAYSIZE(views), views);
    state.clusterSRVsBound = true;

    static std::atomic_uint32_t bindCount = 0;
    const auto bindIndex = ++bindCount;
    if (bindIndex <= 8 || bindIndex % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG DFLight {} LLF resources bound binds={} context=0x{:X} lights={} "
                     "clusters={}",
                     passName, bindIndex, reinterpret_cast<std::uintptr_t>(a_context), state.lightCount,
                     clusterSize[0] * clusterSize[1] * clusterSize[2]);
    }

    return state;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightResourceNoOpPass(
    ID3D11DeviceContext *a_context)
{
    return BindPreNGDFLightNoOpPassResources(a_context, "resource no-op pass");
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightFullContractNoOpPass(
    ID3D11DeviceContext *a_context)
{
    return BindPreNGDFLightNoOpPassResources(a_context, "full contract no-op pass");
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGDFLightLLFAdditivePass(
    ID3D11DeviceContext *a_context)
{
    return BindPreNGDFLightNoOpPassResources(a_context, "LLF additive pass");
}

bool LightLimitFix::TracePreNGActiveLightingBindings(const char *a_source, std::int32_t a_shaderType,
                                                     std::uint32_t a_vertexDescriptor, std::uint32_t a_pixelDescriptor,
                                                     bool a_found, std::uintptr_t a_lookupPixelShader,
                                                     ID3D11DeviceContext *a_contextOverride)
{
    static std::atomic_bool descriptorDFLightBindAuditComplete = false;
    static std::atomic_bool descriptorDFLightBindSkipLogged = false;
    static std::atomic_uint32_t descriptorDFLightBindSkippedAuditCount = 0;
    static std::atomic_bool descriptorDFCompositeSafeBindAuditComplete = false;
    static std::atomic_bool descriptorDFCompositeSafeBindSkipLogged = false;
    static std::atomic_uint32_t descriptorDFCompositeSafeBindSkippedAuditCount = 0;

    const std::string_view sourceName = a_source ? std::string_view{a_source} : std::string_view{};
    const bool descriptorDFLightBindAudit = sourceName == "descriptor-dflight-bind";
    const bool descriptorDFCompositeSafeBindAudit = sourceName == "descriptor-dfcomposite-safe-bind";
    if (descriptorDFLightBindAudit && descriptorDFLightBindAuditComplete.load(std::memory_order_relaxed))
    {
        const auto skipped = descriptorDFLightBindSkippedAuditCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!descriptorDFLightBindSkipLogged.exchange(true, std::memory_order_relaxed))
        {
            logger::info("[LightLimitFix] PreNG descriptor DFLight binding audit complete; skipping repeated D3D state "
                         "queries source={} skipped={} shaderType={} vsDesc=0x{:X} psDesc=0x{:X}",
                         a_source, skipped, a_shaderType, a_vertexDescriptor, a_pixelDescriptor);
        }
        return true;
    }
    if (descriptorDFCompositeSafeBindAudit &&
        descriptorDFCompositeSafeBindAuditComplete.load(std::memory_order_relaxed))
    {
        const auto skipped = descriptorDFCompositeSafeBindSkippedAuditCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!descriptorDFCompositeSafeBindSkipLogged.exchange(true, std::memory_order_relaxed))
        {
            logger::info("[LightLimitFix] PreNG descriptor DFComposite safe-bind audit complete; skipping repeated D3D "
                         "state queries source={} skipped={} shaderType={} vsDesc=0x{:X} psDesc=0x{:X}",
                         a_source, skipped, a_shaderType, a_vertexDescriptor, a_pixelDescriptor);
        }
        return true;
    }

    std::optional<CommunityShaders::ShaderCache::ShaderMetadata> currentPixelShaderMetadata;
    std::optional<CommunityShaders::ShaderCache::ShaderMetadata> lookupPixelShaderMetadata;

    auto logAudit = [&](const char *a_reason, ID3D11DeviceContext *a_context, std::uintptr_t a_currentPixelShader,
                        bool a_pixelShaderMatches, bool a_t35Matches, bool a_t36Matches, bool a_t37Matches,
                        ID3D11ShaderResourceView *a_boundT35, ID3D11ShaderResourceView *a_boundT36,
                        ID3D11ShaderResourceView *a_boundT37) {
        static std::atomic_uint32_t setupGeometryQueriedAuditCount = 0;
        static std::atomic_uint32_t shaderLookupQueriedAuditCount = 0;
        static std::atomic_uint32_t dflightDrawStateQueriedAuditCount = 0;
        static std::atomic_uint32_t dflightCandidateBindQueriedAuditCount = 0;
        static std::atomic_uint32_t descriptorDFLightBindQueriedAuditCount = 0;
        static std::atomic_uint32_t descriptorDFCompositeSafeBindQueriedAuditCount = 0;
        static std::atomic_uint32_t pointLightHookQueriedAuditCount = 0;
        static std::atomic_uint32_t otherQueriedAuditCount = 0;
        static std::atomic_uint32_t heldAuditCount = 0;
        const bool queried = std::strcmp(a_reason, "queried") == 0;
        const bool dflightDrawStateAudit = queried && sourceName == "dflight-draw-state";
        std::atomic_uint32_t *auditCounter = &heldAuditCount;
        if (queried)
        {
            if (sourceName == "setup-geometry")
            {
                auditCounter = &setupGeometryQueriedAuditCount;
            }
            else if (sourceName == "shader-lookup")
            {
                auditCounter = &shaderLookupQueriedAuditCount;
            }
            else if (sourceName == "dflight-draw-state")
            {
                auditCounter = &dflightDrawStateQueriedAuditCount;
            }
            else if (sourceName == "dflight-full-shadowed-candidate-bind")
            {
                auditCounter = &dflightCandidateBindQueriedAuditCount;
            }
            else if (sourceName == "descriptor-dflight-bind")
            {
                auditCounter = &descriptorDFLightBindQueriedAuditCount;
            }
            else if (sourceName == "descriptor-dfcomposite-safe-bind")
            {
                auditCounter = &descriptorDFCompositeSafeBindQueriedAuditCount;
            }
            else if (sourceName == "point-light-hook")
            {
                auditCounter = &pointLightHookQueriedAuditCount;
            }
            else
            {
                auditCounter = &otherQueriedAuditCount;
            }
        }
        const auto auditIndex = ++(*auditCounter);
        const bool resourceComplete = a_t35Matches && a_t36Matches && a_t37Matches;
        const bool complete = a_pixelShaderMatches && resourceComplete;
        const auto currentEvidence = GetPreNGShaderSlotEvidence(currentPixelShaderMetadata);
        const auto lookupEvidence = GetPreNGShaderSlotEvidence(lookupPixelShaderMetadata);
        // FO4 forward clusters-only: no b3 strict-light CB, so cluster SRV
        // declaration (t35-t37) is the completion signal, not a CB3 declaration.
        const bool llfConsumerComplete = complete && currentEvidence.hasMetadata &&
                                         currentEvidence.declaresT35 && currentEvidence.declaresT36 &&
                                         currentEvidence.declaresT37 && currentEvidence.samplesT35 > 0 &&
                                         currentEvidence.samplesT36 > 0 && currentEvidence.samplesT37 > 0;
        const bool descriptorCompleteAudit = queried && descriptorDFLightBindAudit && llfConsumerComplete;
        const bool descriptorDFCompositeSafeCompleteAudit =
            queried && descriptorDFCompositeSafeBindAudit && llfConsumerComplete;
        const auto initialAuditLimit =
            dflightDrawStateAudit ? 64u
                                  : ((descriptorDFLightBindAudit || descriptorDFCompositeSafeBindAudit) ? 32u : 16u);
        if (!descriptorCompleteAudit && !descriptorDFCompositeSafeCompleteAudit &&
            auditIndex > initialAuditLimit && auditIndex % 128 != 0)
        {
            return;
        }

        const bool currentFullShadowed = HasPreNGFullShadowedDFLightVanillaContract(currentPixelShaderMetadata);
        const bool lookupFullShadowed = HasPreNGFullShadowedDFLightVanillaContract(lookupPixelShaderMetadata);
        logger::info(
            "[LightLimitFix] PreNG active lighting binding audit audits={} source={} shaderType={} vsDesc=0x{:X} "
            "psDesc=0x{:X} found={} reason={} context=0x{:X} currentPS=0x{:X} lookupPS=0x{:X} psMatch={} t35={} "
            "t36={} t37={} resourceComplete={} complete={} llfConsumerComplete={} currentMeta={} lookupMeta={} "
            "currentFullShadowed={} lookupFullShadowed={} currentDecl=(meta={},cb3={},t35={},t36={},t37={}) "
            "currentSamples=(t35={},t36={},t37={}) lookupDecl=(meta={},cb3={},t35={},t36={},t37={}) "
            "lookupSamples=(t35={},t36={},t37={}) lights={} "
            "bound=(t35=0x{:X},t36=0x{:X},t37=0x{:X}) expected=(t35=0x{:X},t36=0x{:X},t37=0x{:X})",
            auditIndex, a_source ? a_source : "<null>", a_shaderType, a_vertexDescriptor, a_pixelDescriptor, a_found,
            a_reason, reinterpret_cast<std::uintptr_t>(a_context), a_currentPixelShader, a_lookupPixelShader,
            a_pixelShaderMatches, a_t35Matches, a_t36Matches, a_t37Matches, resourceComplete, complete,
            llfConsumerComplete, FormatPreNGShaderMetadata(currentPixelShaderMetadata),
            FormatPreNGShaderMetadata(lookupPixelShaderMetadata), currentFullShadowed, lookupFullShadowed,
            currentEvidence.hasMetadata, currentEvidence.declaresCB3, currentEvidence.declaresT35,
            currentEvidence.declaresT36, currentEvidence.declaresT37, currentEvidence.samplesT35,
            currentEvidence.samplesT36, currentEvidence.samplesT37, lookupEvidence.hasMetadata,
            lookupEvidence.declaresCB3, lookupEvidence.declaresT35, lookupEvidence.declaresT36,
            lookupEvidence.declaresT37, lookupEvidence.samplesT35, lookupEvidence.samplesT36, lookupEvidence.samplesT37,
            currentLightCount,
            reinterpret_cast<std::uintptr_t>(a_boundT35), reinterpret_cast<std::uintptr_t>(a_boundT36),
            reinterpret_cast<std::uintptr_t>(a_boundT37),
            reinterpret_cast<std::uintptr_t>(GetCurrentLightsSRV()),
            reinterpret_cast<std::uintptr_t>(lightIndexListSRV.get()),
            reinterpret_cast<std::uintptr_t>(lightGridSRV.get()));
    };

    if (!HasResources())
    {
        logAudit("resources-incomplete", nullptr, 0, false, false, false, false, nullptr, nullptr, nullptr);
        return false;
    }

    auto *context = a_contextOverride;
    if (!context)
    {
        auto *rendererData = fo4cs::GetRendererData();
        if (!rendererData)
        {
            logAudit("renderer-data-unavailable", nullptr, 0, false, false, false, false, nullptr, nullptr, nullptr);
            return false;
        }
        context = reinterpret_cast<ID3D11DeviceContext *>(rendererData->context);
    }
    if (!context)
    {
        logAudit("context-unavailable", nullptr, 0, false, false, false, false, nullptr, nullptr, nullptr);
        return false;
    }

    winrt::com_ptr<ID3D11PixelShader> currentPixelShader;
    context->PSGetShader(currentPixelShader.put(), nullptr, nullptr);
    winrt::com_ptr<ID3D11ShaderResourceView> boundSRVs[3];
    for (std::size_t i = 0; i < std::size(boundSRVs); ++i) {
        context->PSGetShaderResources(35 + static_cast<UINT>(i), 1, boundSRVs[i].put());
    }

    const auto currentPixelShaderAddress = reinterpret_cast<std::uintptr_t>(currentPixelShader.get());
    if (auto *shaderCache = CommunityShaders::ShaderCache::GetSingleton())
    {
        currentPixelShaderMetadata =
            shaderCache->GetMetadataForD3DShaderObject(CommunityShaders::ShaderStage::Pixel, currentPixelShaderAddress);
        if (a_lookupPixelShader != 0)
        {
            lookupPixelShaderMetadata = a_lookupPixelShader == currentPixelShaderAddress
                                            ? currentPixelShaderMetadata
                                            : shaderCache->GetMetadataForD3DShaderObject(
                                                  CommunityShaders::ShaderStage::Pixel, a_lookupPixelShader);
        }
    }
    const bool pixelShaderMatches = a_lookupPixelShader != 0 && currentPixelShaderAddress == a_lookupPixelShader;
    const bool t35Matches = boundSRVs[0].get() == GetCurrentLightsSRV();
    const bool t36Matches = boundSRVs[1].get() == lightIndexListSRV.get();
    const bool t37Matches = boundSRVs[2].get() == lightGridSRV.get();
    const bool resourceComplete = t35Matches && t36Matches && t37Matches;
    const bool complete = pixelShaderMatches && resourceComplete;
    const auto currentCompletionEvidence = GetPreNGShaderSlotEvidence(currentPixelShaderMetadata);
    // FO4 forward clusters-only: cluster SRV (t35-t37) declaration is the
    // completion signal; there is no b3 strict-light CB to declare.
    const bool llfConsumerComplete =
        complete && currentCompletionEvidence.hasMetadata &&
        currentCompletionEvidence.declaresT35 && currentCompletionEvidence.declaresT36 &&
        currentCompletionEvidence.declaresT37 && currentCompletionEvidence.samplesT35 > 0 &&
        currentCompletionEvidence.samplesT36 > 0 && currentCompletionEvidence.samplesT37 > 0;

    logAudit("queried", context, currentPixelShaderAddress, pixelShaderMatches, t35Matches, t36Matches, t37Matches,
             boundSRVs[0].get(), boundSRVs[1].get(), boundSRVs[2].get());

    if (descriptorDFLightBindAudit && llfConsumerComplete)
    {
        if (!descriptorDFLightBindAuditComplete.exchange(true, std::memory_order_relaxed))
        {
            logger::info("[LightLimitFix] PreNG descriptor DFLight binding audit reached LLF consumer completion; "
                         "future descriptor-dflight-bind audits skip D3D state queries source={} shaderType={} "
                         "vsDesc=0x{:X} psDesc=0x{:X}",
                         a_source, a_shaderType, a_vertexDescriptor, a_pixelDescriptor);
        }
    }
    if (descriptorDFCompositeSafeBindAudit && llfConsumerComplete)
    {
        if (!descriptorDFCompositeSafeBindAuditComplete.exchange(true, std::memory_order_relaxed))
        {
            logger::info("[LightLimitFix] PreNG descriptor DFComposite safe-bind audit reached LLF consumer "
                         "completion; future descriptor-dfcomposite-safe-bind audits skip D3D state queries source={} "
                         "shaderType={} vsDesc=0x{:X} psDesc=0x{:X}",
                         a_source, a_shaderType, a_vertexDescriptor, a_pixelDescriptor);
        }
    }

    return llfConsumerComplete;
}
#endif

void LightLimitFix::CollectLightCB()
{
    auto *rendererData = fo4cs::GetRendererData();
    if (!rendererData)
        return;
    auto *ctx = reinterpret_cast<ID3D11DeviceContext *>(rendererData->context);
    auto *device = reinterpret_cast<ID3D11Device *>(rendererData->device);
    if (!ctx || !device)
        return;

    ID3D11Buffer *lightCB = nullptr;
    ctx->PSGetConstantBuffers(2, 1, &lightCB);
    if (!lightCB)
        return;

    D3D11_BUFFER_DESC desc;
    lightCB->GetDesc(&desc);
    if (desc.ByteWidth < 48)
    {
        lightCB->Release();
        return;
    }

    D3D11_BUFFER_DESC stagingDesc{};
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.ByteWidth = desc.ByteWidth;

    ID3D11Buffer *stagingCB = nullptr;
    if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, &stagingCB)))
    {
        lightCB->Release();
        return;
    }

    ctx->CopyResource(stagingCB, lightCB);
    lightCB->Release();

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ctx->Map(stagingCB, 0, D3D11_MAP_READ, 0, &mapped)))
    {
        stagingCB->Release();
        return;
    }

    const float *rawData = static_cast<const float *>(mapped.pData);
    std::uint32_t lightCount = desc.ByteWidth / 48;
    if (lightCount > 4)
        lightCount = 4;

    for (std::uint32_t i = 0; i < lightCount && frameLights.size() < kMaxLights; i++)
    {
        const float *l = rawData + i * 12;

        if (l[0] == 0.0f && l[1] == 0.0f && l[2] == 0.0f)
            continue;

        auto cbHash = static_cast<std::uint64_t>(l[0] * 1000.0f) ^ (static_cast<std::uint64_t>(l[1] * 1000.0f) << 20) ^
                      (static_cast<std::uint64_t>(l[4] * 255.0f) << 40);

        if (seenCBHashes.contains(cbHash))
            continue;
        seenCBHashes.insert(cbHash);

        LightData data{};
        data.positionWS[0].data.x = l[0];
        data.positionWS[0].data.y = l[1];
        data.positionWS[0].data.z = l[2];
        data.radius = l[3];
        data.color.x = l[4];
        data.color.y = l[5];
        data.color.z = l[6];
        data.fade = l[7];
        data.invRadius = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
        data.lightFlags = static_cast<std::uint32_t>(LightFlags::Initialised);
        frameLights.push_back(data);
    }

    ctx->Unmap(stagingCB, 0);
    stagingCB->Release();
}

void LightLimitFix::CollectLightsFromBSLight()
{
    for (auto *light : seenLights)
    {
        if (!light || frameLights.size() >= kMaxLights)
            break;
        auto *niLight = reinterpret_cast<RE::NiLight *>(light);
        LightData data{};
        data.color.x = niLight->diff.r;
        data.color.y = niLight->diff.g;
        data.color.z = niLight->diff.b;
        data.fade = niLight->dimmer;
        data.radius = niLight->modelBound.fRadius;
        data.invRadius = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
        data.positionWS[0].data.x = niLight->world.translate.x;
        data.positionWS[0].data.y = niLight->world.translate.y;
        data.positionWS[0].data.z = niLight->world.translate.z;
        data.lightFlags = static_cast<std::uint32_t>(LightFlags::Initialised);
        frameLights.push_back(data);
    }
}

void LightLimitFix::CollectLightsFromScene()
{
    auto *dh = RE::TESDataHandler::GetSingleton();
    if (!dh)
        return;

    auto &refs = dh->GetFormArray<RE::TESObjectREFR>();
    for (auto *ref : refs)
    {
        if (!ref || frameLights.size() >= kMaxLights)
            break;

        auto *baseObj = ref->GetObjectReference();
        auto *lightForm = baseObj ? baseObj->As<RE::TESObjectLIGH>() : nullptr;
        if (!lightForm)
            continue;

        auto *niObj = ref->Get3D();
        if (!niObj)
            continue;

        auto *niLight = reinterpret_cast<RE::NiLight *>(niObj);
        if (!niLight)
            continue;

        auto *lightKey = reinterpret_cast<RE::BSLight *>(niLight);
        if (seenLights.contains(lightKey))
            continue;
        seenLights.insert(lightKey);

        LightData data{};
        data.color.x = niLight->diff.r;
        data.color.y = niLight->diff.g;
        data.color.z = niLight->diff.b;
        data.fade = niLight->dimmer;
        data.radius = niLight->modelBound.fRadius;
        data.invRadius = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
        data.positionWS[0].data.x = niLight->world.translate.x;
        data.positionWS[0].data.y = niLight->world.translate.y;
        data.positionWS[0].data.z = niLight->world.translate.z;
        data.lightFlags = static_cast<std::uint32_t>(LightFlags::Initialised);
        frameLights.push_back(data);
    }
}

void LightLimitFix::SetupGeometryBefore(RE::BSRenderPass * /*a_pass*/)
{
    seenThisPass.clear();
}

void LightLimitFix::SetupGeometryAfter(RE::BSRenderPass *a_pass)
{
#if defined(FALLOUT_PRE_NG)
    if (!TryReservePreNGSetupGeometryCall())
    {
        return;
    }
    if (!TryReservePreNGSetupGeometryFrameSample())
    {
        return;
    }

    const auto collected = CollectLightsFromPreNGSceneLights(a_pass);
    if (collected > 0)
    {
        static std::atomic_uint32_t setupGeometryCollectCount = 0;
        const auto collectIndex = ++setupGeometryCollectCount;
        if (collectIndex <= 8 || collectIndex % 512 == 0)
        {
            logger::info("[LightLimitFix] PreNG SetupGeometry scene-light collection accepted samples={} pass=0x{:X} "
                         "collected={}",
                         collectIndex, reinterpret_cast<std::uintptr_t>(a_pass), collected);
        }
    }
    else
    {
        static std::atomic_uint32_t setupGeometryEmptyCount = 0;
        const auto emptyIndex = ++setupGeometryEmptyCount;
        if (emptyIndex <= 8 || emptyIndex % 512 == 0)
        {
            logger::info("[LightLimitFix] PreNG SetupGeometry scene-light collection empty samples={} pass=0x{:X}",
                         emptyIndex, reinterpret_cast<std::uintptr_t>(a_pass));
        }
    }
#else
    CollectLightsFromPass(a_pass);
#endif
}

namespace RE::VTABLE
{
}

#if !defined(FALLOUT_PRE_NG)
namespace
{
	constexpr const char *kPostNGBSLightingDescriptorObserveEnv = "FO4CS_LLF_POSTNG_BSLIGHTING_DESCRIPTOR_OBSERVE";
	constexpr const char *kPostNGBSLightingLLFBindEnv = "FO4CS_LLF_POSTNG_BSLIGHTING_LLF_BIND";
	std::atomic_bool s_postNGBSLightingLLFConsumerDescriptorObserved = false;
	std::atomic_uint32_t s_postNGBSLightingLLFConsumerDescriptorObservations = 0;
}

bool LightLimitFix::HasPostNGBSLightingDescriptorConsumerData() const
{
	// The clustered prepass uploads lights + binds t35-t37 every frame; the
	// consumer is only meaningful once both the GPU resources and a non-empty
	// light list are present.
	return HasResources() && currentLightCount > 0;
}

LightLimitFix::PostNGClusterResourceBindingState LightLimitFix::BindPostNGBSLightingClusterResourcesToPixelShader()
{
	PostNGClusterResourceBindingState state{};
	auto *rendererData = fo4cs::GetRendererData();
	auto *context = rendererData ? reinterpret_cast<ID3D11DeviceContext *>(rendererData->context) : nullptr;
	if (!context)
	{
		return state;
	}

	ID3D11ShaderResourceView *views[3]{GetCurrentLightsSRV(), lightIndexListSRV.get(), lightGridSRV.get()};
	context->PSSetShaderResources(35, ARRAYSIZE(views), views);
	state.clusterSRVsBound = true;
	state.lightCount = currentLightCount;
	return state;
}

void LightLimitFix::NotifyPostNGBSLightingLLFConsumerDescriptorObserved(
	std::uint32_t a_vertexDescriptor,
	std::uint32_t a_pixelDescriptor,
	bool a_found,
	std::uintptr_t a_vanillaPixelShader)
{
	s_postNGBSLightingLLFConsumerDescriptorObserved.store(true, std::memory_order_relaxed);
	const auto observationIndex =
		s_postNGBSLightingLLFConsumerDescriptorObservations.fetch_add(1, std::memory_order_relaxed) + 1;
	if (observationIndex <= 8 || (observationIndex & (observationIndex - 1)) == 0)
	{
		logger::info(
			"[LightLimitFix] PostNG/AE BSLighting LLF consumer descriptor observed observations={} vsDesc=0x{:X} psDesc=0x{:X} found={} vanillaPS=0x{:X}",
			observationIndex, a_vertexDescriptor, a_pixelDescriptor, a_found, a_vanillaPixelShader);
	}
}

bool LightLimitFix::HasPostNGBSLightingLLFConsumerDescriptorObserved() const
{
	return s_postNGBSLightingLLFConsumerDescriptorObserved.load(std::memory_order_relaxed);
}
#endif

#if defined(FALLOUT_PRE_NG)
void LightLimitFix::InstallPreNGBSLightingBatchHook()
{
    static std::atomic_bool installed = false;
    if (installed.exchange(true, std::memory_order_acq_rel))
    {
        return;
    }

    const auto hookAddr = F4Runtime::PreNG::BS_LIGHTING_BATCH_SETUP.address();
    if (!F4Runtime::IsReadableAddress(hookAddr, 16))
    {
        logger::warn("[LightLimitFix] PreNG BSLighting batch-setup hook held; target 0x{:X} is not readable", hookAddr);
        return;
    }

    Hooks::PreNGBSLightingBatchSetup::func = reinterpret_cast<decltype(Hooks::PreNGBSLightingBatchSetup::func)>(
        Detours::X64::DetourFunction(
            hookAddr,
            reinterpret_cast<std::uintptr_t>(Hooks::PreNGBSLightingBatchSetup::thunk)));
    s_preNGBSLightingBatchSetupHookInstalled.store(true, std::memory_order_release);
    logger::info("[LightLimitFix] PreNG BSLighting batch-setup hook installed at 0x{:X} original=0x{:X}",
                 hookAddr, reinterpret_cast<std::uintptr_t>(Hooks::PreNGBSLightingBatchSetup::func));
}
#endif

void LightLimitFix::Hooks::Install(bool a_includeEffectShader)
{
    static std::atomic_bool lightingInstalled = false;
    static std::atomic_bool effectInstalled = false;

    const bool installedLightingNow = !lightingInstalled.exchange(true, std::memory_order_acq_rel);
    bool installedEffectNow = false;
    if (installedLightingNow)
    {
        stl::write_vfunc<0x7, BSLightingShader_SetupGeometry>(RE::VTABLE::BSLightingShader[0]);
#if defined(FALLOUT_PRE_NG)
        stl::write_vfunc<0x7, BSDFLightShader_SetupGeometry>(RE::VTABLE::BSDFLightShader[0]);
#endif
    }
#if defined(FALLOUT_PRE_NG)
    s_preNGBSLightingSetupGeometryHookInstalled.store(true, std::memory_order_release);
#endif
    if (a_includeEffectShader)
    {
        installedEffectNow = !effectInstalled.exchange(true, std::memory_order_acq_rel);
        if (installedEffectNow)
        {
            stl::write_vfunc<0x7, BSEffectShader_SetupGeometry>(RE::VTABLE::BSEffectShader[0]);
        }
    }

    const auto *state = CommunityShaders::State::GetSingleton();
    const char *runtimeName = state ? state->GetRuntimeName().c_str() : "unknown";
    if (installedLightingNow || installedEffectNow)
    {
        logger::info("[LightLimitFix] Installed SetupGeometry hooks (runtime={}, vfunc index 7, lightingInstalled={} "
                     "effectInstalled={} effectRequested={})",
                     runtimeName, installedLightingNow, installedEffectNow, a_includeEffectShader);
    }
    else
    {
        logger::info("[LightLimitFix] SetupGeometry hooks already installed; skipping duplicate install (runtime={}, "
                     "effectRequested={})",
                     runtimeName, a_includeEffectShader);
    }
}

void LightLimitFix::Hooks::BSLightingShader_SetupGeometry::thunk(RE::BSShader *a_this, RE::BSRenderPass *a_pass)
{
    auto &self = globals::features::lightLimitFix;
#if defined(FALLOUT_PRE_NG)
    const auto totalSetupGeometryCalls =
        s_preNGBSLightingSetupGeometryHookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!self.ShouldProcessPreNGBSLightingSetupGeometryProof())
    {
        const auto previewReason = GetCachedPreNGBSLightingSetupGeometryPreviewReason();
        const bool preserveSceneLightCollection = previewReason == 0;
        const auto bypassCallIndex =
            s_preNGBSLightingSetupGeometryBypassCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (bypassCallIndex <= 8 || (bypassCallIndex & (bypassCallIndex - 1)) == 0)
        {
            logger::info("[LightLimitFix] PreNG BSLighting SetupGeometry proof bypass calling vanilla calls={} "
                         "totalCalls={} reason={} collectSceneLights={} shader=0x{:X} pass=0x{:X}",
                         bypassCallIndex, totalSetupGeometryCalls,
                         GetPreNGBSLightingSetupGeometryPreviewReasonName(previewReason), preserveSceneLightCollection,
                         reinterpret_cast<std::uintptr_t>(a_this), reinterpret_cast<std::uintptr_t>(a_pass));
        }
        if (preserveSceneLightCollection)
        {
            self.SetupGeometryBefore(a_pass);
            func(a_this, a_pass);
            self.SetupGeometryAfter(a_pass);
        }
        else
        {
            func(a_this, a_pass);
        }
        return;
    }

    static std::atomic_uint32_t preNGBSLightingSetupGeometryCalls = 0;
    const auto callIndex = ++preNGBSLightingSetupGeometryCalls;
    if (callIndex <= 8 || callIndex % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG BSLightingShader SetupGeometry hook reached calls={} totalCalls={} "
                     "shader=0x{:X} pass=0x{:X}",
                     callIndex, totalSetupGeometryCalls, reinterpret_cast<std::uintptr_t>(a_this),
                     reinterpret_cast<std::uintptr_t>(a_pass));
    }
#endif
    self.SetupGeometryBefore(a_pass);
    func(a_this, a_pass);
    self.SetupGeometryAfter(a_pass);
#if defined(FALLOUT_PRE_NG)
    self.BindPreNGBSLightingSetupGeometryResources(a_pass);
    self.TryBindPreNGBSLightingVisibleConsumerFromSetupGeometry(a_this);
#endif
}

#if defined(FALLOUT_PRE_NG)
void LightLimitFix::Hooks::BSDFLightShader_SetupGeometry::thunk(RE::BSShader *a_this, RE::BSRenderPass *a_pass)
{
    func(a_this, a_pass);
    auto &self = globals::features::lightLimitFix;
    // DFLight per-pass setup runs immediately before the draw; swapping the
    // pixel shader here sticks, unlike the earlier batch-setup hook where the
    // renderer re-bound the vanilla shader before the draw.
    self.HandlePreNGDFLightForwardBatchPostCall(a_this);
}
#endif

#if defined(FALLOUT_PRE_NG)
std::uint8_t LightLimitFix::Hooks::PreNGBSLightingBatchSetup::thunk(
    RE::BSShader *a_shader,
    void *a_batchData,
    std::uint32_t a_flags,
    std::uint8_t a_flag)
{
    auto &self = globals::features::lightLimitFix;
    const auto result = func(a_shader, a_batchData, a_flags, a_flag);

#if defined(FALLOUT_PRE_NG)
    s_preNGBSLightingBatchSetupHookCallCount.fetch_add(1, std::memory_order_relaxed);
    CapturePreNGDFLightCameraCBOnce();
#endif

    // Vanilla batch setup just finished the shader lookup + CB1/CB2 bind, so the
    // current VS/PS entries describe this item; reuse the SetupGeometry consumer
    // bind (swap PS to the ShaderCache consumer + re-assert t35-t37).
    self.TryBindPreNGBSLightingVisibleConsumerFromSetupGeometry(a_shader, "BatchSetup");
    return result;
}
#endif

void LightLimitFix::Hooks::BSEffectShader_SetupGeometry::thunk(RE::BSShader *a_this, RE::BSRenderPass *a_pass)
{
    func(a_this, a_pass);
    auto &self = globals::features::lightLimitFix;
    self.SetupGeometryBefore(a_pass);
    self.SetupGeometryAfter(a_pass);
}
