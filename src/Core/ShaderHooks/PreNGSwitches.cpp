#include "Core/ShaderHooks/PreNGSwitches.h"

#include "Core/PreNGEnvironment.h"
#include "Core/ShaderHooks/PreNGRuntime.h"

#include <cstdint>

namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	std::uint32_t GetPreNGDFLightFullShadowedCandidateBindBudget()
	{
		static const std::uint32_t budget = [] {
			const auto primaryState = ReadPreNGEnvironmentUInt(kPreNGDFLightFullShadowedBindBudgetEnv);
			const auto aliasState = ReadPreNGEnvironmentUInt(kPreNGDFLightFullShadowedBindBudgetAliasEnv);
			const auto& state = primaryState.present ? primaryState : aliasState;
			const char* selectedEnv = primaryState.present ? kPreNGDFLightFullShadowedBindBudgetEnv :
				(aliasState.present ? kPreNGDFLightFullShadowedBindBudgetAliasEnv : kPreNGDFLightFullShadowedBindBudgetEnv);
			auto resolved = kPreNGDefaultDFLightFullShadowedCandidateBindBudget;
			auto clamped = false;
			if (state.present && state.valid) {
				const auto requested = state.value;
				resolved = std::clamp(
					requested,
					kPreNGMinDFLightFullShadowedCandidateBindBudget,
					kPreNGMaxDFLightFullShadowedCandidateBindBudget);
				clamped = resolved != requested;
			}

			logger::info(
				"[BSShaderHooks] PreNG DFLight full-shadowed candidate bind budget resolved {}={} source={} present={} valid={} clamped={} default={} range={}..{} aliasEnv={} aliasPresent={} aliasValid={}",
				selectedEnv,
				resolved,
				PreNGEnvironmentValueSourceName(state.source),
				state.present,
				state.valid,
				clamped,
				kPreNGDefaultDFLightFullShadowedCandidateBindBudget,
				kPreNGMinDFLightFullShadowedCandidateBindBudget,
				kPreNGMaxDFLightFullShadowedCandidateBindBudget,
				kPreNGDFLightFullShadowedBindBudgetAliasEnv,
				aliasState.present,
				aliasState.valid);
			return resolved;
		}();
		return budget;
	}

	bool ShouldBindPreNGDescriptorShaders()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDescriptorBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFLightFullContractDescriptorShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullContractDescriptorBindEnv);
		return enabled;
	}

	bool ShouldCompilePreNGBSLightingContractShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGBSLightingContractCompileEnv);
		return enabled;
	}

	bool ShouldCompilePreNGBSLightingConsumerShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGBSLightingConsumerCompileEnv);
		return enabled;
	}

	bool ShouldObservePreNGBSLightingDescriptors()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGBSLightingDescriptorObserveEnv);
		return enabled;
	}

	bool ShouldBindPreNGBSLightingDescriptorResources()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGBSLightingResourceBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGBSLightingVanillaDescriptorShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGBSLightingVanillaBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGBSLightingLLFConsumerShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGBSLightingLLFBindEnv);
		return enabled;
	}

	bool ShouldCompilePreNGDFLightFullContractDescriptorShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullContractDescriptorCompileEnv);
		return enabled;
	}

	bool ShouldObservePreNGDFLightDescriptors()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightDescriptorObserveEnv);
		return enabled;
	}

	bool ShouldObservePreNGDFCompositeDescriptors()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFCompositeDescriptorObserveEnv);
		return enabled;
	}

	bool ShouldCompilePreNGDFCompositeDescriptorShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFCompositeDescriptorCompileEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFCompositeDescriptorResources()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFCompositeResourceBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFCompositeVisibleDescriptorResources(std::uint32_t a_pixelDescriptor)
	{
		static const bool enabled =
			ReadPreNGEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv) &&
			ReadPreNGEnvironmentSwitch(kPreNGDFCompositeVisibleLLFEnv);
		return enabled &&
		       (a_pixelDescriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88 ||
		        a_pixelDescriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088);
	}

	bool ShouldBindPreNGDFCompositeSafeDescriptorShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFCompositeFogSafeDescriptorShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFCompositeFogSafeBindEnv);
		return enabled;
	}

	bool ShouldPersistPreNGClusterPrepass()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGPersistentClusterPrepassEnv);
		return enabled;
	}

	bool ShouldMutatePreNGDescriptorShaders()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDescriptorMutateEnv);
		return enabled;
	}

	bool ShouldEnablePreNGShaderLookupDiagnostic()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(PreNGEnvironment::kPreNGShaderLookupDiagEnv);
		return enabled;
	}

	bool ShouldCompilePreNGDescriptorShadersForDiagnostic()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDescriptorCompileEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFLightFullShadowedCandidate()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullShadowedBindEnv);
		return enabled;
	}

	bool ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer()
	{
		static const bool enabled = [] {
			const bool requested = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerEnv);
			const bool unsafeOverride = ReadPreNGEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
			if (requested && !unsafeOverride) {
				logger::warn(
					"[BSShaderHooks] PreNG DFLight full-shadowed descriptor consumer held; DFLightFullShadowedPS is not vanilla-equivalent and can black out sky-light-only views. Set {}=1 only for focused diagnostics.",
					kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
			}
			return requested && unsafeOverride;
		}();
		return enabled;
	}

	bool ShouldDumpPreNGBSLightingVanillaShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(PreNGEnvironment::kPreNGBSLightingVanillaDumpEnv);
		return enabled;
	}

	bool ShouldDumpPreNGDFLightVanillaShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(PreNGEnvironment::kPreNGDFLightVanillaDumpEnv);
		return enabled;
	}

	bool ShouldDumpPreNGDFCompositeVanillaShader()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(PreNGEnvironment::kPreNGDFCompositeVanillaDumpEnv);
		return enabled;
	}
#endif
}
