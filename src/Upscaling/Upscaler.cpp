#include "Upscaling/Upscaler.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostics/LogEvents.h"

using fo4cs::diagnostics::Event;
using fo4cs::diagnostics::LogEvent;

namespace
{
	struct IniSource
	{
		std::filesystem::path path;
		const char* label;
	};

	bool LoadIniIfExists(CSimpleIniA& ini, const IniSource& source, const char* component)
	{
		std::error_code ec;
		if (!std::filesystem::exists(source.path, ec)) {
			return false;
		}

		if (ini.LoadFile(source.path.string().c_str()) < 0) {
			logger::warn("[{}] Failed to load {} settings from {}", component, source.label, source.path.string());
			return false;
		}

		logger::debug("[{}] Loaded {} settings from {}", component, source.label, source.path.string());
		return true;
	}

	int ClampIntSetting(int value, int minValue, int maxValue, const char* settingName)
	{
		const int clamped = std::clamp(value, minValue, maxValue);
		if (clamped != value) {
			logger::warn("[Upscaler] {}={} is out of range, clamping to {}", settingName, value, clamped);
		}
		return clamped;
	}

	std::optional<std::string> GetEnvironmentValue(const char* name)
	{
		char buffer[64]{};
		const auto length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(std::size(buffer)));
		if (length == 0 || length >= std::size(buffer)) {
			return std::nullopt;
		}
		return std::string(buffer, length);
	}

	bool IsTruthy(std::string_view value)
	{
		return value == "1" || value == "true" || value == "TRUE" || value == "on" || value == "ON";
	}

	bool IsFrameGenPluginVisible()
	{
		std::error_code ec;
		if (std::filesystem::exists("Data\\F4SE\\Plugins\\FrameGen\\FrameGen.dll", ec))
			return true;
		if (GetModuleHandleW(L"NuclearGFX.dll") != nullptr)
			return true;

		HMODULE currentModule = nullptr;
		if (GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&IsFrameGenPluginVisible),
				&currentModule)) {
			std::array<wchar_t, 4096> modulePath{};
			const auto length = GetModuleFileNameW(currentModule, modulePath.data(), static_cast<DWORD>(modulePath.size()));
			if (length > 0 && length < modulePath.size()) {
				auto fileName = std::filesystem::path(modulePath.data(), modulePath.data() + length).filename().wstring();
				if (_wcsicmp(fileName.c_str(), L"CommunityShaders.dll") == 0)
					return true;
			}
		}

		return false;
	}

	std::filesystem::path GetModuleDirectory(HMODULE module)
	{
		std::array<wchar_t, 4096> buffer{};
		const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size()) {
			return {};
		}

		return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
	}

	std::filesystem::path GetCurrentPluginDirectory()
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetCurrentPluginDirectory),
				&module)) {
			return {};
		}

		return GetModuleDirectory(module);
	}

	bool HasStreamlineInterposer()
	{
		std::error_code ec;
		const auto exists = [&](const std::filesystem::path& directory) {
			return !directory.empty() && std::filesystem::exists(directory / L"sl.interposer.dll", ec);
		};

		if (exists(GetCurrentPluginDirectory() / L"Streamline")) {
			return true;
		}
		ec.clear();

		if (exists(GetModuleDirectory(nullptr) / L"Data" / L"F4SE" / L"Plugins" / L"Streamline")) {
			return true;
		}

		return false;
	}

	std::optional<int> ParseIntSetting(std::string_view value)
	{
		try {
			return std::stoi(std::string(value));
		} catch (...) {
			return std::nullopt;
		}
	}

#ifdef FO4CS_ENABLE_DEBUG_SETTINGS
	constexpr bool kDebugSettingsSupported = true;
#else
	constexpr bool kDebugSettingsSupported = false;
#endif

	void LoadSharedDebugSettings(Upscaling::Settings& settings)
	{
		if constexpr (kDebugSettingsSupported) {
			settings.debugLogging = true;
			settings.streamlineLogLevel = 2;
			settings.debugFrameLogCount = 240;
		} else {
			settings.debugLogging = false;
			settings.streamlineLogLevel = 0;
			settings.debugFrameLogCount = 0;
		}
	}

	void ApplyDebugEnvironmentOverrides(Upscaling::Settings& settings)
	{
		if constexpr (!kDebugSettingsSupported) {
			return;
		}

		if (const auto value = GetEnvironmentValue("FO4CS_DEBUG_LOG")) {
			settings.debugLogging = IsTruthy(*value);
		}
		if (const auto value = GetEnvironmentValue("FO4CS_STREAMLINE_LOG_LEVEL")) {
			if (const auto parsed = ParseIntSetting(*value)) {
				settings.streamlineLogLevel = ClampIntSetting(*parsed, 0, 2, "FO4CS_STREAMLINE_LOG_LEVEL");
			}
		}
		if (const auto value = GetEnvironmentValue("FO4CS_DEBUG_FRAMES")) {
			if (const auto parsed = ParseIntSetting(*value)) {
				settings.debugFrameLogCount = ClampIntSetting(*parsed, 0, 600, "FO4CS_DEBUG_FRAMES");
			}
		}
	}

	void ConfigureDebugLogging(const Upscaling::Settings& settings)
	{
		if (!settings.debugLogging) {
			return;
		}

		spdlog::set_level(spdlog::level::debug);
		if (auto log = spdlog::default_logger()) {
			log->flush_on(spdlog::level::warn);
		}
	}
}

