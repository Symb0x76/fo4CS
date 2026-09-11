#pragma once

#include <cstdint>

namespace RE
{
	class BSShader;
}

// Diagnostics for the shader-lookup detour itself: what the engine asked for,
// what it got back, how often, and whether the detour is still patched in.
//
// The heavy work here is self-limiting. Every trace deduplicates on a call-shape
// key and stops after kPreNGMaxShaderLookup*Diagnostics distinct shapes, and once
// the evidence budget is spent MaybeComplete... latches the whole cluster off so
// the hot path stops paying for metadata reads. ShouldBypass... is what the
// thunk checks to skip that work; those two are the only view the thunk has of
// the counters and completion flags, which stay private to this unit.
namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	// True once the heavy lookup diagnostics have gathered their budget and
	// latched off. The thunk uses this to skip hot-path metadata/audit work.
	bool ShouldBypassPreNGShaderLookupHeavyDiagnostics();

	// Checks the call budget and, on the transition, logs the completion summary
	// and latches ShouldBypass... true. Safe to call every lookup.
	void MaybeCompletePreNGShaderLookupHeavyDiagnostics();

	void TracePreNGShaderLookupEntry(
		RE::BSShader* a_shader,
		std::int32_t a_originalVertexDescriptor,
		std::int32_t a_originalHullDescriptor,
		std::int32_t a_originalDomainDescriptor,
		std::int32_t a_originalPixelDescriptor,
		std::int32_t a_lookupVertexDescriptor,
		std::int32_t a_lookupPixelDescriptor,
		bool a_found);

	void TracePreNGShaderLookup(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found);

	// LogPreNGShaderLookupDetourPatch is deliberately not here. It is a lookup
	// diagnostic by topic, but it reads PreNGBSShaderLookup::thunk and ::func, so
	// it belongs with the detour struct rather than with this unit.
#endif
}
