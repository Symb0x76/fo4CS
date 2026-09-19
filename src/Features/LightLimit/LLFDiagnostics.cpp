// LightLimitFix -- one-shot diagnostics.
//
// The Debug.ini environment snapshot, the every-600-frame hook-reachability
// watchdog, and the compile-only pixel-shader probes.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #45, #72, #78-80).
//
// The watchdog is the thing that reports pointCalls / setupCalls /
// setupBypassCalls / batchCalls, i.e. whether the verified LLF consumer path
// was reached at all this run. It reads the s_preNG* hook counters that
// LLFInternal.h keeps single-instance; a second copy of any of them would make
// this log lie.
//
// #79 and #80 each own their own one-shot attempted/loggedHeld pair and are
// deliberately two functions rather than one parameterised one.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include "Core/CommunityShaders.h"
#include "Core/ShaderCache.h"
#include "Core/ShaderCompiler.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

#if defined(FALLOUT_PRE_NG)
namespace CommunityShaders::lightlimit
{
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
                 "setupBypassCalls={} batchHookInstalled={} batchCalls={}; zero-call hooks mean this run has not "
                 "exercised the verified BSLighting/point-light/batch route yet, so visible LLF remains held",
                 a_frame, pointRequested, s_preNGPointLightHookInstalled.load(std::memory_order_acquire),
                 s_preNGPointLightHookPatchVerified.load(std::memory_order_acquire),
                 s_preNGPointLightHookCallCount.load(std::memory_order_relaxed), setupResourceRequested,
                 s_preNGBSLightingSetupGeometryHookInstalled.load(std::memory_order_acquire),
                 s_preNGBSLightingSetupGeometryHookCallCount.load(std::memory_order_relaxed),
                 s_preNGBSLightingSetupGeometryBypassCallCount.load(std::memory_order_relaxed),
                 batchHookInstalled, s_preNGBSLightingBatchSetupHookCallCount.load(std::memory_order_relaxed));
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
} // namespace CommunityShaders::lightlimit
#endif
