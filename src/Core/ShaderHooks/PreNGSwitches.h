#pragma once

#include "Core/ShaderHooks/PreNGShaderHookConstants.h"

#include <cstdint>

// Every Debug.ini gate the PreNG shader hooks consult, in one translation unit.
//
// That grouping is deliberate, not cosmetic. Each of these caches its lookup in a
// function-local `static const bool` (or `static const auto` for the budget), so
// the switch is read once per process and every later call is a load. Keeping
// them together means each latch keeps exactly one definition; splitting them
// across TUs, or "simplifying" them into plain functions or constexpr reads,
// would make the gates re-read Debug.ini on the draw path they guard.
namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	std::uint32_t GetPreNGDFLightFullShadowedCandidateBindBudget();
	bool ShouldBindPreNGDescriptorShaders();
	bool ShouldBindPreNGDFLightFullContractDescriptorShader();
	bool ShouldCompilePreNGBSLightingContractShader();
	bool ShouldCompilePreNGBSLightingConsumerShader();
	bool ShouldObservePreNGBSLightingDescriptors();
	bool ShouldBindPreNGBSLightingDescriptorResources();
	bool ShouldBindPreNGBSLightingVanillaDescriptorShader();
	bool ShouldBindPreNGBSLightingLLFConsumerShader();
	bool ShouldCompilePreNGDFLightFullContractDescriptorShader();
	bool ShouldObservePreNGDFLightDescriptors();
	bool ShouldObservePreNGDFCompositeDescriptors();
	bool ShouldCompilePreNGDFCompositeDescriptorShader();
	bool ShouldBindPreNGDFCompositeDescriptorResources();
	bool ShouldBindPreNGDFCompositeVisibleDescriptorResources(std::uint32_t a_pixelDescriptor);
	bool ShouldBindPreNGDFCompositeSafeDescriptorShader();
	bool ShouldBindPreNGDFCompositeFogSafeDescriptorShader();
	bool ShouldPersistPreNGClusterPrepass();
	bool ShouldMutatePreNGDescriptorShaders();
	bool ShouldEnablePreNGShaderLookupDiagnostic();
	bool ShouldCompilePreNGDescriptorShadersForDiagnostic();
	bool ShouldBindPreNGDFLightFullShadowedCandidate();
	bool ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer();
	bool ShouldDumpPreNGBSLightingVanillaShader();
	bool ShouldDumpPreNGDFLightVanillaShader();
	bool ShouldDumpPreNGDFCompositeVanillaShader();
#endif
}