void Upscaling::LoadFrameGenerationSettings()
{
	const std::vector<IniSource> iniSources{
		{ std::filesystem::path("Data\\F4SE\\Plugins\\FrameGen\\FrameGen.ini"), "default" }
	};

	CSimpleIniA ini;
	ini.SetUnicode();

	bool loadedAny = false;
	for (const auto& source : iniSources) {
		loadedAny = LoadIniIfExists(ini, source, "Frame Generation") || loadedAny;
	}

	if (!loadedAny) {
		logger::warn("[FrameGen] Settings file not found, using defaults");
	}

	settings.frameGenerationMode = ini.GetBoolValue("Settings", "bFrameGenerationMode", true);
	settings.frameLimitMode = ini.GetBoolValue("Settings", "bFrameLimitMode", true);
	settings.frameGenerationBackend = ClampIntSetting(
		static_cast<int>(ini.GetLongValue("Settings", "iFrameGenerationBackend", kFrameGenerationBackendDLSS)),
		0,
		kFrameGenerationBackendFSR,
		"iFrameGenerationBackend");
	LoadSharedDebugSettings(settings);
	ApplyDebugEnvironmentOverrides(settings);
	ConfigureDebugLogging(settings);
	ApplyRuntimeFallbacks();
}

void Upscaling::LoadReflexSettings()
{
	const std::vector<IniSource> iniSources{
		{ std::filesystem::path("Data\\F4SE\\Plugins\\Reflex\\Reflex.ini"), "default" }
	};

	CSimpleIniA ini;
	ini.SetUnicode();

	bool loadedAny = false;
	for (const auto& source : iniSources) {
		loadedAny = LoadIniIfExists(ini, source, "Reflex") || loadedAny;
	}

	if (!loadedAny) {
		logger::warn("[Reflex] Settings file not found, using defaults");
	}

	settings.reflexMode = ClampIntSetting(
		static_cast<int>(ini.GetLongValue("Settings", "iReflexMode", settings.reflexMode)),
		0,
		2,
		"iReflexMode");
	settings.reflexSleepMode = ini.GetBoolValue("Settings", "bReflexSleepMode", settings.reflexSleepMode);
	LoadSharedDebugSettings(settings);
	ApplyDebugEnvironmentOverrides(settings);
	ConfigureDebugLogging(settings);
	ApplyRuntimeFallbacks();

	LogEvent(Event::FeatureState,
		"[Settings] Reflex(mode={}, sleep={}), Debug(enabled={}, streamlineLogLevel={}, frames={})",
		settings.reflexMode,
		settings.reflexSleepMode,
		settings.debugLogging,
		settings.streamlineLogLevel,
		settings.debugFrameLogCount);
}

void Upscaling::LoadSettings()
{
	LoadFrameGenerationSettings();

	if (pluginMode == PluginMode::kUpscaler && !IsFrameGenPluginVisible()) {
		settings.frameGenerationMode = false;
		settings.frameLimitMode = false;
		logger::info("[FrameGen] FrameGen.dll is not visible; disabling frame generation in Upscaler mode");
	}

	const std::vector<IniSource> upscalerIniSources{
		{ std::filesystem::path("Data\\F4SE\\Plugins\\Upscaler\\Upscaler.ini"), "default" }
	};

	CSimpleIniA upscalerIni;
	upscalerIni.SetUnicode();

	bool loadedUpscalerSettings = false;
	for (const auto& source : upscalerIniSources) {
		loadedUpscalerSettings = LoadIniIfExists(upscalerIni, source, "Upscaler") || loadedUpscalerSettings;
	}

	if (!loadedUpscalerSettings) {
		logger::warn("[Upscaler] Settings file not found, using defaults");
	}

	settings.upscaleMethodPreference = ClampIntSetting(
		static_cast<int>(upscalerIni.GetLongValue("Settings", "iUpscaleMethodPreference", settings.upscaleMethodPreference)),
		0,
		2,
		"iUpscaleMethodPreference");
	settings.qualityMode = ClampIntSetting(
		static_cast<int>(upscalerIni.GetLongValue("Settings", "iQualityMode", settings.qualityMode)),
		0,
		4,
		"iQualityMode");
	settings.dlssPreset = ClampIntSetting(
		static_cast<int>(upscalerIni.GetLongValue("Settings", "iDLSSPreset", settings.dlssPreset)),
		0,
		15,
		"iDLSSPreset");

	ApplyRuntimeFallbacks();

	LogEvent(Event::FeatureState,
		"[Settings] FrameGen(enabled={}, limiter={}, backend={}), Upscaler(method={}, quality={}, dlssPreset={}), Reflex(mode={}), Debug(enabled={}, streamlineLogLevel={}, frames={})",
		settings.frameGenerationMode,
		settings.frameLimitMode,
		settings.frameGenerationBackend,
		settings.upscaleMethodPreference,
		settings.qualityMode,
		settings.dlssPreset,
		settings.reflexMode,
		settings.debugLogging,
		settings.streamlineLogLevel,
		settings.debugFrameLogCount);
}

