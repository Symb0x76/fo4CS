#include "Upscaling/Streamline.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Render/DX12SwapChain.h"
#include "Upscaling/Upscaler.h"
#include "Upscaling/StreamlineInternal.h"

using fo4cs::streamline::EnumToString;
using fo4cs::streamline::GetConfiguredReflexMode;
using fo4cs::streamline::ResultToString;
using fo4cs::streamline::ShouldTraceStreamlineFrame;

bool Streamline::ConfigureReflex(sl::ReflexMode mode, const char* reason)
{
	if (!featureReflex || !slReflexSetOptions) {
		static bool loggedMissingReflex = false;
		if (!loggedMissingReflex) {
			logger::warn("[Streamline] Reflex is unavailable; requested by {}", reason ? reason : "unknown");
			loggedMissingReflex = true;
		}
		return false;
	}

	sl::ReflexOptions options{};
	options.mode = mode;
	options.frameLimitUs = 0;
	options.useMarkersToOptimize = false;

	if (reflexOptionsValid && reflexConfiguredMode == options.mode) {
		return true;
	}

	const auto result = slReflexSetOptions(options);
	if (result != sl::Result::eOk) {
		logger::warn("[Streamline] slReflexSetOptions failed: {}", ResultToString(result));
		return false;
	}

	reflexOptionsValid = true;
	reflexConfiguredMode = options.mode;
	logger::info("[Streamline] Reflex mode={} reason={}", EnumToString(options.mode), reason ? reason : "unknown");
	return true;
}

void Streamline::ConfigureReflexForDLSSG()
{
	if (!featureDLSSG) {
		return;
	}

	if (!ConfigureReflex(sl::ReflexMode::eLowLatency, "DLSS-G")) {
		static bool loggedDLSSGMissingReflex = false;
		if (!loggedDLSSGMissingReflex) {
			logger::warn("[Streamline] Reflex is unavailable; DLSS-G may report eFailReflexNotDetectedAtRuntime");
			loggedDLSSGMissingReflex = true;
		}
	}
}

bool Streamline::SleepReflexFrame(const char* reason)
{
	auto upscaling = Upscaling::GetSingleton();
	if (!upscaling->UsesReflex() || (upscaling->pluginMode == Upscaling::PluginMode::kReflex && !upscaling->settings.reflexSleepMode)) {
		return false;
	}

	if (!initialized || !featureReflex || !slReflexSleep) {
		return false;
	}

	if (upscaling->pluginMode == Upscaling::PluginMode::kReflex && !ConfigureReflex(GetConfiguredReflexMode(), reason)) {
		return false;
	}

	if (!EnsureFrameToken(reason ? reason : "Reflex sleep")) {
		return false;
	}

	const auto result = slReflexSleep(*frameToken);
	if (result != sl::Result::eOk) {
		static bool loggedSleepFailure = false;
		if (!loggedSleepFailure || upscaling->settings.debugLogging) {
			logger::warn("[Streamline] slReflexSleep failed: {}", ResultToString(result));
			loggedSleepFailure = true;
		}
		return false;
	}

	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug("[Streamline] Reflex sleep completed (frame={}, reason={})", frameID, reason ? reason : "unknown");
	}
	return true;
}

bool Streamline::SetPCLMarker(sl::PCLMarker marker, const char* reason)
{
	if (!initialized || !featurePCL || !slPCLSetMarker) {
		return false;
	}

	if (!EnsureFrameToken(reason ? reason : "PCL marker")) {
		return false;
	}

	const auto result = slPCLSetMarker(marker, *frameToken);
	if (result != sl::Result::eOk) {
		static bool loggedMarkerFailure = false;
		if (!loggedMarkerFailure || Upscaling::GetSingleton()->settings.debugLogging) {
			logger::warn("[Streamline] slPCLSetMarker failed marker={} reason={} result={}", static_cast<uint32_t>(marker), reason ? reason : "unknown", ResultToString(result));
			loggedMarkerFailure = true;
		}
		return false;
	}

	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug("[Streamline] PCL marker set (frame={}, marker={}, reason={})", frameID, static_cast<uint32_t>(marker), reason ? reason : "unknown");
	}
	return true;
}
