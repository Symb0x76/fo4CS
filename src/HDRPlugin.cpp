#include "PluginCommon.h"

#include "DX11Hooks.h"
#include "HDR.h"

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
			IsPluginAvailable(a_f4se, "FrameGen", L"FrameGen.dll") ||
			IsPluginAvailable(a_f4se, "Reflex", L"Reflex.dll");
	}
}

// Panel callbacks for Overlay.dll registration
namespace HDROverlay
{
	int RenderPanel(void* userData)
	{
		auto* hdr = static_cast<HDRSettings*>(userData);
		int changed = 0;

		if (ImGui::CollapsingHeader("HDR")) {
			const char* hdrModes[] = { "Disabled", "scRGB (HDR)", "HDR10 (HDR)" };
			if (ImGui::Combo("Mode", &hdr->hdrMode, hdrModes, IM_ARRAYSIZE(hdrModes))) {
				if (hdr->hdrMode == 2 && hdr->peakLuminance < 400.0f) hdr->peakLuminance = 1000.0f;
				changed = 1;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::TextWrapped("scRGB: HDR via linear FP16 framebuffer. Recommended for monitors with native scRGB support.");
				ImGui::TextWrapped("HDR10: Standard PQ (ST 2084) pipeline. Recommended for most HDR TVs and monitors.");
				ImGui::EndTooltip();
			}

			if (hdr->hdrMode > 0) {
				changed |= ImGui::SliderFloat("Peak Luminance (nits)", &hdr->peakLuminance, 80.0f, 10000.0f, "%.0f") ? 1 : 0;
				changed |= ImGui::SliderFloat("Paper White (nits)", &hdr->paperWhiteLuminance, 20.0f, 1000.0f, "%.0f") ? 1 : 0;
				if (hdr->hdrMode == 1) {
					changed |= ImGui::SliderFloat("scRGB Reference (nits)", &hdr->scRGBReferenceLuminance, 10.0f, 500.0f, "%.0f") ? 1 : 0;
				}
			}
		}
		return changed;
	}

	void SavePanel(void* userData)
	{
		auto* hdr = static_cast<HDRSettings*>(userData);
		SaveHDRSettingsToINI(*hdr);
	}

	void TryRegister()
	{
		HMODULE overlay = GetModuleHandleW(L"Overlay.dll");
		if (!overlay) return;

		auto registerFn = reinterpret_cast<decltype(&Overlay_RegisterPanel)>(
			GetProcAddress(overlay, "Overlay_RegisterPanel"));
		if (!registerFn) return;

		static OverlayPanelCallbacks cbs;
		cbs.render = RenderPanel;
		cbs.save = SavePanel;
		cbs.userData = &DX12SwapChain::GetSingleton()->hdrSettings;
		registerFn("HDR", kOverlayCategory_Rendering, &cbs);
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

	const auto hdrSettings = LoadHDRSettingsFromINI();
	if (!hdrSettings.IsEnabled()) {
		logger::info("[HDR] Disabled by settings; D3D12 proxy hooks not installed");
		HDROverlay::TryRegister();
		return true;
	}

	if (HasExternalProxyOwner(a_f4se)) {
		logger::info("[HDR] Upscaler/FrameGen/Reflex plugin detected, leaving D3D12 proxy ownership to that plugin");
		HDROverlay::TryRegister();
		return true;
	}

	logger::info("[HDR] Installing D3D12 proxy hooks");
	DX11Hooks::Install();

	HDROverlay::TryRegister();
	return true;
}
