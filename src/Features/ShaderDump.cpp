#include "Features/ShaderDump.h"

#include "Core/ShaderCache.h"

#include "SimpleIni.h"

#include <filesystem>

namespace
{
	constexpr auto kSection = "Settings";
	constexpr auto kDumpAllShadersKey = "bDumpAllShaders";

	const std::filesystem::path kDefaultSettingsPath{ "Data\\MCM\\Config\\CommunityShaders\\settings.ini" };
	const std::filesystem::path kUserSettingsPath{ "Data\\MCM\\Settings\\CommunityShaders.ini" };

	bool LoadIniIfExists(CSimpleIniA& a_ini, const std::filesystem::path& a_path, std::string_view a_label)
	{
		std::error_code ec;
		if (!std::filesystem::exists(a_path, ec)) {
			return false;
		}

		const auto result = a_ini.LoadFile(a_path.string().c_str());
		if (result < 0) {
			logger::warn("[CommunityShaders] Failed to load {} settings from {}", a_label, a_path.string());
			return false;
		}

		logger::info("[CommunityShaders] Loaded {} settings from {}", a_label, a_path.string());
		return true;
	}
}

std::pair<std::string, std::vector<std::string>> ShaderDump::GetFeatureSummary()
{
	return {
		"Captures original D3D11 shader bytecode for building runtime-specific ShaderDB data.",
		{
			"Hooks D3D11 shader creation through the Phase 0 foundation",
			"Writes PreNG dumps under Data/F4SE/Plugins/fo4CS/ShaderDump/PreNG",
			"Can be enabled with MCM ini or FO4CS_DUMP_SHADERS"
		}
	};
}

void ShaderDump::LoadSettings()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	LoadIniIfExists(ini, kDefaultSettingsPath, "default CommunityShaders");
	LoadIniIfExists(ini, kUserSettingsPath, "user CommunityShaders");

	dumpAllShaders = ini.GetBoolValue(kSection, kDumpAllShadersKey, dumpAllShaders);
	if (GetEnvironmentVariableW(L"FO4CS_DUMP_SHADERS", nullptr, 0) > 0) {
		dumpAllShaders = true;
	}
}

void ShaderDump::SaveSettings()
{
	CSimpleIniA ini;
	ini.SetUnicode();
	ini.SetBoolValue(kSection, kDumpAllShadersKey, dumpAllShaders);

	std::error_code ec;
	std::filesystem::create_directories(kUserSettingsPath.parent_path(), ec);
	if (ec) {
		logger::warn("[CommunityShaders] Failed to create settings directory {}: {}", kUserSettingsPath.parent_path().string(), ec.message());
		return;
	}

	if (ini.SaveFile(kUserSettingsPath.string().c_str()) < 0) {
		logger::warn("[CommunityShaders] Failed to save settings to {}", kUserSettingsPath.string());
	}
}

void ShaderDump::Load()
{
	CommunityShaders::ShaderCache::GetSingleton()->SetDumpAllShaders(dumpAllShaders);
	logger::info("[CommunityShaders] ShaderDB dump mode {}", dumpAllShaders ? "enabled" : "disabled");
}
