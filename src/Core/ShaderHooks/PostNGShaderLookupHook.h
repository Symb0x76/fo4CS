#pragma once

#include <cstdint>

#include <RE/FO4Runtime.h>

namespace RE
{
	class BSShader;
}

// The PostNG / PostAE shader-lookup detour and its LLF consumer bind.
//
// Mirrors the PreNG BSLighting LLF consumer, but against the statically verified
// PostNG:: addresses. FO4 forward has no b3 strict-light buffer, so the consumer
// swaps the pixel shader and re-asserts the cluster SRVs t35-t37 that
// RunClusterPrepass already binds.
//
// This unit is the whole non-PreNG half of BSShaderHooks and shares nothing with
// the PreNG clusters.
namespace CommunityShaders
{
#if !defined(FALLOUT_PRE_NG)
	// The alias and the two env names live here rather than in the .cpp because
	// BSShaderHooks::Install still needs them: its #else branch names both switches
	// in the held-detour log and reaches F4Runtime::PostNG::BS_SHADER_LOOKUP for the
	// detour address. PreNGShaderHookConstants.h supplies the same alias for PreNG
	// builds and is compiled out here, so this is the only definition non-PreNG
	// translation units get.
	namespace F4Runtime = RE::FO4Runtime;

	// static constexpr for the same reason as the PreNG constants: each including
	// translation unit gets its own copy with identical values, which is what
	// BSShaderHooks.cpp already had. Nothing takes their addresses.
	static constexpr std::int32_t kPostNGBSLightingShaderType =
		static_cast<std::int32_t>(F4Runtime::ShaderType::kLighting);
	static constexpr const char* kPostNGBSLightingDescriptorObserveEnv =
		"FO4CS_LLF_POSTNG_BSLIGHTING_DESCRIPTOR_OBSERVE";
	static constexpr const char* kPostNGBSLightingLLFBindEnv =
		"FO4CS_LLF_POSTNG_BSLIGHTING_LLF_BIND";

	bool ShouldObservePostNGBSLightingDescriptors();
	bool ShouldBindPostNGBSLightingLLFConsumerShader();

	// Install() writes func with the trampoline Detours hands back and installs
	// thunk, so both members have to be visible outside this unit. thunk's body
	// moves to the .cpp -- keeping it in-class would drag the consumer bind and the
	// PostNG pointer readers into every includer.
	struct PostNGShaderLookup
	{
		static std::uint8_t thunk(
			RE::BSShader* a_shader,
			std::int32_t a_vertexDescriptor,
			std::int32_t a_hullDescriptor,
			std::int32_t a_domainDescriptor,
			std::int32_t a_pixelDescriptor);

		static inline std::uint8_t (*func)(RE::BSShader*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) = nullptr;
	};
#endif
}
