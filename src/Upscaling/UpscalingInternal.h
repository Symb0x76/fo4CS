#pragma once

#include <cstdint>

// Helpers shared by the Upscaling translation units. Each has exactly one
// definition (UpscalingInternal.cpp); NextHUDLessFrameID owns the single
// monotonic counter behind Upscaling::hudLessFrameIDs.
namespace fo4cs::upscaling
{
	[[nodiscard]] uint64_t NextHUDLessFrameID();
	[[nodiscard]] bool IsLoadingMenuOpen();
}
