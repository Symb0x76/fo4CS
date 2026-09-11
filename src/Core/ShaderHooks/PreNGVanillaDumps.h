#pragma once

#include <cstdint>

namespace RE
{
	class BSShader;
}

// Targeted dumps of the engine's own (vanilla) pixel shaders, taken from inside
// the lookup hook at the moment the engine resolves one.
//
// Each family dumps at most kPreNGMax<Family>VanillaDumpDiagnostics distinct
// (pixelDescriptor, D3D object) pairs, so repeatedly re-entering a cell does not
// grow the dump directory without bound. Diagnostics only: nothing here binds or
// substitutes a shader, and every entry point is safe to call unconditionally --
// the dedup and limit checks live behind the call.
namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	void DumpPreNGBSLightingVanillaShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		std::uint8_t a_lookupResult);

	void DumpPreNGDFLightVanillaShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		std::uint8_t a_lookupResult);

	void DumpPreNGDFCompositeVanillaShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		std::uint8_t a_lookupResult);
#endif
}
