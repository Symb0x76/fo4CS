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

		const auto loggerName = std::string(Plugin::NAME);
		spdlog::drop(loggerName);

		auto log = std::make_shared<spdlog::logger>(loggerName, std::move(sink));
		log->set_level(level);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(log);
		spdlog::set_pattern("%v"s);
	}

#if defined(FALLOUT_POST_NG)
	constexpr void PopulateVersionData(F4SE::PluginVersionData& data)
	{
		data.PluginVersion(Plugin::VERSION);
		data.PluginName(Plugin::NAME.data());
		data.AuthorName("");
		data.UsesAddressLibrary(true);
		data.UsesSigScanning(false);
		data.IsLayoutDependent(true);
		data.HasNoStructUse(false);
#if defined(FALLOUT_POST_AE)
		data.CompatibleVersions({
			F4SE::RUNTIME_1_10_984,
			F4SE::RUNTIME_1_11_137,
			F4SE::RUNTIME_1_11_159,
			F4SE::RUNTIME_1_11_169,
			F4SE::RUNTIME_1_11_191,
		});
#else
		data.CompatibleVersions({ F4SE::RUNTIME_LATEST });
#endif
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
		wchar_t waitForDebugger[8]{};
		if (GetEnvironmentVariableW(L"FO4CS_WAIT_FOR_DEBUGGER", waitForDebugger, static_cast<DWORD>(std::size(waitForDebugger))) == 0 ||
			wcscmp(waitForDebugger, L"1") != 0) {
			return;
		}

#	if defined(FALLOUT_POST_NG)
		while (!REX::W32::IsDebuggerPresent()) {}
#	else
		while (!IsDebuggerPresent()) {}
#endif
	}
}
