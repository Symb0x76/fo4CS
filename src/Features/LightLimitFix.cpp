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

// Nested profiling zones inside RunClusterPrepass. The Feature.h zone wraps the
// whole phase, which measured 7.74ms CPU against 0.28ms GPU per frame on PreNG
// (2026-09-14 capture) -- enough to say the cost is CPU-side, not enough to say
// which part. These split it. Compiled out entirely without TRACY_SUPPORT=ON, and
// the include is guarded because the tracy dependency only exists behind the
// vcpkg "tracy" manifest feature.
// ZoneScopedN declares a fixed-name variable, so two of them in one scope fail to
// compile. ZoneNamedN takes the variable name, which lets several coexist -- needed
// here because some of these markers share a scope with a nested zone.
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

using namespace CommunityShaders::lightlimit;

// This was an anonymous namespace. It becomes a named one so the clusters under
// src/Features/LightLimit/ can be carved out of it one at a time: a function
// still living here can be declared in LLFInternal.h and called from a cluster
// that has already moved, and vice versa, without either side having to move
// before it is ready.
//
// The linkage change is deliberate and matches what the ShaderCache split did.
// It does not touch the thing that actually matters here -- a function-local
// `static` is one object per function either way, so no gate latch and no
// one-shot log changes count. What WOULD change it is making a gate `inline` or
// giving it a body in the header; LLFInternal.h says why, and that rule holds.
//
// Release builds with /GL and links with /LTCG (cmake/Fo4csTargets.cmake), so
// the inliner still sees across the new translation-unit boundaries and the
// per-draw gates cost what they cost today.
namespace CommunityShaders::lightlimit
{
#if defined(FALLOUT_PRE_NG)

// The renderer-state base vanilla DFLight reads: TLS[TlsIndex] + 2848
// (falling back to qword_1461DDC68 when the TLS slot is null).
//
// Both constants are fixed addresses in the game image, so whether they are
// readable is settled at load time and cannot change afterwards. Re-probing
// them every call is not free: IsReadableAddress calls VirtualQuery, which
// serializes on the process address-space lock that the D3D12 proxy and the
// frame-generation threads contend for constantly. Probe each once, then read
// raw.
std::uintptr_t GetPreNGDFLightRendererStateBase()
{
    const RE::FO4Runtime::RuntimeAddressValue kPreNGTlsIndex{ 0x1467347B4 };
    const RE::FO4Runtime::RuntimeAddressValue kPreNGRendererFallback{ 0x1461DDC68 };

    // An unreadable index yields a sentinel that fails the tlsIndex < 0x400 gate
    // below, so it falls through to the fallback pointer exactly as the old
    // !tlsIndexRead branch did.
    static constexpr std::uint32_t kUnreadableTlsIndex = 0xFFFFFFFFu;
    static const std::uint32_t tlsIndex =
        RE::FO4Runtime::ReadValueOr<std::uint32_t>(kPreNGTlsIndex.address(), kUnreadableTlsIndex);
    static const bool fallbackReadable =
        RE::FO4Runtime::IsReadableAddress(kPreNGRendererFallback.address(), sizeof(std::uintptr_t));

    const auto teb = __readgsqword(0x30);
    const auto tlsArray = *reinterpret_cast<std::uintptr_t *>(teb + 0x58);
    const auto slot = (tlsArray && tlsIndex < 0x400) ?
        *reinterpret_cast<std::uintptr_t *>(tlsArray + static_cast<std::uintptr_t>(tlsIndex) * 8) : 0;
    const auto base = slot ? *reinterpret_cast<std::uintptr_t *>(slot + 2848) : 0;
    if (base)
    {
        return base;
    }
    return fallbackReadable ?
        *reinterpret_cast<const std::uintptr_t *>(kPreNGRendererFallback.address()) : 0;
}

// Whether the view rows and camera position hanging off the renderer state are
// readable depends only on the base pointer, so the guard only has to run when
// that pointer changes. It exists to catch a wrong offset on an unexpected
// binary, which is a load-time property, not a per-frame one.
//
// This is the expensive one: with the two probes here plus the TLS read above
// running every frame, Tracy measured LLF/LightPrep at 5.33 ms mean self time
// (min 2.16 ms over 2193 frames) in a light-heavy exterior -- 16x the next
// largest zone in the capture, for a scope that otherwise does 80 bytes of
// memcpy. thread_local because the base is derived from TLS; plain statics
// would be a data race if the prepass ever ran off the render thread.
bool IsPreNGDFLightRendererStateReadable(std::uintptr_t a_rendererBase)
{
    if (a_rendererBase == 0)
    {
        return false;
    }

    thread_local std::uintptr_t probedBase = 0;
    thread_local bool probedReadable = false;
    if (a_rendererBase != probedBase)
    {
        probedReadable =
            RE::FO4Runtime::IsReadableAddress(a_rendererBase + 7024 + 114 * 16, 4 * 16) &&
            RE::FO4Runtime::IsReadableAddress(a_rendererBase + 8736, sizeof(float) * 3);
        probedBase = a_rendererBase;
    }
    return probedReadable;
}
#endif

#if defined(FALLOUT_PRE_NG)
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

#endif
} // namespace CommunityShaders::lightlimit

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
    {
        FO4CS_LLF_ZONE("LLF/CollectLights");
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
        // Covers the compute-submission block. Its SELF time excludes the nested
        // Transform/Upload/ClusterBuild/ClusterCull zones, so whatever it reports is
        // cost in this block that none of those four account for -- the renderer-state
        // probes, the GPU timer, and the payload cache work.
        FO4CS_LLF_ZONE("LLF/ComputeBlock");

#if defined(FALLOUT_PRE_NG)
        auto *timingDevice = reinterpret_cast<ID3D11Device *>(rendererData->device);
        const auto gpuTimerSlot = BeginPreNGClusterGpuTimer(context, timingDevice);
#endif

        if (currentLightCount > 0)
        {
            // Self time here is the first renderer-state probe and the snapshot
            // bookkeeping; TransformLights and UploadLights are nested children.
            FO4CS_LLF_ZONE("LLF/LightPrep");
            DirectX::XMFLOAT4X4 viewRows{};
            bool viewRowsValid = false;
            DirectX::XMFLOAT3 camPos{};
            bool camPosValid = false;
#if defined(FALLOUT_PRE_NG)
            // Replicate the EXACT transform vanilla sub_1428C37A0 uses for
            // cb2[1]: read the renderer-base camera position (+8736) and the
            // view rows (base + 7024 + 114..117 * 16), then
            // viewPos = (lightWorld - camWorld) * viewRows (row-vector, with
            // perspective divide). This is the only source guaranteed to match
            // the space of vanilla cb2[1].
            //
            // PreNG only: those are byte offsets into the 1.10.163 renderer state,
            // recovered from that build's disassembly. There is no reason for them
            // to hold on 1.10.984 or the Anniversary build, so the other runtimes
            // fall through to the CommonLibF4 camera state below -- the same path
            // PreNG itself takes whenever the raw read is not readable.
            const auto rendererBase = GetPreNGDFLightRendererStateBase();
            if (IsPreNGDFLightRendererStateReadable(rendererBase))
            {
                std::memcpy(&viewRows, reinterpret_cast<const void *>(rendererBase + 7024 + 114 * 16), sizeof(viewRows));
                std::memcpy(&camPos, reinterpret_cast<const void *>(rendererBase + 8736), sizeof(float) * 3);
                viewRowsValid = true;
                camPosValid = true;
            }
#endif
            if (!viewRowsValid || !camPosValid)
            {
                // Fallback: previous camViewData-based rotation, so lights keep
                // a consistent (if not vanilla-exact) space rather than garbage.
                const auto &gfxState = RE::BSGraphics::State::GetSingleton();
                viewRows = *reinterpret_cast<const DirectX::XMFLOAT4X4 *>(gfxState.cameraState.camViewData.viewMat);
                const auto &p = gfxState.cameraState.posAdjust;
                camPos = DirectX::XMFLOAT3{ p.x, p.y, p.z };
            }

#if defined(FALLOUT_PRE_NG)
            preNGDFLightLastSnapshotCameraPos = DirectX::XMFLOAT4{ camPos.x, camPos.y, camPos.z, 0.0f };
            preNGDFLightLastSnapshotViewRows = viewRows;
            preNGDFLightLastSnapshotViewValid = viewRowsValid && camPosValid;
#endif

            DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&viewRows);
            DirectX::XMVECTOR camPosV = DirectX::XMLoadFloat3(&camPos);
            {
                FO4CS_LLF_ZONE("LLF/TransformLights");
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
            }

            FO4CS_LLF_ZONE("LLF/UploadLights");
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

            FO4CS_LLF_ZONE("LLF/ClusterBuild");
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
            // Self time here is the culling CB map/fill/unmap, the second renderer-state
            // probe, and the SRV/UAV binds; ClusterCull is a nested child. The constant
            // buffer stays mapped across all of that.
            FO4CS_LLF_ZONE("LLF/CullSetup");
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
#if defined(FALLOUT_PRE_NG)
                DirectX::XMFLOAT4X4 viewRows = preNGDFLightLastSnapshotViewRows;
                DirectX::XMFLOAT3 camPos{
                    preNGDFLightLastSnapshotCameraPos.x,
                    preNGDFLightLastSnapshotCameraPos.y,
                    preNGDFLightLastSnapshotCameraPos.z };
                bool valid = preNGDFLightLastSnapshotViewValid;
#else
                // No PreNG renderer-state snapshot exists on these runtimes, so the
                // CommonLibF4 camera state below is the only source. It is still the
                // same camera the positionWS[1] fill used, because that fill took the
                // identical fallback.
                DirectX::XMFLOAT4X4 viewRows{};
                DirectX::XMFLOAT3 camPos{};
                bool valid = false;
#endif
                if (!valid)
                {
#if defined(FALLOUT_PRE_NG)
                    const auto rendererBase = GetPreNGDFLightRendererStateBase();
                    valid = IsPreNGDFLightRendererStateReadable(rendererBase);
                    if (valid)
                    {
                        std::memcpy(&viewRows, reinterpret_cast<const void *>(rendererBase + 7024 + 114 * 16), sizeof(viewRows));
                        std::memcpy(&camPos, reinterpret_cast<const void *>(rendererBase + 8736), sizeof(camPos));
                    }
#endif
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

            FO4CS_LLF_ZONE("LLF/ClusterCull");
            context->CSSetShader(clusterCullingCS.get(), nullptr, 0);
            ID3D11Buffer *cullCBPtr = lightCullingCB.get();
            context->CSSetConstantBuffers(0, 1, &cullCBPtr);

            context->Dispatch((clusterSize[0] + NUMTHREAD_X - 1) / NUMTHREAD_X,
                              (clusterSize[1] + NUMTHREAD_Y - 1) / NUMTHREAD_Y,
                              (clusterSize[2] + NUMTHREAD_Z - 1) / NUMTHREAD_Z);
        }

        FO4CS_LLF_ZONE("LLF/Finalize");
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
    // Runs to the end of the function: the Prepass SRV bind and
    // TryBindPreNGBSLightingDeferredDescriptorResources, neither of which has been
    // measured yet.
    FO4CS_LLF_ZONE("LLF/Tail");
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

// PreNG only. Every member defined between here and the #endif below is
// declared inside a FALLOUT_PRE_NG block in LightLimitFix.h, so on the other
// runtimes these definitions have no matching declaration and the raw
// renderer-state offsets they read do not apply. The guard was missing, which
// is why PostNG and PostAE never compiled on this branch.
#if defined(FALLOUT_PRE_NG)
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
#endif

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

#if defined(FALLOUT_PRE_NG)
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
#endif

#if defined(FALLOUT_PRE_NG)
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
    if (!F4Runtime::PreNG::IsBSLightingContractPixelDescriptor(pixelDescriptor))
    {
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
    if (bindIndex <= 8 || (bindIndex & (bindIndex - 1)) == 0)
    {
        logger::info("[LightLimitFix] PreNG BSLighting LLF consumer bound via {} binds={} shaderType={} "
                     "descriptor=0x{:X} llfConsumerComplete=true lights={}",
                     a_sourceName, bindIndex, static_cast<std::int32_t>(a_shader->shaderType), pixelDescriptor,
                     currentLightCount);
    }
}
#endif

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
