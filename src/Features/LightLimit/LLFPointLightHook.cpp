// LightLimitFix -- the PreNG internal point-light call detour.
//
// Call-site validation, the thunk, the E8 patch verification and the guarded
// install. Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #27, #81-85).
//
// This cluster writes game memory. The REL::ID, the vfunc/call-site signature
// check and the write_thunk_call are transcribed verbatim; the install is
// one-shot through s_preNGPointLightHookInstalled, which lives in
// LLFInternal.h precisely so the diagnostics watchdog reads the same object
// this cluster writes.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include "Core/Globals.h"

#include <atomic>
#include <cstdint>

#if defined(FALLOUT_PRE_NG)
namespace CommunityShaders::lightlimit
{
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
} // namespace CommunityShaders::lightlimit
#endif
