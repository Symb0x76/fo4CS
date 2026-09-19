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
#endif