void Upscaling::ApplyRuntimeFallbacks()
{
	if (settings.frameGenerationBackend != kFrameGenerationBackendDLSS &&
		settings.frameGenerationBackend != kFrameGenerationBackendFSR) {
		settings.frameGenerationBackend = kFrameGenerationBackendDLSS;
	}

	const bool dlssUnavailable = GetDLSSUnavailableReason() != nullptr;
	if (dlssUnavailable && settings.upscaleMethodPreference == static_cast<int>(UpscaleMethod::kDLSS)) {
		settings.upscaleMethodPreference = static_cast<int>(UpscaleMethod::kFSR);
		logger::info("[Upscaler] DLSS unavailable; switching Upscaling to FSR");
	}

	if (dlssUnavailable && settings.frameGenerationBackend == kFrameGenerationBackendDLSS) {
		settings.frameGenerationBackend = kFrameGenerationBackendFSR;
		logger::info("[FrameGen] DLSS-G unavailable; switching Frame Generation to FSR FG");
	}

	if (pluginMode == PluginMode::kUpscaler && !IsFrameGenPluginVisible()) {
		if (settings.frameGenerationMode || settings.frameLimitMode) {
			logger::info("[FrameGen] FrameGen.dll is not visible; disabling frame generation in Upscaler mode");
		}
		settings.frameGenerationMode = false;
		settings.frameLimitMode = false;
	}

	if (IsPreNGRuntime() && settings.reflexMode != 0) {
		settings.reflexMode = 0;
		logger::info("[Reflex] PreNG detected; disabling Reflex");
	}
}

const char* Upscaling::GetDLSSUnavailableReason() const
{
	if (IsPreNGRuntime()) {
		return "PreNG (1.10.163) detected: DLSS is unavailable because of an engine issue. FSR is selected automatically for Frame Generation and Upscaling.";
	}

	if (!IsStreamlineRuntimeAvailable()) {
		return "NVIDIA Streamline runtime is missing (sl.interposer.dll). FSR is selected automatically for Frame Generation and Upscaling; DLSS and Reflex are unavailable.";
	}

	return nullptr;
}

bool Upscaling::IsPreNGRuntime() noexcept
{
#if defined(FALLOUT_PRE_NG)
	return true;
#else
	return false;
#endif
}

bool Upscaling::IsStreamlineRuntimeAvailable()
{
	return HasStreamlineInterposer();
}

Upscaling::UpscaleMethod Upscaling::GetPreferredUpscaleMethod() const
{
	switch (static_cast<UpscaleMethod>(settings.upscaleMethodPreference)) {
	case UpscaleMethod::kFSR:
		return UpscaleMethod::kFSR;
	case UpscaleMethod::kDLSS:
		return UpscaleMethod::kDLSS;
	default:
		return UpscaleMethod::kDisabled;
	}
}

bool Upscaling::UsesDLSSUpscaling() const
{
	return pluginMode == PluginMode::kUpscaler && GetPreferredUpscaleMethod() == UpscaleMethod::kDLSS;
}

bool Upscaling::UsesFSRUpscaling() const
{
	return pluginMode == PluginMode::kUpscaler && GetPreferredUpscaleMethod() == UpscaleMethod::kFSR;
}

bool Upscaling::UsesDLSSFrameGeneration() const
{
	if (pluginMode == PluginMode::kReflex || !settings.frameGenerationMode)
		return false;

	switch (settings.frameGenerationBackend) {
	case kFrameGenerationBackendDLSS:
		return true;
	case kFrameGenerationBackendFSR:
		return false;
	default:
		return false;
	}
}

bool Upscaling::UsesFSRFrameGeneration() const
{
	if (pluginMode == PluginMode::kReflex || !settings.frameGenerationMode)
		return false;

	switch (settings.frameGenerationBackend) {
	case kFrameGenerationBackendDLSS:
		return false;
	case kFrameGenerationBackendFSR:
		return true;
	default:
		return false;
	}
}

bool Upscaling::UsesReflex() const
{
	return UsesDLSSFrameGeneration() || (pluginMode == PluginMode::kReflex && settings.reflexMode > 0);
}
