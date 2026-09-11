#pragma once

#include <cstdint>

namespace RE
{
	class BSShader;

	namespace BSGraphics
	{
		class PixelShader;
		class VertexShader;
	}
}

// The descriptor-driven pixel-shader binds: DFComposite, DFLight (full-shadowed
// candidate, full-contract and generic), and the generic vertex+pixel pair.
//
// Like the BSLighting binds these mutate live GPU state -- they write the
// engine's current-shader globals through the PreNG bind helper -- and each is
// gated by its own Debug.ini switch with an attempt limit, so a bind is tried
// only while its evidence is still being gathered.
//
// The full-shadowed candidate cache (compiled once, reused per descriptor) is
// private to this unit; only the entry points the lookup hook calls are here.
namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	void LogPreNGDFLightFullContractDescriptorBindHeld(
		const RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found);

	bool TryBindPreNGDFCompositeDescriptorPixelShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found,
		RE::BSGraphics::PixelShader* a_pixelShader);

	bool TryBindPreNGDFLightFullShadowedCandidate(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		std::uint8_t a_lookupResult);

	bool TryBindPreNGDescriptorShaders(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		RE::BSGraphics::VertexShader* a_vertexShader,
		RE::BSGraphics::PixelShader* a_pixelShader);

	bool TryBindPreNGDFLightDescriptorPixelShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		RE::BSGraphics::PixelShader* a_pixelShader);

	// TryBindPreNGDeferredLightingPixelShader lives in the .cpp but is not declared
	// here on purpose: it has no callers anywhere in the tree. It is pre-existing
	// dead code, moved verbatim rather than deleted, and kept at namespace scope so
	// its linkage -- and therefore the absence of a /W4 unreferenced-function
	// warning -- matches what it had in BSShaderHooks.cpp.
#endif
}
