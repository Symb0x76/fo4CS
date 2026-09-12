#include "Upscaling/Streamline.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Render/DX12SwapChain.h"
#include "Upscaling/Upscaler.h"
#include "Upscaling/StreamlineInternal.h"
#include "Diagnostics/LogEvents.h"
#include "Platform/ModulePaths.h"

using fo4cs::streamline::EnumToString;
using fo4cs::streamline::GetConfiguredReflexMode;
using fo4cs::streamline::ResultToString;
using fo4cs::streamline::ShouldTraceStreamlineFrame;
using fo4cs::diagnostics::Event;
using fo4cs::diagnostics::LogEvent;
using fo4cs::platform::GetCurrentModuleDirectory;
using fo4cs::platform::GetModuleDirectory;

namespace
{
	std::string WideToUtf8(const std::wstring& value)
	{
		if (value.empty()) {
			return {};
		}

		const auto size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		std::string result(size, '\0');
		WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
		return result;
	}

	std::string PathToUtf8(const std::filesystem::path& path)
	{
		return WideToUtf8(path.wstring());
	}

	std::string Trim(std::string value)
	{
		const auto begin = value.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos) {
			return {};
		}

		const auto end = value.find_last_not_of(" \t\r\n");
		return value.substr(begin, end - begin + 1);
	}

	std::string CleanStreamlineSDKMessage(const char* msg)
	{
		if (!msg) {
			return {};
		}

		std::string raw = Trim(msg);
		std::string_view view(raw);
		while (!view.empty() && view.front() == '[') {
			const auto close = view.find(']');
			if (close == std::string_view::npos) {
				break;
			}

			view.remove_prefix(close + 1);
			while (!view.empty() && std::isspace(static_cast<unsigned char>(view.front()))) {
				view.remove_prefix(1);
			}
		}

		return Trim(std::string(view));
	}

	bool Contains(std::string_view value, std::string_view needle)
	{
		return value.find(needle) != std::string_view::npos;
	}

	void StreamlineLogCallback(sl::LogType type, const char* msg)
	{
		const auto clean = CleanStreamlineSDKMessage(msg);
		const auto text = clean.empty() ? std::string(msg ? msg : "") : clean;
		const bool debugLogging = Upscaling::GetSingleton()->settings.debugLogging;

		if (Contains(text, "Ignoring plugin") && Contains(text, "was not requested by the host")) {
			if (debugLogging) {
				logger::debug("[Streamline][SDK] {}", text);
			}
			return;
		}

		if (Contains(text, "nvngx_update.exe") || Contains(text, "ota.cpp")) {
			static bool loggedOTA = false;
			if (!loggedOTA) {
				logger::warn("[Streamline][SDK] NVIDIA OTA updater could not start; continuing without OTA bootstrap");
				loggedOTA = true;
			} else if (debugLogging) {
				logger::debug("[Streamline][SDK] {}", text);
			}
			return;
		}

		switch (type) {
		case sl::LogType::eError:
			logger::error("[Streamline][SDK] {}", text);
			break;
		case sl::LogType::eWarn:
			logger::warn("[Streamline][SDK] {}", text);
			break;
		default:
			if (debugLogging) {
				logger::debug("[Streamline][SDK] {}", text);
			}
			break;
		}
	}

	std::string GetLastErrorMessage(DWORD error)
	{
		LPWSTR messageBuffer = nullptr;
		const auto length = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			reinterpret_cast<LPWSTR>(&messageBuffer),
			0,
			nullptr);

		if (length == 0 || !messageBuffer) {
			return "Unknown error";
		}

		std::wstring message(messageBuffer, length);
		LocalFree(messageBuffer);

		while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
			message.pop_back();
		}

		return WideToUtf8(message);
	}

	std::vector<std::filesystem::path> GetStreamlineSearchDirectories()
	{
		std::vector<std::filesystem::path> directories;
		auto addUnique = [&](const std::filesystem::path& path) {
			if (path.empty()) {
				return;
			}

			for (const auto& existing : directories) {
				if (existing == path) {
					return;
				}
			}

			directories.push_back(path);
		};

		if (const auto pluginDir = GetCurrentModuleDirectory(); !pluginDir.empty()) {
			addUnique(pluginDir / L"Streamline");
		}

		if (const auto exeDir = GetModuleDirectory(nullptr); !exeDir.empty()) {
			addUnique(exeDir / L"Data" / L"F4SE" / L"Plugins" / L"Streamline");
		}

		return directories;
	}

	std::filesystem::path FindStreamlineInterposer(const std::vector<std::filesystem::path>& directories)
	{
		std::error_code ec;
		for (const auto& directory : directories) {
			const auto candidate = directory / L"sl.interposer.dll";
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			ec.clear();
		}

		return {};
	}

	std::string JoinSearchDirectories(const std::vector<std::filesystem::path>& directories)
	{
		std::string joined;
		for (const auto& directory : directories) {
			if (!joined.empty()) {
				joined += "; ";
			}
			joined += PathToUtf8(directory);
		}
		return joined;
	}
}

