#pragma once

#include <cstdint>
#include <string>

// Helpers shared by the Upscaling translation units. Each has exactly one
// definition (UpscalingInternal.cpp); NextHUDLessFrameID owns the single
// monotonic counter behind Upscaling::hudLessFrameIDs.
namespace fo4cs::upscaling
{
	[[nodiscard]] uint64_t NextHUDLessFrameID();
	[[nodiscard]] bool IsLoadingMenuOpen();

	// Validates fRealFrameRateLimit: non-finite/negative means "automatic",
	// 0 stays 0, anything else is clamped to [30, 1000]. Warns when it changes
	// the value. Shared by settings load and the frame-limiter policy.
	[[nodiscard]] float NormalizeRealFrameRateLimit(float value, const char* settingName);

	// Formats a frame-rate limit for logs: "0" for automatic, no decimals when
	// integral, three decimals otherwise.
	[[nodiscard]] std::string FormatRealFrameRate(float value);
}
