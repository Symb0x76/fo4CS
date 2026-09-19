// LightLimitFix -- the renderer-state base and the deferred descriptor bind.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #1, #77).
//
// This is the cluster the ad7cab9 performance fix lives in, and the comment on
// GetPreNGDFLightRendererStateBase is the load-bearing part: both addresses are
// fixed in the game image, so readability is settled at load time. Probing them
// per call meant a VirtualQuery per call, which serialises on the process
// address-space lock that the D3D12 proxy and the frame-generation threads
// contend for -- 5.33 ms/frame mean, 2.16 ms floor. The per-binary + per-base
// caching is not an optimisation to tidy up; removing it costs 30 fps.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include <RE/FO4Runtime.h>
#include <Windows.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>

#if defined(FALLOUT_PRE_NG)
namespace CommunityShaders::lightlimit
{
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
} // namespace CommunityShaders::lightlimit
#endif
