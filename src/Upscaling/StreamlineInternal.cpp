#include "Upscaling/StreamlineInternal.h"

#include "Upscaling/Upscaler.h"

namespace fo4cs::streamline
{
	std::string ResultToString(sl::Result result)
	{
		if (const auto name = magic_enum::enum_name(result); !name.empty()) {
			return std::string(name);
		}

		return std::to_string(static_cast<int>(result));
	}

	bool ShouldTraceStreamlineFrame(uint64_t frameID)
	{
		const auto upscaling = Upscaling::GetSingleton();
		const auto settings = upscaling->settings;
		const auto bootstrapFrames = std::min(settings.debugFrameLogCount, 12);
		return settings.debugLogging &&
			(upscaling->debugTraceCurrentPresent ||
			 frameID < static_cast<uint64_t>(bootstrapFrames));
	}

	sl::ReflexMode GetConfiguredReflexMode()
	{
		switch (Upscaling::GetSingleton()->settings.reflexMode) {
		case 2:
			return sl::ReflexMode::eLowLatencyWithBoost;
		case 1:
			return sl::ReflexMode::eLowLatency;
		default:
			return sl::ReflexMode::eOff;
		}
	}
}
