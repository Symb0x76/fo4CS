// LightLimitFix -- hook installation and the vfunc thunks.
//
// PostPostLoad (which decides what installs per runtime), the PostNG BSLighting
// consumer, the batch-setup detour, Hooks::Install and all four SetupGeometry
// thunks.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #93, #144-153).
//
// write_vfunc<0x7> on BSLightingShader / BSDFLightShader / BSEffectShader is
// the verified entry point for the DFLight forward consumer -- see
// docs/llf-dflight-forward-consumer.md. The vtable index, the REL::IDs and the
// Detours call are transcribed verbatim.
//
// Hooks::Install and InstallPreNGBSLightingBatchHook are one-shot through their
// own atomics. Keeping them in one TU keeps them one latch each; they must not
// be reachable from a second static initialiser.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"
#include "Core/ShaderCache.h"
#include "Core/State.h"

#if defined(FALLOUT_POST_AE)
#include "RE/B/BSGraphics.h"
#else
#include "RE/Bethesda/BSGraphics.h"
#endif

#include <atomic>
#include <cstdint>

using namespace CommunityShaders::lightlimit;

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
