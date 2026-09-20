#pragma once

#include <filesystem>
#include <optional>

#include <ShlObj_core.h>

namespace fo4cs::diagnostics
{
	// %USERPROFILE%\Documents\My Games\Fallout4\F4SE — the directory F4SE and every
	// fo4CS plugin write their logs to (<Plugin>.log, hang trace). Created on demand.
	inline std::optional<std::filesystem::path> GetF4SELogDirectory()
	{
		PWSTR documentsPath = nullptr;
		if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documentsPath))) {
			return std::nullopt;
		}

		std::filesystem::path path{ documentsPath };
		CoTaskMemFree(documentsPath);

		path /= "My Games/Fallout4/F4SE";
		std::error_code ec;
		std::filesystem::create_directories(path, ec);
		if (ec) {
			return std::nullopt;
		}

		return path;
	}
}
