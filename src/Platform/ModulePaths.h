#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

// Module/path helpers shared by the plugin entry points and the runtime.
// Header-only on purpose: GetCurrentModuleDirectory() takes its own address, so
// each plugin DLL (which carries its own copy of the runtime objects) resolves to
// its own on-disk location.
namespace fo4cs::platform
{
	// Directory containing `module`; nullptr means the game executable.
	inline std::filesystem::path GetModuleDirectory(HMODULE module)
	{
		std::array<wchar_t, 4096> buffer{};
		const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size()) {
			return {};
		}

		return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
	}

	// Directory of the DLL this code is linked into (e.g. Data\F4SE\Plugins\Upscaler).
	inline std::filesystem::path GetCurrentModuleDirectory()
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetCurrentModuleDirectory),
				&module)) {
			return {};
		}

		return GetModuleDirectory(module);
	}

	// Memory layout of F4SE::LoadInterface as handed to F4SEPlugin_Load. Used to ask
	// F4SE whether a sibling plugin has registered without depending on the
	// CommonLibF4 wrapper for each runtime variant.
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

	// True when the sibling fo4CS plugin `pluginName` (e.g. "Upscaler") is known to
	// F4SE, already loaded in the process, or present on disk at
	// Data\F4SE\Plugins\<pluginName>\<pluginName>.dll next to this plugin's folder.
	// Load order between fo4CS DLLs is not guaranteed, hence the three checks.
	inline bool IsSiblingPluginAvailable(const F4SE::LoadInterface* a_f4se, const char* pluginName)
	{
		const std::string dllNameA = std::string(pluginName) + ".dll";
		const std::wstring pluginNameW(pluginName, pluginName + std::string_view(pluginName).size());
		const std::wstring dllNameW = pluginNameW + L".dll";

		const auto f4se = reinterpret_cast<const F4SEInterfaceLayout*>(a_f4se);
		if (f4se && f4se->GetPluginInfo && (f4se->GetPluginInfo(pluginName) || f4se->GetPluginInfo(dllNameA.c_str()))) {
			return true;
		}

		if (GetModuleHandleW(dllNameW.c_str()) != nullptr) {
			return true;
		}

		const auto pluginDir = GetCurrentModuleDirectory();
		if (pluginDir.empty()) {
			return false;
		}

		std::error_code ec;
		return std::filesystem::exists(pluginDir.parent_path() / pluginNameW / dllNameW, ec);
	}
}
