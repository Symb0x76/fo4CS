#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <RE/FO4Runtime.h>

// Constants and the RE::FO4Runtime alias shared by every PreNG shader-hook
// translation unit.
//
// PreNG shader-path runtime API lives in RE::FO4Runtime::PreNG.
//
// Kept as `static constexpr` exactly as they were in BSShaderHooks.cpp: each
// including translation unit gets its own copy with identical values, which is
// what the file already had. Nothing takes their addresses or compares the
// const char* pointers -- they are only ever passed to the Debug.ini readers --
// so per-TU copies are indistinguishable from the single definition they were.
namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	namespace F4Runtime = RE::FO4Runtime;
	static constexpr std::int32_t kPreNGBSLightingShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::BS_LIGHTING_SHADER_TYPE);
	static constexpr std::int32_t kPreNGDFLightingShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::DF_LIGHTING_SHADER_TYPE);
	static constexpr std::int32_t kPreNGDFCompositeShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::DF_COMPOSITE_SHADER_TYPE);
	static constexpr std::size_t kPreNGMaxShaderLookupDiagnostics = 48;
	static constexpr std::uint32_t kPreNGMaxShaderLookupHeavyDiagnostics = 16;
	static constexpr std::size_t kPreNGMaxDescriptorMutationDiagnostics = 64;
	static constexpr std::size_t kPreNGMaxDescriptorBindDiagnostics = 64;
	static constexpr const char* kPreNGDescriptorMutateEnv = "FO4CS_LLF_PRENG_DESCRIPTOR_MUTATE";
	static constexpr const char* kPreNGDescriptorCompileEnv = "FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE";
	static constexpr const char* kPreNGDescriptorBindEnv = "FO4CS_LLF_PRENG_DESCRIPTOR_BIND";
	static constexpr const char* kPreNGDFLightFullContractDescriptorCompileEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_DESCRIPTOR_COMPILE";
	static constexpr const char* kPreNGDFLightFullContractDescriptorBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_DESCRIPTOR_BIND";
	static constexpr const char* kPreNGDFLightDescriptorObserveEnv = "FO4CS_LLF_PRENG_DFLIGHT_DESCRIPTOR_OBSERVE";
	static constexpr const char* kPreNGDFLightFullShadowedBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_BIND";
	static constexpr const char* kPreNGDFLightFullShadowedBindBudgetEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_BIND_BUDGET";
	static constexpr const char* kPreNGDFLightFullShadowedBindBudgetAliasEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_BUDGET";
	static constexpr const char* kPreNGDFLightFullShadowedDescriptorConsumerEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER";
	static constexpr const char* kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER_UNSAFE";
	static constexpr const char* kPreNGBSLightingContractCompileEnv = "FO4CS_LLF_PRENG_BSLIGHTING_CONTRACT_COMPILE";
	static constexpr const char* kPreNGBSLightingConsumerCompileEnv = "FO4CS_LLF_PRENG_BSLIGHTING_CONSUMER_COMPILE";
	static constexpr const char* kPreNGBSLightingDescriptorObserveEnv = "FO4CS_LLF_PRENG_BSLIGHTING_DESCRIPTOR_OBSERVE";
	static constexpr const char* kPreNGBSLightingResourceBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_RESOURCE_BIND";
	static constexpr const char* kPreNGBSLightingVanillaBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_VANILLA_BIND";
	static constexpr const char* kPreNGBSLightingLLFBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_LLF_BIND";
	static constexpr const char* kPreNGDFCompositeDescriptorObserveEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_DESCRIPTOR_OBSERVE";
	static constexpr const char* kPreNGDFCompositeDescriptorCompileEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_DESCRIPTOR_COMPILE";
	static constexpr const char* kPreNGDFCompositeResourceBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_RESOURCE_BIND";
	static constexpr const char* kPreNGDFCompositeSafeBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_SAFE_BIND";
	static constexpr const char* kPreNGDFCompositeVisibleLLFEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_VISIBLE_LLF";
	static constexpr const char* kPreNGDFCompositeFogSafeBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_FOG_SAFE_BIND";
	static constexpr const char* kPreNGPersistentClusterPrepassEnv = "FO4CS_LLF_PRENG_PERSISTENT_CLUSTER_PREPASS";
	static constexpr std::size_t kPreNGMaxFxpFilenameLength = 96;
	static constexpr std::uint32_t kPreNGDFLightFullShadowedPixelDesc920 = F4Runtime::PreNG::DF_LIGHT_FULL_SHADOWED_PIXEL_DESCRIPTOR_920;
	static constexpr std::uint32_t kPreNGDFLightFullShadowedPixelDesc922 = F4Runtime::PreNG::DF_LIGHT_FULL_SHADOWED_PIXEL_DESCRIPTOR_922;
	static constexpr std::uint32_t kPreNGDefaultDFLightFullShadowedCandidateBindBudget = 1;
	static constexpr std::uint32_t kPreNGMinDFLightFullShadowedCandidateBindBudget = 1;
	static constexpr std::uint32_t kPreNGMaxDFLightFullShadowedCandidateBindBudget = 2048;
	static constexpr std::uint32_t kPreNGMaxDFLightFullShadowedCandidateBindLogs = 16;
	static constexpr std::size_t kPreNGMaxBSLightingVanillaDumpDiagnostics = 8;
	static constexpr std::size_t kPreNGMaxDFLightVanillaDumpDiagnostics = 8;
	static constexpr std::size_t kPreNGMaxDFCompositeVanillaDumpDiagnostics = 8;
	static constexpr std::size_t kPreNGMaxShaderLookupFirstSeenDiagnostics = 96;
	static constexpr std::string_view kPreNGDFLightFullShadowedCandidateSource = "LightLimitFix\\DFLightFullShadowedPS.hlsl";
#endif
}
