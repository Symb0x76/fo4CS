#include "Upscaling/UpscalingInternal.h"

#include "Upscaling/Upscaler.h"

#include "Diagnostics/HangTrace.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace fo4cs::upscaling
{
	uint64_t NextHUDLessFrameID()
	{
		static uint64_t nextFrameID = 0;
		return ++nextFrameID;
	}

	bool IsLoadingMenuOpen()
	{
		if (auto ui = RE::UI::GetSingleton()) {
			return ui->GetMenuOpen("LoadingMenu");
		}
		return false;
	}

	float NormalizeRealFrameRateLimit(float value, const char* settingName)
	{
		if (!std::isfinite(value) || value < 0.0f) {
			logger::warn("[FrameGen] {}={} is invalid, using automatic policy", settingName, value);
			return 0.0f;
		}
		if (value == 0.0f)
			return 0.0f;

		const float clamped = std::clamp(value, 30.0f, 1000.0f);
		if (clamped != value) {
			logger::warn("[FrameGen] {}={} is out of range, clamping to {}", settingName, value, clamped);
		}
		return clamped;
	}

	std::string FormatRealFrameRate(float value)
	{
		if (value == 0.0f)
			return "0";
		if (std::floor(value) == value)
			return std::format("{:.0f}", value);
		return std::format("{:.3f}", value);
	}
}
