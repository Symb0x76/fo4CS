#include "HDR.h"
#include "Upscaler.h"

#include <algorithm>
#include <filesystem>
#include <vector>

#include "SimpleIni.h"

namespace
{
	struct IniSource
	{
		std::filesystem::path path;
		const char* label;
	};

	constexpr const char* kSettingsSection = "Settings";
	const std::filesystem::path kDefaultSettingsPath{ "Data\\F4SE\\Plugins\\HDR\\HDR.ini" };
	const std::filesystem::path kUserSettingsPath{ "Data\\F4SE\\Plugins\\HDR\\HDR.ini" };

	bool LoadIniIfExists(CSimpleIniA& ini, const IniSource& source)
	{
		std::error_code ec;
		if (!std::filesystem::exists(source.path, ec)) {
			return false;
		}

		if (ini.LoadFile(source.path.string().c_str()) < 0) {
			logger::warn("[HDR] Failed to load {} settings from {}", source.label, source.path.string());
			return false;
		}

		return true;
	}

	int ClampIntSetting(int value, int minValue, int maxValue, const char* settingName)
	{
		const int clamped = std::clamp(value, minValue, maxValue);
		if (clamped != value) {
			logger::warn("[HDR] {}={} is out of range, clamping to {}", settingName, value, clamped);
		}
		return clamped;
	}

	float ClampFloatSetting(float value, float minValue, float maxValue, const char* settingName)
	{
		const float clamped = std::clamp(value, minValue, maxValue);
		if (clamped != value) {
			logger::warn("[HDR] {}={} is out of range, clamping to {}", settingName, value, clamped);
		}
		return clamped;
	}

	HDRSettings ReadHDRSettings(const bool logResult)
	{
		const std::vector<IniSource> iniSources{
			{ kDefaultSettingsPath, "MCM default" },
			{ kUserSettingsPath, "MCM user override" }
		};

		CSimpleIniA ini;
		ini.SetUnicode();

		bool loadedAny = false;
		for (const auto& source : iniSources) {
			loadedAny = LoadIniIfExists(ini, source) || loadedAny;
		}

		if (!loadedAny) {
			logger::warn("[HDR] Settings file not found, using defaults");
		}

		HDRSettings settings{};
		settings.hdrMode = ClampIntSetting(
			static_cast<int>(ini.GetLongValue(kSettingsSection, "iHDRMode", settings.hdrMode)),
			0,
			2,
			"iHDRMode");
		settings.peakLuminance = ClampFloatSetting(
			static_cast<float>(ini.GetDoubleValue(kSettingsSection, "fPeakLuminance", settings.peakLuminance)),
			80.0f,
			10000.0f,
			"fPeakLuminance");
		settings.paperWhiteLuminance = ClampFloatSetting(
			static_cast<float>(ini.GetDoubleValue(kSettingsSection, "fPaperWhiteLuminance", settings.paperWhiteLuminance)),
			20.0f,
			1000.0f,
			"fPaperWhiteLuminance");
		settings.scRGBReferenceLuminance = ClampFloatSetting(
			static_cast<float>(ini.GetDoubleValue(kSettingsSection, "fScRGBReferenceLuminance", settings.scRGBReferenceLuminance)),
			20.0f,
			1000.0f,
			"fScRGBReferenceLuminance");
		settings.calibrationActive = ini.GetBoolValue(kSettingsSection, "bCalibrationActive", settings.calibrationActive);

		if (logResult) {
			logger::info(
				"[Settings] HDR(mode={}, peak={}, paperWhite={}, scRGBReference={}, calibration={})",
				settings.hdrMode,
				settings.peakLuminance,
				settings.paperWhiteLuminance,
				settings.scRGBReferenceLuminance,
				settings.calibrationActive);
		}

		return settings;
	}
}

bool HDRSettings::IsEnabled() const
{
	return hdrMode > 0;
}

HDRMode HDRSettings::GetMode() const
{
	switch (hdrMode) {
	case 1:
		return HDRMode::kScRGB;
	case 2:
		return HDRMode::kHDR10;
	default:
		return HDRMode::kDisabled;
	}
}

HDRSettings LoadHDRSettingsFromINI()
{
	return ReadHDRSettings(true);
}

HDRSettings ReloadCalibrationSettings()
{
	static bool lastMCMCalibrationActive = false;
	static HDRSettings cached;

	auto upscaling = Upscaling::GetSingleton();
	const bool mcmCalibrationActive = upscaling->settings.hdrCalibrationActive;
	const bool mcmChanged = mcmCalibrationActive != lastMCMCalibrationActive;
	lastMCMCalibrationActive = mcmCalibrationActive;

	// Only hit disk when calibration is active or MCM just toggled
	if (!mcmCalibrationActive && !cached.calibrationActive && !mcmChanged) {
		return cached;
	}

	cached = ReadHDRSettings(false);

	if (mcmCalibrationActive && !cached.calibrationActive) {
		cached.calibrationActive = true;
		SaveHDRSettingsToINI(cached);
	}

	return cached;
}

void SaveHDRSettingsToINI(const HDRSettings& settings)
{
	CSimpleIniA ini;
	ini.SetUnicode();

	std::error_code ec;
	std::filesystem::create_directories(kUserSettingsPath.parent_path(), ec);
	if (ec) {
		logger::warn("[HDR] Failed to create settings directory {}: {}", kUserSettingsPath.parent_path().string(), ec.message());
		return;
	}

	if (std::filesystem::exists(kUserSettingsPath, ec)) {
		ini.LoadFile(kUserSettingsPath.string().c_str());
	}

	ini.SetLongValue(kSettingsSection, "iHDRMode", settings.hdrMode);
	ini.SetDoubleValue(kSettingsSection, "fPeakLuminance", settings.peakLuminance);
	ini.SetDoubleValue(kSettingsSection, "fPaperWhiteLuminance", settings.paperWhiteLuminance);
	ini.SetDoubleValue(kSettingsSection, "fScRGBReferenceLuminance", settings.scRGBReferenceLuminance);
	ini.SetBoolValue(kSettingsSection, "bCalibrationActive", settings.calibrationActive);

	if (ini.SaveFile(kUserSettingsPath.string().c_str()) < 0) {
		logger::warn("[HDR] Failed to save settings to {}", kUserSettingsPath.string());
		return;
	}

	logger::info("[HDR] Saved calibration settings to {}", kUserSettingsPath.string());
}
