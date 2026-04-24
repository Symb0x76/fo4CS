#pragma once

#include "Plugin.h"

#include <ShlObj_core.h>

namespace fo4cs
{
	inline std::optional<std::filesystem::path> GetLogDirectory()
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

	inline void InitializeLog()
	{
#ifndef NDEBUG
		auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
		auto path = GetLogDirectory();
		if (!path) {
			stl::report_and_fail("Failed to find standard logging directory"sv);
		}

		*path /= std::format("{}.log"sv, Plugin::NAME);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
		const auto level = spdlog::level::trace;
#else
		const auto level = spdlog::level::info;
#endif

		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(level);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("%v"s);
	}

#if defined(FALLOUT_POST_NG)
	inline void PopulateVersionData(F4SE::PluginVersionData& data)
	{
		data.PluginVersion(Plugin::VERSION);
		data.PluginName(Plugin::NAME.data());
		data.AuthorName("");
		data.UsesAddressLibrary(true);
		data.UsesSigScanning(false);
		data.IsLayoutDependent(true);
		data.HasNoStructUse(false);
		data.CompatibleVersions({ F4SE::RUNTIME_LATEST });
	}
#else
	inline void PopulatePluginInfo(F4SE::PluginInfo* a_info)
	{
		a_info->name = Plugin::NAME.data();
		a_info->infoVersion = F4SE::PluginInfo::kVersion;
		a_info->version = 0;
	}
#endif

	inline void WaitForDebuggerIfNeeded()
	{
#ifndef NDEBUG
#	if defined(FALLOUT_POST_NG)
		while (!REX::W32::IsDebuggerPresent()) {}
#	else
		while (!IsDebuggerPresent()) {}
#	endif
#endif
	}
}
