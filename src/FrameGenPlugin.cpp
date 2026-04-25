#include "PluginCommon.h"

#include "DX11Hooks.h"
#include "Upscaler.h"

#include <array>
#include <filesystem>

namespace
{
	std::filesystem::path GetCurrentPluginDirectory()
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetCurrentPluginDirectory),
				&module)) {
			return {};
		}

		std::array<wchar_t, 4096> buffer{};
		const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size()) {
			return {};
		}

		return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
	}

	bool IsUpscalerPluginInstalled()
	{
		const auto pluginDir = GetCurrentPluginDirectory();
		if (pluginDir.empty()) {
			return GetModuleHandleW(L"Upscaler.dll") != nullptr;
		}

		std::error_code ec;
		return std::filesystem::exists(pluginDir / L"Upscaler.dll", ec) || GetModuleHandleW(L"Upscaler.dll") != nullptr;
	}
}


#if defined(FALLOUT_POST_NG)
extern "C" DLLEXPORT constinit F4SE::PluginVersionData F4SEPlugin_Version = []() consteval {
	F4SE::PluginVersionData data{};
	fo4cs::PopulateVersionData(data);
	return data;
}();
#else
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface*, F4SE::PluginInfo* a_info)
{
	fo4cs::PopulatePluginInfo(a_info);
	return true;
}
#endif

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);
	fo4cs::WaitForDebuggerIfNeeded();
	fo4cs::InitializeLog();
	if (IsUpscalerPluginInstalled()) {
		logger::info("Upscaler.dll detected, leaving DX hooks to Upscaler");
	} else {
		DX11Hooks::Install();
	}
	return true;
}
