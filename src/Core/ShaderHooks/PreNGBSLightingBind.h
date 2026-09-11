#pragma once

#include <cstdint>

namespace RE
{
	class BSShader;
}

// The two BSLighting pixel-shader binds the lookup hook can perform.
//
// Unlike the diagnostic clusters, these mutate live GPU state: they write the
// engine's current-pixel-shader global and bind LLF's b3/t35-t37 cluster
// resources. Both are gated by Debug.ini switches and both latch a proof log
// once, so a bind is attempted only while its evidence is still being gathered.
//
// TryBind...Vanilla binds an alias of the engine's own PS, which proves the bind
// path works without changing pixels. TryBind...LLFConsumer binds the
// ShaderCache-compiled BSLightingLLFConsumerPS, which is the visible one.
namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	bool TryBindPreNGBSLightingVanillaPixelShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found);

	bool TryBindPreNGBSLightingLLFConsumerPixelShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found);
#endif
}