void Streamline::LoadAndInit()
{
	auto upscaling = Upscaling::GetSingleton();

	const auto searchDirectories = GetStreamlineSearchDirectories();
	const auto interposerPath = FindStreamlineInterposer(searchDirectories);
	if (interposerPath.empty()) {
		logger::warn("[Streamline] Could not locate sl.interposer.dll. Checked: {}", JoinSearchDirectories(searchDirectories));
		return;
	}

	interposer = LoadLibraryExW(interposerPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
	if (!interposer) {
		const auto error = GetLastError();
		logger::warn("[Streamline] Failed to load {} (error {}: {})", PathToUtf8(interposerPath), error, GetLastErrorMessage(error));
		return;
	}

	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slShutdown = (PFun_slShutdown*)GetProcAddress(interposer, "slShutdown");
	slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
	slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
	slSetFeatureLoaded = (PFun_slSetFeatureLoaded*)GetProcAddress(interposer, "slSetFeatureLoaded");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slAllocateResources = (PFun_slAllocateResources*)GetProcAddress(interposer, "slAllocateResources");
	slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
	slSetTag = (PFun_slSetTag2*)GetProcAddress(interposer, "slSetTag");
	slSetTagForFrame = (PFun_slSetTagForFrame*)GetProcAddress(interposer, "slSetTagForFrame");
	slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
	slGetFeatureVersion = (PFun_slGetFeatureVersion*)GetProcAddress(interposer, "slGetFeatureVersion");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetNativeInterface = (PFun_slGetNativeInterface*)GetProcAddress(interposer, "slGetNativeInterface");

	if (!slInit) {
		LogEvent(Event::Error, "[Streamline] Failed to get slInit");
		return;
	}

	sl::Preferences pref{};
	std::vector<sl::Feature> featuresToLoad;
	const auto addFeature = [&](sl::Feature feature) {
		if (std::find(featuresToLoad.begin(), featuresToLoad.end(), feature) == featuresToLoad.end()) {
			featuresToLoad.push_back(feature);
		}
	};
	if (upscaling->UsesDLSSUpscaling())
		addFeature(sl::kFeatureDLSS);
	if (upscaling->UsesDLSSFrameGeneration()) {
		addFeature(sl::kFeatureDLSS_G);
		addFeature(sl::kFeatureReflex);
		addFeature(sl::kFeaturePCL);
	}
	if (upscaling->UsesReflex()) {
		addFeature(sl::kFeatureReflex);
		addFeature(sl::kFeaturePCL);
	}
	if (featuresToLoad.empty()) {
		logger::info("[Streamline] Runtime not required for current settings");
		return;
	}
	pref.featuresToLoad = featuresToLoad.data();
	pref.numFeaturesToLoad = static_cast<uint32_t>(featuresToLoad.size());
	switch (upscaling->settings.streamlineLogLevel) {
	case 2:
		pref.logLevel = sl::LogLevel::eVerbose;
		break;
	case 1:
		pref.logLevel = sl::LogLevel::eDefault;
		break;
	default:
		pref.logLevel = sl::LogLevel::eOff;
		break;
	}

	pref.showConsole = false;
	pref.logMessageCallback = StreamlineLogCallback;

	const auto pluginDirectory = interposerPath.parent_path().wstring();
	const wchar_t* pluginPaths[1] = { pluginDirectory.c_str() };
	pref.pathsToPlugins = pluginPaths;
	pref.numPathsToPlugins = 1;

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "5298f3a2-a84c-485f-aa4b-4baeb3d01b99";
	pref.renderAPI = sl::RenderAPI::eD3D12;
	pref.flags = sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	if (slInit(pref, sl::kSDKVersion) != sl::Result::eOk) {
		LogEvent(Event::Error, "[Streamline] slInit failed");
		return;
	}

	initialized = true;
	logger::info("[Streamline] Initialized (features={}, logLevel={}, pluginPath={})", featuresToLoad.size(), upscaling->settings.streamlineLogLevel, PathToUtf8(interposerPath.parent_path()));
}

void Streamline::PostDevice(ID3D12Device* device, IDXGIAdapter* adapter)
{
	// Streamline is loaded here, not from DX11Hooks::Install(). LoadAndInit() calls
	// LoadLibraryExW on sl.interposer.dll, and the interposer hooks DXGI/D3D as it
	// loads. Install() runs from Feature::Load(), i.e. inside F4SEPlugin_Load with the
	// Windows loader lock held, and on PreNG loading the interposer there deadlocks:
	// the process stays alive with no window, CommunityShaders.log stops mid-load and
	// f4se.log never reaches "loaded correctly". PostDevice runs well after the loader
	// lock is released and is shared by every path that brings up the proxy, so one
	// call site covers them all. Must stay above the `initialized` guard below.
	{
		static bool s_initAttempted = false;
		auto* upscaling = Upscaling::GetSingleton();
		const bool wantsStreamline = upscaling->UsesDLSSUpscaling() ||
		                             upscaling->UsesDLSSFrameGeneration() ||
		                             upscaling->UsesReflex();
		if (!s_initAttempted && !initialized && wantsStreamline) {
			s_initAttempted = true;
			LoadAndInit();
		}
	}

	if (!initialized)
		return;

	if (slSetD3DDevice(device) != sl::Result::eOk) {
		LogEvent(Event::Error, "[Streamline] slSetD3DDevice failed");
		initialized = false;
		return;
	}

	DXGI_ADAPTER_DESC adapterDesc{};
	adapter->GetDesc(&adapterDesc);
	logger::debug(
		"[Streamline] Checking D3D12 features on '{}' (vendor=0x{:X}, device=0x{:X})",
		WideToUtf8(adapterDesc.Description),
		adapterDesc.VendorId,
		adapterDesc.DeviceId);

	sl::AdapterInfo adapterInfo{};
	adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
	adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

	const auto checkFeatureAvailability = [&](sl::Feature feature, const char* name, bool& outAvailable) {
		outAvailable = false;
		bool loaded = false;
		const auto loadedResult = slIsFeatureLoaded ? slIsFeatureLoaded(feature, loaded) : sl::Result::eErrorMissingOrInvalidAPI;
		logger::debug("[Streamline] {} loaded query: {} (loaded={})", name, ResultToString(loadedResult), loaded);

		if (loadedResult == sl::Result::eOk && loaded) {
			const auto supportResult = slIsFeatureSupported ? slIsFeatureSupported(feature, adapterInfo) : sl::Result::eErrorMissingOrInvalidAPI;
			outAvailable = supportResult == sl::Result::eOk;
			logger::debug("[Streamline] {} support query: {}", name, ResultToString(supportResult));
			return;
		}

		if (slGetFeatureRequirements) {
			sl::FeatureRequirements requirements{};
			const auto requirementsResult = slGetFeatureRequirements(feature, requirements);
			logger::debug("[Streamline] {} requirements query: {}", name, ResultToString(requirementsResult));
		}
	};

	checkFeatureAvailability(sl::kFeatureDLSS, "DLSS", featureDLSS);
	checkFeatureAvailability(sl::kFeatureDLSS_G, "DLSS-G", featureDLSSG);
	checkFeatureAvailability(sl::kFeatureReflex, "Reflex", featureReflex);
	checkFeatureAvailability(sl::kFeaturePCL, "PCL", featurePCL);

	const auto bindFeatureFn = [&](sl::Feature feature, const char* functionName, void*& fn) {
		fn = nullptr;
		if (!slGetFeatureFunction) {
			logger::warn("[Streamline] {} bind skipped: slGetFeatureFunction is unavailable", functionName);
			return false;
		}

		const auto result = slGetFeatureFunction(feature, functionName, fn);
		if (result != sl::Result::eOk || !fn) {
			logger::warn("[Streamline] {} bind failed: {}", functionName, ResultToString(result));
			fn = nullptr;
			return false;
		}

		if (Upscaling::GetSingleton()->settings.debugLogging) {
			logger::debug("[Streamline] {} bound", functionName);
		}
		return true;
	};

	if (featureDLSS) {
		bool dlssBound = true;
		dlssBound &= bindFeatureFn(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		dlssBound &= bindFeatureFn(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		dlssBound &= bindFeatureFn(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
		featureDLSS = dlssBound && slDLSSSetOptions && slEvaluateFeature && slSetConstants && slGetNewFrameToken;
		logger::info("[Streamline] DLSS {}", featureDLSS ? "ready" : "unavailable after binding");
	} else {
		logger::info("[Streamline] DLSS unavailable");
	}

	if (featureReflex) {
		bool reflexBound = true;
		reflexBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexGetState", (void*&)slReflexGetState);
		reflexBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSleep", (void*&)slReflexSleep);
		reflexBound &= bindFeatureFn(sl::kFeatureReflex, "slReflexSetOptions", (void*&)slReflexSetOptions);
		featureReflex = reflexBound && slReflexSetOptions;
		if (featureReflex) {
			logger::info("[Streamline] Reflex ready");
			if (Upscaling::GetSingleton()->UsesReflex()) {
				ConfigureReflex(GetConfiguredReflexMode(), "startup");
			}
		} else {
			logger::warn("[Streamline] Reflex unavailable after binding; DLSS-G status may reject frame generation");
		}
	}

	if (featurePCL) {
		bool pclBound = true;
		pclBound &= bindFeatureFn(sl::kFeaturePCL, "slPCLSetMarker", (void*&)slPCLSetMarker);
		featurePCL = pclBound && slPCLSetMarker;
		logger::info("[Streamline] PCL {}", featurePCL ? "ready" : "unavailable after binding");
	} else {
		logger::info("[Streamline] PCL unavailable");
	}

	if (featureDLSSG) {
		bool dlssgBound = true;
		dlssgBound &= bindFeatureFn(sl::kFeatureDLSS_G, "slDLSSGGetState", (void*&)slDLSSGGetState);
		dlssgBound &= bindFeatureFn(sl::kFeatureDLSS_G, "slDLSSGSetOptions", (void*&)slDLSSGSetOptions);
		featureDLSSG = dlssgBound && slDLSSGSetOptions && slSetTagForFrame && slGetNewFrameToken;
		if (featureDLSSG) {
			ConfigureReflexForDLSSG();
		}
		logger::info("[Streamline] DLSS-G {}", featureDLSSG ? "ready" : "unavailable after binding");
	} else {
		logger::info("[Streamline] DLSS-G unavailable");
	}
}

bool Streamline::UpgradeD3D12DeviceForDLSSG(ID3D12Device** device)
{
	if (!Upscaling::GetSingleton()->UsesDLSSFrameGeneration() || !initialized || dlssgDisabledAfterError) {
		return false;
	}

	if (!slUpgradeInterface || !device || !*device) {
		DisableDLSSGAfterError("slUpgradeInterface or D3D12 device is unavailable");
		return false;
	}

	void* upgradedInterface = *device;
	const auto result = slUpgradeInterface(&upgradedInterface);
	if (result != sl::Result::eOk || !upgradedInterface) {
		LogEvent(Event::Error, "[Streamline] slUpgradeInterface failed for D3D12 device: {}", ResultToString(result));
		DisableDLSSGAfterError("slUpgradeInterface failed for D3D12 device");
		return false;
	}

	const bool upgraded = upgradedInterface != *device;
	*device = static_cast<ID3D12Device*>(upgradedInterface);
	logger::info(
		"[Streamline] D3D12 device {} for manual-hooked DLSS-G command queue path",
		upgraded ? "upgraded" : "kept native");
	return upgraded;
}

void Streamline::LogD3D12CommandQueueProxyState(ID3D12CommandQueue* commandQueue)
{
	if (!Upscaling::GetSingleton()->UsesDLSSFrameGeneration() || !initialized || !slGetNativeInterface || !commandQueue) {
		return;
	}

	void* nativeInterface = nullptr;
	const auto result = slGetNativeInterface(commandQueue, &nativeInterface);
	if (result != sl::Result::eOk || !nativeInterface) {
		logger::warn("[Streamline] slGetNativeInterface for D3D12 command queue returned {}", ResultToString(result));
		return;
	}

	const bool proxied = nativeInterface != commandQueue;
	static_cast<IUnknown*>(nativeInterface)->Release();
	logger::info(
		"[Streamline] D3D12 command queue {}",
		proxied ? "is available behind Streamline proxy" : "is native");
}
