#include "PluginCommon.h"

#include "DX11Hooks.h"
#include "Upscaler.h"

#include <OverlayAPI.h>
#include <SimpleIni.h>
#include <imgui.h>

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

	bool IsPluginAvailable(const F4SE::LoadInterface* a_f4se, const char* pluginName, const wchar_t* dllName)
	{
		const auto f4se = reinterpret_cast<const F4SEInterfaceLayout*>(a_f4se);
		if (f4se && f4se->GetPluginInfo && (f4se->GetPluginInfo(pluginName) || f4se->GetPluginInfo(std::format("{}.dll", pluginName).c_str()))) {
			return true;
		}

		if (GetModuleHandleW(dllName) != nullptr) {
			return true;
		}

		const auto pluginDir = GetCurrentPluginDirectory();
		if (pluginDir.empty()) {
			return false;
		}

		std::error_code ec;
		std::wstring subDir(dllName);
			auto dot = subDir.rfind(L'.');
			if (dot != std::wstring::npos) subDir.resize(dot);
			return std::filesystem::exists(pluginDir.parent_path() / subDir / dllName, ec);
	}

	bool HasExternalProxyOwner(const F4SE::LoadInterface* a_f4se)
	{
		return IsPluginAvailable(a_f4se, "Upscaler", L"Upscaler.dll") ||
			IsPluginAvailable(a_f4se, "FrameGen", L"FrameGen.dll");
	}
}

// Panel callbacks for Overlay.dll registration
namespace ReflexOverlay
{
	int RenderPanel(void* userData)
	{
		auto& s = *static_cast<Upscaling::Settings*>(userData);
		int changed = 0;

		if (ImGui::CollapsingHeader("Reflex")) {
			const char* reflexModes[] = { "Off", "Low Latency", "Low Latency + Boost" };
			changed |= ImGui::Combo("Mode", &s.reflexMode, reflexModes, IM_ARRAYSIZE(reflexModes)) ? 1 : 0;
			changed |= ImGui::Checkbox("Reflex Sleep Mode", &s.reflexSleepMode) ? 1 : 0;
		}
		return changed;
	}

	void SavePanel(void* userData)
	{
		auto& s = *static_cast<Upscaling::Settings*>(userData);
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.SetValue("Settings", "iReflexMode", std::to_string(s.reflexMode).c_str());
		ini.SetValue("Settings", "bReflexSleepMode", s.reflexSleepMode ? "true" : "false");
		ini.SaveFile("Data\\F4SE\\Plugins\\Reflex\\Reflex.ini");
	}

	void TryRegister()
	{
		HMODULE overlay = GetModuleHandleW(nullptr);
		if (!overlay) overlay = GetModuleHandleW(L"Overlay.dll");
		if (!overlay) return;

		auto registerFn = reinterpret_cast<decltype(&Overlay_RegisterPanel)>(
			GetProcAddress(overlay, "Overlay_RegisterPanel"));
		if (!registerFn) return;

		static OverlayPanelCallbacks cbs;
		cbs.render = RenderPanel;
		cbs.save = SavePanel;
		cbs.userData = &Upscaling::GetSingleton()->settings;
		registerFn("Reflex", kOverlayCategory_Latency, &cbs);
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

	auto upscaling = Upscaling::GetSingleton();
	upscaling->pluginMode = Upscaling::PluginMode::kReflex;
	upscaling->LoadReflexSettings();

	if (!upscaling->UsesReflex()) {
		logger::info("[Reflex] Disabled by settings; D3D12 proxy hooks not installed");
		ReflexOverlay::TryRegister();
		return true;
	}

	if (HasExternalProxyOwner(a_f4se)) {
		logger::info("[Reflex] Upscaler/FrameGen plugin detected, leaving D3D12 proxy ownership to that plugin");
		ReflexOverlay::TryRegister();
		return true;
	}

	logger::info("[Reflex] Installing D3D12 proxy hooks");
	DX11Hooks::Install();

	ReflexOverlay::TryRegister();
	return true;
}
