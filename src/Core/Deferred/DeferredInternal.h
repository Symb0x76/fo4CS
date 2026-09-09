#pragma once

#include "Core/Deferred.h"

#include <array>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <d3d11.h>
#include <d3d11_1.h>
#include <winrt/base.h>

// Shared surface between the Deferred translation units.
//
// Everything here was an anonymous-namespace member of Deferred.cpp, so it had
// internal linkage and exactly one definition by construction. Splitting the file
// forces these to become external, and two of them own state that MUST NOT be
// duplicated:
//
//   * IsDeferredTraceEnabled / IsGBufferDumpEnabled each cache their Debug.ini
//     lookup in a function-local `static const bool`. One definition each, in
//     DeferredTrace.cpp.
//   * DumpGBufferSnapshot owns a one-shot `static std::atomic_bool dumped` latch.
//     Duplicating it turns "dump the GBuffer once" into "once per translation
//     unit". One definition, in DeferredTrace.cpp.
//
// Declare here, define there. Never make these inline.
namespace CommunityShaders::deferred
{
	// Per-thread save slot for the output-merger state that BeginLightingDraw
	// overrides and EndLightingDraw restores. Lives in the header only because
	// TraceRestoreCheck takes it by const reference; the `thread_local` instance
	// itself must stay in the same translation unit as Begin/EndLightingDraw.
	struct LightingDrawState
	{
		bool active = false;
		UINT renderTargetCount = 0;
		std::array<winrt::com_ptr<ID3D11RenderTargetView>, Deferred::kMaxBoundRenderTargetCount> renderTargets;
		winrt::com_ptr<ID3D11DepthStencilView> depthStencil;
		winrt::com_ptr<ID3D11BlendState> blendState;
		std::array<FLOAT, 4> blendFactor{};
		UINT sampleMask = D3D11_DEFAULT_SAMPLE_MASK;
		winrt::com_ptr<ID3DUserDefinedAnnotation> annotation;
	};

	[[nodiscard]] bool IsDeferredTraceEnabled() noexcept;
	[[nodiscard]] bool IsGBufferDumpEnabled() noexcept;

	// The I/O half of TraceDeferred: resolves the trace directory, stamps the frame
	// number and appends one line under deferredTraceLock. Split out so that the
	// lock and the file handling have a single definition while the variadic
	// formatting stays visible to every call site.
	void TraceDeferredLine(std::string_view a_line);

	// Formatting stays in the header because it is a template, but the enabled
	// check stays in FRONT of the formatting: TraceDeferred is called from the draw
	// hooks, so with tracing off this must cost one cached bool test and nothing
	// else. Do not hoist the std::format call above the guard.
	template <class... Args>
	void TraceDeferred(std::format_string<Args...> a_format, Args&&... a_args)
	{
		try {
			if (!IsDeferredTraceEnabled()) {
				return;
			}
			TraceDeferredLine(std::format(a_format, std::forward<Args>(a_args)...));
		} catch (...) {
			// Diagnostic output must never terminate a render hook.
		}
	}

	[[nodiscard]] std::string DescribeRTV(ID3D11RenderTargetView* a_rtv);
	[[nodiscard]] std::string DescribeDSV(ID3D11DepthStencilView* a_dsv);

	void TraceOMState(ID3D11DeviceContext* a_context, std::string_view a_phase);
	void TraceRestoreCheck(ID3D11DeviceContext* a_context, const LightingDrawState& a_state);
	void DumpGBufferSnapshot();
}
