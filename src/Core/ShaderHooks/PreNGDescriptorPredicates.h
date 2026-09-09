#pragma once

#include "Core/ShaderHooks/PreNGShaderHookConstants.h"

#include <cstdint>
#include <string_view>

namespace RE
{
	class BSShader;
}

// Classification of a shader-lookup request: which engine shader family it
// belongs to, which descriptor variant, and whether Community Shaders is
// permitted to substitute for it.
//
// Pure predicates over (shader, descriptor) plus the Debug.ini gates in
// PreNGSwitches. Nothing here mutates engine state or logs, which is what makes
// them safe to call from the middle of the lookup hook's hot path.
namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	bool IsPreNGLightingDescriptorShader(std::int32_t a_shaderType, std::string_view a_fxpFilename);
	bool IsPreNGLightingDescriptorShader(const RE::BSShader* a_shader);
	bool IsPreNGDFCompositeDescriptorShader(const RE::BSShader* a_shader);
	bool IsPreNGDFCompositeContractDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor);
	bool IsPreNGDFCompositeSafeBindDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor);
	bool IsPreNGDFLightFullShadowedPixelDescriptor(std::uint32_t a_descriptor);
	bool IsPreNGDFLightFullShadowedDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor);
	bool IsPreNGDFLightFullContractDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor);
	bool IsPreNGDFLightLLFConsumerDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor);
	bool IsPreNGDFLightFullShadowedDescriptorConsumerShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor);
	bool CanActivelyReplacePreNGLightingDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor);
	bool IsPreNGDFLightFullShadowedCandidateLookup(RE::BSShader* a_shader, std::int32_t a_pixelDescriptor);
	bool IsPreNGBSLightingVanillaDumpLookup(RE::BSShader* a_shader);
	bool IsPreNGBSLightingContractPixelDescriptor(std::uint32_t a_descriptor);
	bool IsPreNGBSLightingContractDescriptorShader(RE::BSShader* a_shader, std::int32_t a_pixelDescriptor);
	bool IsPreNGDFLightVanillaDumpLookup(RE::BSShader* a_shader, std::int32_t a_pixelDescriptor);
	bool IsPreNGDFCompositeVanillaDumpLookup(RE::BSShader* a_shader);
	std::string_view GetPreNGDFLightVanillaDumpFamily(std::uint32_t a_pixelDescriptor);
#endif
}
