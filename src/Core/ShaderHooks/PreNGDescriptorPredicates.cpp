#include "Core/ShaderHooks/PreNGDescriptorPredicates.h"

#include "Core/PreNGEnvironment.h"
#include "Core/ShaderHooks/PreNGRuntime.h"
#include "Core/ShaderHooks/PreNGSwitches.h"

#if defined(FALLOUT_POST_AE)
#include "RE/B/BSShader.h"
#else
#include "RE/Bethesda/BSShader.h"
#endif

namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	bool IsPreNGLightingDescriptorShader(std::int32_t a_shaderType, std::string_view a_fxpFilename)
	{
		return a_shaderType == kPreNGBSLightingShaderType ||
		       (a_shaderType == kPreNGDFLightingShaderType && IsPreNGDFLightFxpName(a_fxpFilename));
	}

	bool IsPreNGLightingDescriptorShader(const RE::BSShader* a_shader)
	{
		if (!a_shader) {
			return false;
		}

		const auto shaderType = static_cast<std::int32_t>(a_shader->shaderType);
		if (shaderType == kPreNGBSLightingShaderType) {
			return true;
		}
		if (shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename);
	}

	bool IsPreNGDFCompositeDescriptorShader(const RE::BSShader* a_shader)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFCompositeShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFCompositeFxpName(fxpFilename);
	}

	bool IsPreNGDFCompositeContractDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		return IsPreNGDFCompositeDescriptorShader(a_shader) &&
		       F4Runtime::PreNG::IsDFCompositeObservedPixelDescriptor(a_pixelDescriptor);
	}

	bool IsPreNGDFCompositeSafeBindDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		return IsPreNGDFCompositeDescriptorShader(a_shader) &&
		       (a_pixelDescriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88 ||
		        a_pixelDescriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088 ||
		        (ShouldBindPreNGDFCompositeFogSafeDescriptorShader() &&
		         (a_pixelDescriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_40 ||
		          a_pixelDescriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10040)));
	}


	bool IsPreNGDFLightFullShadowedPixelDescriptor(std::uint32_t a_descriptor)
	{
		return F4Runtime::PreNG::IsDFLightFullShadowedPixelDescriptor(a_descriptor);
	}

	bool IsPreNGDFLightFullShadowedDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename) &&
		       IsPreNGDFLightFullShadowedPixelDescriptor(a_pixelDescriptor);
	}

	bool IsPreNGDFLightFullContractDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename) &&
		       F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(a_pixelDescriptor);
	}

	bool IsPreNGDFLightLLFConsumerDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename) &&
		       (F4Runtime::PreNG::IsDFLightLLFConsumerPixelDescriptor(a_pixelDescriptor) ||
		        (ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer() &&
		         F4Runtime::PreNG::IsDFLightFullShadowedPixelDescriptor(a_pixelDescriptor)));
	}

	bool IsPreNGDFLightFullShadowedDescriptorConsumerShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		return ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer() &&
		       IsPreNGDFLightFullShadowedDescriptorShader(a_shader, a_pixelDescriptor);
	}

	bool CanActivelyReplacePreNGLightingDescriptorShader(const RE::BSShader* a_shader, std::uint32_t a_pixelDescriptor)
	{
		if (!a_shader) {
			return false;
		}
		if (a_shader->shaderType == kPreNGBSLightingShaderType) {
			return true;
		}
		return IsPreNGDFLightLLFConsumerDescriptorShader(a_shader, a_pixelDescriptor);
	}

	bool IsPreNGDFLightFullShadowedCandidateLookup(RE::BSShader* a_shader, std::int32_t a_pixelDescriptor)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename) &&
		       IsPreNGDFLightFullShadowedPixelDescriptor(static_cast<std::uint32_t>(a_pixelDescriptor));
	}

	bool IsPreNGBSLightingVanillaDumpLookup(RE::BSShader* a_shader)
	{
		if (!a_shader || a_shader->shaderType != kPreNGBSLightingShaderType) {
			return false;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGBSLightingFxpName(fxpFilename);
	}

	bool IsPreNGBSLightingContractPixelDescriptor(std::uint32_t a_descriptor)
	{
		return F4Runtime::PreNG::IsBSLightingContractPixelDescriptor(a_descriptor);
	}

	bool IsPreNGBSLightingContractDescriptorShader(RE::BSShader* a_shader, std::int32_t a_pixelDescriptor)
	{
		return IsPreNGBSLightingVanillaDumpLookup(a_shader) &&
		       IsPreNGBSLightingContractPixelDescriptor(static_cast<std::uint32_t>(a_pixelDescriptor));
	}

	bool IsPreNGDFLightVanillaDumpLookup(RE::BSShader* a_shader, std::int32_t a_pixelDescriptor)
	{
		if (!a_shader || a_shader->shaderType != kPreNGDFLightingShaderType) {
			return false;
		}

		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		return IsPreNGDFLightFxpName(fxpFilename) &&
		       (F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(pixelDescriptor) ||
		        IsPreNGDFLightFullShadowedPixelDescriptor(pixelDescriptor) ||
		        F4Runtime::PreNG::IsDFLightForwardPixelDescriptor(pixelDescriptor));
	}

	bool IsPreNGDFCompositeVanillaDumpLookup(RE::BSShader* a_shader)
	{
		return IsPreNGDFCompositeDescriptorShader(a_shader);
	}

	std::string_view GetPreNGDFLightVanillaDumpFamily(std::uint32_t a_pixelDescriptor)
	{
		if (F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(a_pixelDescriptor)) {
			return "DFLightFullContract";
		}
		if (IsPreNGDFLightFullShadowedPixelDescriptor(a_pixelDescriptor)) {
			return "DFLightFullShadowed";
		}
		if (F4Runtime::PreNG::IsDFLightForwardPixelDescriptor(a_pixelDescriptor)) {
			return "DFLightForward";
		}
		return "DFLightUnknown";
	}
#endif
}
