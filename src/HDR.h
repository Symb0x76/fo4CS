#pragma once

enum class HDRMode
{
	kDisabled = 0,
	kScRGB = 1,
	kHDR10 = 2
};

struct HDRSettings
{
	int hdrMode = 0;
	float peakLuminance = 1000.0f;
	float paperWhiteLuminance = 200.0f;
	float scRGBReferenceLuminance = 80.0f;
	bool calibrationActive = false;

	bool IsEnabled() const;
	HDRMode GetMode() const;
};

HDRSettings LoadHDRSettingsFromINI();
HDRSettings ReloadCalibrationSettings();
void SaveHDRSettingsToINI(const HDRSettings& settings);
