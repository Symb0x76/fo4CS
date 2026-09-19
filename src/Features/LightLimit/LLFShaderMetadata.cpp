// LightLimitFix -- pixel-shader metadata formatting.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #15-24). These
// turn ShaderCache metadata into the cb3/t35-t37 evidence the LLF consumer
// logs are built from. No statics, no game-memory writes.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include "Core/ShaderCache.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string>

#if defined(FALLOUT_PRE_NG)
namespace CommunityShaders::lightlimit
{
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
} // namespace CommunityShaders::lightlimit

using namespace CommunityShaders::lightlimit;

// #138: the live binding audit. It queries the real pipeline state -- current
// PS entry, cb2/cb3/cb12 byte sizes, t35-t37 SRVs -- and reports whether the LLF
// contract is actually satisfied on this draw. It owns fifteen throttled-log
// counters including nine inside its logAudit lambda, all independent and all
// moved verbatim; merging any two of them would change what the log reports.
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
