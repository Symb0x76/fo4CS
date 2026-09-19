// LightLimitFix -- cluster and descriptor resource binds.
//
// The t35-t37 SRV binds, the shared descriptor-resource bind body and its three
// wrappers, the payload-readiness predicates, the descriptor observation
// latches and the DFLight draw-state / no-op pass binds.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #102-104,
// #107-112, #117-121, #132-137).
//
// The Notify*DescriptorObserved functions write the s_preNG* observation
// latches that the config gates read. That write/read pair crossing a TU
// boundary is the single reason LLFInternal.h declares them extern instead of
// letting each cluster own a copy.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"
#include "Core/ShaderCache.h"

#include <atomic>
#include <cstdint>

using namespace CommunityShaders::lightlimit;

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
#endif
