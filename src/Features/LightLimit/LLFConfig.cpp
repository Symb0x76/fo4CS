// LightLimitFix -- debug-switch plumbing, budgets and every feature gate.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #28-38, #44,
// #46-71, #73-76).
//
// THIS CLUSTER IS ONE TRANSLATION UNIT ON PURPOSE. Nearly every gate below owns
// a function-local `static const` that resolves its Debug.ini switch once, logs
// the resolution once, and is read from the draw path forever after. One TU
// means one latch and one log line each. Moving a gate into a header, marking
// one inline, or defining the same gate in a second cluster splits the latch and
// changes both the logging and -- for the clamped interval gates -- what the
// draw path reads. LLFInternal.h says the same thing from the other side.
//
// ShouldTimePreNGClusterPrepassGpu is the deliberate exception: it is NOT
// latched, see the comment at its definition.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include "Core/CommunityShaders.h"
#include "Core/DebugSwitches.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>

#if defined(FALLOUT_PRE_NG)
namespace CommunityShaders::lightlimit
{
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
bool IsTruthyEnvironmentSwitch(const char *a_name)
{
    return ReadEnvironmentSwitch(a_name).enabled;
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
} // namespace CommunityShaders::lightlimit
#endif
