#include "PluginCommon.h"

#include "DX11Hooks.h"
#include "Upscaler.h"

#include <array>
#include <filesystem>

namespace
{
	struct F4SEInterfaceLayout
	{
		std::uint32_t f4seVersion;
		std::uint32_t runtimeVersion;
		std::uint32_t editorVersion;
		std::uint32_t isEditor;
		void*(F4SEAPI* QueryInterface)(std::uint32_t);
		std::uint32_t(F4SEAPI* GetPluginHandle)();
		std::uint32_t(F4SEAPI* GetReleaseIndex)();
		const void*(F4SEAPI* GetPluginInfo)(const char*);
	};

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

	bool IsUpscalerPluginAvailable(const F4SE::LoadInterface* a_f4se)
	{
		const auto f4se = reinterpret_cast<const F4SEInterfaceLayout*>(a_f4se);
		const bool registered =
			f4se &&
			f4se->GetPluginInfo &&
			(f4se->GetPluginInfo("Upscaler") || f4se->GetPluginInfo("Upscaler.dll"));
		if (registered || GetModuleHandleW(L"Upscaler.dll") != nullptr)
			return true;

		const auto pluginDir = GetCurrentPluginDirectory();
		if (pluginDir.empty())
			return false;

		std::error_code ec;
		return std::filesystem::exists(pluginDir.parent_path() / L"Upscaler" / L"Upscaler.dll", ec);
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

	const bool upscalerPluginAvailable = IsUpscalerPluginAvailable(a_f4se);
	if (upscalerPluginAvailable) {
		auto upscaling = Upscaling::GetSingleton();
		upscaling->LoadFrameGenerationSettings();
		logger::info("[Settings] FrameGen(enabled={}, limiter={}), Debug(enabled={}, streamlineLogLevel={}, frames={})",
			upscaling->settings.frameGenerationMode,
			upscaling->settings.frameLimitMode,
			upscaling->settings.debugLogging,
			upscaling->settings.streamlineLogLevel,
			upscaling->settings.debugFrameLogCount);
		logger::info("[FrameGen] Upscaler plugin available, leaving DX hooks to Upscaler");
	} else {
		Upscaling::GetSingleton()->LoadSettings();
		logger::info("[FrameGen] Upscaler plugin not available, installing FrameGen DX hooks");
		DX11Hooks::Install();
	}
	return true;
}
