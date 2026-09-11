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

// Logging for what the lookup hook did to a descriptor: how it was mutated
// before the engine resolved it, what got bound afterwards, and what the
// DFComposite path observed.
//
// Every entry point deduplicates on a per-call-shape key and stops after
// kPreNGMaxDescriptor*Diagnostics distinct shapes, so these are safe to call
// unconditionally from the hot path -- the engine resolves the same descriptors
// thousands of times per second and only the first of each shape is logged.
//
// Diagnostics only. Nothing here mutates engine state; the binds these describe
// live in the bind clusters, which call LogPreNGDescriptorBind to report.
namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	void LogPreNGDescriptorMutation(
		RE::BSShader* a_shader,
		std::int32_t a_originalVertexDescriptor,
		std::int32_t a_originalPixelDescriptor,
		std::int32_t a_modifiedVertexDescriptor,
		std::int32_t a_modifiedPixelDescriptor,
		const char* a_mutateState,
		const char* a_reason);

	void LogPreNGDescriptorBind(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		RE::BSGraphics::VertexShader* a_vertexShader,
		RE::BSGraphics::PixelShader* a_pixelShader,
		std::uintptr_t a_hullEntry,
		std::uintptr_t a_domainEntry,
		const char* a_bindState,
		const char* a_reason);

	void TracePreNGDFCompositeDescriptor(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found);

	void ObservePreNGDFCompositeDescriptorShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found);
#endif
}
