#pragma once

#include <cstdint>
#include <string_view>

// Helpers shared by the Upscaling translation units. Each has exactly one
// definition (UpscalingInternal.cpp); NextHUDLessFrameID owns the single
// monotonic counter behind Upscaling::hudLessFrameIDs.
namespace fo4cs::upscaling
{
	[[nodiscard]] uint64_t NextHUDLessFrameID();
	[[nodiscard]] bool IsLoadingMenuOpen();

	// Debug-log a render-backend stage transition once (deduplicated against the
	// previous stage) and mirror it to the hang trace.
	void TraceRenderBackendStage(std::string_view stage);
}
