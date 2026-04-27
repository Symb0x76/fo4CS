#include "Streamline.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "DX12SwapChain.h"
#include "Upscaler.h"

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

	std::string ResultToString(sl::Result result)
	{
		if (const auto name = magic_enum::enum_name(result); !name.empty()) {
			return std::string(name);
		}

		return std::to_string(static_cast<int>(result));
	}

	sl::float4x4 IdentityMatrix()
	{
		return {
			sl::float4(1.0f, 0.0f, 0.0f, 0.0f),
			sl::float4(0.0f, 1.0f, 0.0f, 0.0f),
			sl::float4(0.0f, 0.0f, 1.0f, 0.0f),
			sl::float4(0.0f, 0.0f, 0.0f, 1.0f),
		};
	}

	template <class T>
	std::string EnumToString(T value)
	{
		if (const auto name = magic_enum::enum_name(value); !name.empty()) {
			return std::string(name);
		}

		return std::to_string(static_cast<int>(value));
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

	bool ShouldTraceStreamlineFrame(uint64_t frameID)
	{
		const auto upscaling = Upscaling::GetSingleton();
		const auto settings = upscaling->settings;
		const auto bootstrapFrames = std::min(settings.debugFrameLogCount, 12);
		return settings.debugLogging &&
			(upscaling->debugTraceCurrentPresent ||
			 frameID < static_cast<uint64_t>(bootstrapFrames));
	}

	sl::ReflexMode GetConfiguredReflexMode()
	{
		switch (Upscaling::GetSingleton()->settings.reflexMode) {
		case 2:
			return sl::ReflexMode::eLowLatencyWithBoost;
		case 1:
			return sl::ReflexMode::eLowLatency;
		default:
			return sl::ReflexMode::eOff;
		}
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

	std::filesystem::path GetModuleDirectory(HMODULE module)
	{
		std::array<wchar_t, 4096> buffer{};
		const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size()) {
			return {};
		}

		return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
	}

	std::filesystem::path GetCurrentPluginDirectory()
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&GetCurrentPluginDirectory),
				&module)) {
			return {};
		}

		return GetModuleDirectory(module);
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

		if (const auto pluginDir = GetCurrentPluginDirectory(); !pluginDir.empty()) {
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
		logger::error("[Streamline] Failed to get slInit");
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
		logger::error("[Streamline] slInit failed");
		return;
	}

	initialized = true;
	logger::info("[Streamline] Initialized (features={}, logLevel={}, pluginPath={})", featuresToLoad.size(), upscaling->settings.streamlineLogLevel, PathToUtf8(interposerPath.parent_path()));
}

void Streamline::PostDevice(ID3D12Device* device, IDXGIAdapter* adapter)
{
	if (!initialized)
		return;

	if (slSetD3DDevice(device) != sl::Result::eOk) {
		logger::error("[Streamline] slSetD3DDevice failed");
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
		logger::error("[Streamline] slUpgradeInterface failed for D3D12 device: {}", ResultToString(result));
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

bool Streamline::UpgradeDXGIFactoryForDLSSG(IDXGIFactory4** factory)
{
	if (!Upscaling::GetSingleton()->UsesDLSSFrameGeneration() || !initialized || dlssgDisabledAfterError) {
		return false;
	}

	if (!slUpgradeInterface || !factory || !*factory) {
		DisableDLSSGAfterError("slUpgradeInterface or DXGI factory is unavailable");
		return false;
	}

	void* upgradedInterface = *factory;
	const auto result = slUpgradeInterface(&upgradedInterface);
	if (result != sl::Result::eOk || !upgradedInterface) {
		logger::error("[Streamline] slUpgradeInterface failed for DXGI factory: {}", ResultToString(result));
		DisableDLSSGAfterError("slUpgradeInterface failed for DXGI factory");
		return false;
	}

	const bool upgraded = upgradedInterface != *factory;
	*factory = static_cast<IDXGIFactory4*>(upgradedInterface);
	logger::info(
		"[Streamline] DXGI factory {} for manual-hooked DLSS-G swap chain creation path",
		upgraded ? "upgraded" : "kept native");
	return upgraded;
}

bool Streamline::UpgradeSwapChainForDLSSG(IDXGISwapChain4** swapChain)
{
	if (!Upscaling::GetSingleton()->UsesDLSSFrameGeneration() || !initialized || !featureDLSSG || dlssgDisabledAfterError) {
		return false;
	}

	if (!slUpgradeInterface || !swapChain || !*swapChain) {
		DisableDLSSGAfterError("slUpgradeInterface or swap chain is unavailable");
		return false;
	}

	void* upgradedInterface = *swapChain;
	const auto result = slUpgradeInterface(&upgradedInterface);
	if (result != sl::Result::eOk || !upgradedInterface) {
		logger::error("[Streamline] slUpgradeInterface failed for D3D12 swap chain: {}", ResultToString(result));
		DisableDLSSGAfterError("slUpgradeInterface failed for D3D12 swap chain");
		return false;
	}

	*swapChain = static_cast<IDXGISwapChain4*>(upgradedInterface);
	logger::info("[Streamline] D3D12 swap chain upgraded for manual-hooked DLSS-G Present path");

	if (slGetNativeInterface) {
		void* nativeInterface = nullptr;
		const auto nativeResult = slGetNativeInterface(*swapChain, &nativeInterface);
		if (nativeResult == sl::Result::eOk && nativeInterface) {
			logger::debug("[Streamline] Native swap chain interface is available behind Streamline proxy");
			static_cast<IUnknown*>(nativeInterface)->Release();
		} else if (Upscaling::GetSingleton()->settings.debugLogging) {
			logger::debug("[Streamline] slGetNativeInterface for swap chain returned {}", ResultToString(nativeResult));
		}
	}

	return true;
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
		proxied ? "is available behind Streamline proxy" : "is native; DLSS-G Present path may not present generated frames");
}

bool Streamline::EnsureFrameToken(const char* caller)
{
	if (!initialized || !slGetNewFrameToken) {
		logger::error("[Streamline] {} cannot get frame token; Streamline is not fully initialized", caller);
		frameToken = nullptr;
		return false;
	}

	if (frameToken && frameTokenFrameID == frameID) {
		return true;
	}

	uint32_t fid = static_cast<uint32_t>(frameID);
	const auto result = slGetNewFrameToken(frameToken, &fid);
	if (result != sl::Result::eOk || !frameToken) {
		logger::error("[Streamline] {} failed to get frame token for frame {}: {}", caller, frameID, ResultToString(result));
		frameToken = nullptr;
		return false;
	}

	frameTokenFrameID = frameID;
	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug("[Streamline] {} frame token ready (frame={})", caller, frameID);
	}
	return true;
}

bool Streamline::ConfigureReflex(sl::ReflexMode mode, const char* reason)
{
	if (!featureReflex || !slReflexSetOptions) {
		static bool loggedMissingReflex = false;
		if (!loggedMissingReflex) {
			logger::warn("[Streamline] Reflex is unavailable; requested by {}", reason ? reason : "unknown");
			loggedMissingReflex = true;
		}
		return false;
	}

	sl::ReflexOptions options{};
	options.mode = mode;
	options.frameLimitUs = 0;
	options.useMarkersToOptimize = false;

	if (reflexOptionsValid && reflexConfiguredMode == options.mode) {
		return true;
	}

	const auto result = slReflexSetOptions(options);
	if (result != sl::Result::eOk) {
		logger::warn("[Streamline] slReflexSetOptions failed: {}", ResultToString(result));
		return false;
	}

	reflexOptionsValid = true;
	reflexConfiguredMode = options.mode;
	logger::info("[Streamline] Reflex mode={} reason={}", EnumToString(options.mode), reason ? reason : "unknown");
	return true;
}

void Streamline::ConfigureReflexForDLSSG()
{
	if (!featureDLSSG) {
		return;
	}

	if (!ConfigureReflex(sl::ReflexMode::eLowLatency, "DLSS-G")) {
		static bool loggedDLSSGMissingReflex = false;
		if (!loggedDLSSGMissingReflex) {
			logger::warn("[Streamline] Reflex is unavailable; DLSS-G may report eFailReflexNotDetectedAtRuntime");
			loggedDLSSGMissingReflex = true;
		}
	}
}

bool Streamline::SleepReflexFrame(const char* reason)
{
	auto upscaling = Upscaling::GetSingleton();
	if (!upscaling->UsesReflex() || (upscaling->pluginMode == Upscaling::PluginMode::kReflex && !upscaling->settings.reflexSleepMode)) {
		return false;
	}

	if (!initialized || !featureReflex || !slReflexSleep) {
		return false;
	}

	if (upscaling->pluginMode == Upscaling::PluginMode::kReflex && !ConfigureReflex(GetConfiguredReflexMode(), reason)) {
		return false;
	}

	if (!EnsureFrameToken(reason ? reason : "Reflex sleep")) {
		return false;
	}

	const auto result = slReflexSleep(*frameToken);
	if (result != sl::Result::eOk) {
		static bool loggedSleepFailure = false;
		if (!loggedSleepFailure || upscaling->settings.debugLogging) {
			logger::warn("[Streamline] slReflexSleep failed: {}", ResultToString(result));
			loggedSleepFailure = true;
		}
		return false;
	}

	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug("[Streamline] Reflex sleep completed (frame={}, reason={})", frameID, reason ? reason : "unknown");
	}
	return true;
}

bool Streamline::SetPCLMarker(sl::PCLMarker marker, const char* reason)
{
	if (!initialized || !featurePCL || !slPCLSetMarker) {
		return false;
	}

	if (!EnsureFrameToken(reason ? reason : "PCL marker")) {
		return false;
	}

	const auto result = slPCLSetMarker(marker, *frameToken);
	if (result != sl::Result::eOk) {
		static bool loggedMarkerFailure = false;
		if (!loggedMarkerFailure || Upscaling::GetSingleton()->settings.debugLogging) {
			logger::warn("[Streamline] slPCLSetMarker failed marker={} reason={} result={}", static_cast<uint32_t>(marker), reason ? reason : "unknown", ResultToString(result));
			loggedMarkerFailure = true;
		}
		return false;
	}

	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug("[Streamline] PCL marker set (frame={}, marker={}, reason={})", frameID, static_cast<uint32_t>(marker), reason ? reason : "unknown");
	}
	return true;
}

void Streamline::DisableDLSSGAfterError(const char* reason)
{
	if (dlssgDisabledAfterError) {
		return;
	}

	logger::error("[Streamline] Disabling DLSS-G after integration error: {}", reason);

	if (slDLSSGSetOptions) {
		sl::DLSSGOptions options{};
		options.mode = sl::DLSSGMode::eOff;
		options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
		slDLSSGSetOptions(viewport, options);
	}

	dlssgDisabledAfterError = true;
	featureDLSSG = false;
	dlssgOptionsValid = false;
	dlssgConfiguredMode = sl::DLSSGMode::eOff;
}

bool Streamline::ConfigureDLSSG(
	ID3D12Resource* hudless,
	ID3D12Resource* uiColorAndAlpha,
	ID3D12Resource* depth,
	ID3D12Resource* motionVectors,
	sl::DLSSGMode mode,
	const char* reason)
{
	if (!initialized || !featureDLSSG || dlssgDisabledAfterError) {
		return false;
	}

	if (!slDLSSGSetOptions) {
		DisableDLSSGAfterError("slDLSSGSetOptions is unavailable");
		return false;
	}

	auto dx12 = DX12SwapChain::GetSingleton();
	const auto hudlessDesc = hudless ? hudless->GetDesc() : D3D12_RESOURCE_DESC{};
	const auto uiDesc = uiColorAndAlpha ? uiColorAndAlpha->GetDesc() : D3D12_RESOURCE_DESC{};
	const auto depthDesc = depth ? depth->GetDesc() : D3D12_RESOURCE_DESC{};
	const auto motionDesc = motionVectors ? motionVectors->GetDesc() : D3D12_RESOURCE_DESC{};

	sl::DLSSGOptions options{};
	options.mode = mode;
	options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
	options.numFramesToGenerate = 1;
	options.numBackBuffers = std::max(1u, dx12->swapChainDesc.BufferCount);
	options.colorWidth = dx12->swapChainDesc.Width;
	options.colorHeight = dx12->swapChainDesc.Height;
	options.colorBufferFormat = static_cast<uint32_t>(dx12->swapChainDesc.Format);
	options.mvecDepthWidth = motionDesc.Width ? static_cast<uint32_t>(motionDesc.Width) : dx12->swapChainDesc.Width;
	options.mvecDepthHeight = motionDesc.Height ? motionDesc.Height : dx12->swapChainDesc.Height;
	options.mvecBufferFormat = static_cast<uint32_t>(motionDesc.Format);
	options.depthBufferFormat = static_cast<uint32_t>(depthDesc.Format);
	options.hudLessBufferFormat = static_cast<uint32_t>(hudlessDesc.Format);
	options.uiBufferFormat = static_cast<uint32_t>(uiDesc.Format);
	// OptiScaler-style: HUDLess provides a clean scene reference for optical
	// flow without needing explicit UI separation.  Recomposition is kept off
	// — the generated frames retain the game's own UI through normal
	// interpolation while motion vectors are computed against the HUD-less
	// scene, reducing the interference UI motion has on the algorithm.
	options.enableUserInterfaceRecomposition = sl::Boolean::eFalse;

	const bool unchanged =
		dlssgOptionsValid &&
		dlssgConfiguredMode == mode &&
		dlssgConfiguredWidth == options.colorWidth &&
		dlssgConfiguredHeight == options.colorHeight &&
		dlssgConfiguredColorFormat == options.colorBufferFormat &&
		dlssgConfiguredMvecFormat == options.mvecBufferFormat &&
		dlssgConfiguredDepthFormat == options.depthBufferFormat &&
		dlssgConfiguredHudlessFormat == options.hudLessBufferFormat &&
		dlssgConfiguredUIFormat == options.uiBufferFormat &&
		dlssgConfiguredBackBuffers == options.numBackBuffers;

	if (unchanged) {
		return true;
	}

	if (dlssgLastSetOptionsFrame == frameID) {
		logger::warn(
			"[Streamline] Suppressing duplicate DLSS-G option change in frame {} (old={}, new={}, reason={})",
			frameID,
			EnumToString(dlssgConfiguredMode),
			EnumToString(mode),
			reason);
		return true;
	}

	ConfigureReflexForDLSSG();

	const auto result = slDLSSGSetOptions(viewport, options);
	if (result != sl::Result::eOk) {
		logger::error("[Streamline] slDLSSGSetOptions failed: {}", ResultToString(result));
		DisableDLSSGAfterError("slDLSSGSetOptions failed");
		return false;
	}

	dlssgLastSetOptionsFrame = frameID;
	dlssgOptionsValid = true;
	dlssgConfiguredMode = mode;
	dlssgConfiguredWidth = options.colorWidth;
	dlssgConfiguredHeight = options.colorHeight;
	dlssgConfiguredColorFormat = options.colorBufferFormat;
	dlssgConfiguredMvecFormat = options.mvecBufferFormat;
	dlssgConfiguredDepthFormat = options.depthBufferFormat;
	dlssgConfiguredHudlessFormat = options.hudLessBufferFormat;
	dlssgConfiguredUIFormat = options.uiBufferFormat;
	dlssgConfiguredBackBuffers = options.numBackBuffers;

	logger::info(
		"[Streamline] DLSS-G mode={} reason={} color={}x{} fmt={} hudless={}x{} fmt={} ui={}x{} fmt={} depth={}x{} fmt={} mvec={}x{} fmt={}",
		EnumToString(mode),
		reason,
		options.colorWidth,
		options.colorHeight,
		options.colorBufferFormat,
		static_cast<uint32_t>(hudlessDesc.Width),
		hudlessDesc.Height,
		options.hudLessBufferFormat,
		static_cast<uint32_t>(uiDesc.Width),
		uiDesc.Height,
		options.uiBufferFormat,
		static_cast<uint32_t>(depthDesc.Width),
		depthDesc.Height,
		options.depthBufferFormat,
		static_cast<uint32_t>(motionDesc.Width),
		motionDesc.Height,
		options.mvecBufferFormat);

	if (mode == sl::DLSSGMode::eOn && slDLSSGGetState) {
		sl::DLSSGState state{};
		const auto stateResult = slDLSSGGetState(viewport, state, nullptr);
		if (stateResult != sl::Result::eOk) {
			logger::warn("[Streamline] slDLSSGGetState failed after mode change: {}", ResultToString(stateResult));
		} else {
			logger::info(
				"[Streamline] DLSS-G state status={} minSize={} maxGenerated={} vsyncSupport={}",
				EnumToString(state.status),
				state.minWidthOrHeight,
				state.numFramesToGenerateMax,
				state.bIsVsyncSupportAvailable == sl::Boolean::eTrue);

			if (state.status != sl::DLSSGStatus::eOk) {
				DisableDLSSGAfterError("slDLSSGGetState returned non-OK status");
				return false;
			}
		}
	}

	return true;
}

bool Streamline::TagResourcesAndConfigure(
	ID3D12Resource* hudless,
	ID3D12Resource* uiColorAndAlpha,
	ID3D12Resource* depth,
	ID3D12Resource* motionVectors,
	bool enable)
{
	if (!initialized || !featureDLSSG || dlssgDisabledAfterError) {
		return false;
	}

	const auto requestedMode = enable && hudless && depth && motionVectors ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
	if (!hudless || !depth || !motionVectors) {
		static bool loggedMissingResources = false;
		if (enable && !loggedMissingResources) {
			logger::warn(
				"[Streamline] DLSS-G resources are not ready; hudless={}, depth={}, mvec={}",
				hudless != nullptr,
				depth != nullptr,
				motionVectors != nullptr);
			loggedMissingResources = true;
		}
		return ConfigureDLSSG(hudless, uiColorAndAlpha, depth, motionVectors, requestedMode, "missing-resources");
	}

	if (!EnsureFrameToken("DLSS-G resource tagging")) {
		DisableDLSSGAfterError("slGetNewFrameToken failed during DLSS-G tagging");
		return false;
	}

	UpdateConstants(Upscaling::GetSingleton()->jitter);

	auto dx12 = DX12SwapChain::GetSingleton();

	sl::Resource hudlessRes{ sl::ResourceType::eTex2d, hudless, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource uiRes{ sl::ResourceType::eTex2d, uiColorAndAlpha, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource depthRes{ sl::ResourceType::eTex2d, depth, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource mvecRes{ sl::ResourceType::eTex2d, motionVectors, D3D12_RESOURCE_STATE_COMMON };

	// HUDLess and UI must match the backbuffer (swap chain) resolution.
	sl::Extent uiExtent{ 0, 0, dx12->swapChainDesc.Width, dx12->swapChainDesc.Height };

	// Depth and motion vectors are at the *internal render* resolution,
	// which may be smaller than the swap chain when DLSS/FSR upscaling is
	// active.  Using the swap-chain extent here would cause DLSS-G to read
	// beyond buffer bounds → horizontal pixel stretch at render boundary.
	auto depthDesc = depth->GetDesc();
	auto mvecDesc = motionVectors->GetDesc();
	sl::Extent renderExtent{ 0, 0, static_cast<uint32_t>(depthDesc.Width), depthDesc.Height };
	sl::Extent mvecExtent{ 0, 0, static_cast<uint32_t>(mvecDesc.Width), mvecDesc.Height };

	std::vector<sl::ResourceTag> tags;
	tags.reserve(uiColorAndAlpha ? 4u : 3u);
	tags.emplace_back(&hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &uiExtent);
	if (uiColorAndAlpha) {
		tags.emplace_back(&uiRes, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &uiExtent);
	}
	tags.emplace_back(&depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent);
	tags.emplace_back(&mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &mvecExtent);

	const auto tagResult = slSetTagForFrame(
		*frameToken,
		viewport,
		tags.data(),
		static_cast<uint32_t>(tags.size()),
		reinterpret_cast<sl::CommandBuffer*>(dx12->commandLists[dx12->frameIndex].get()));
	if (tagResult != sl::Result::eOk) {
		logger::error("[Streamline] slSetTagForFrame failed: {}", ResultToString(tagResult));
		DisableDLSSGAfterError("slSetTagForFrame failed");
		return false;
	}

	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug("[Streamline] DLSS-G resources tagged (frame={}, frameIndex={}, mode={}, ui={})", frameID, dx12->frameIndex, EnumToString(requestedMode), uiColorAndAlpha != nullptr);
	}

	return ConfigureDLSSG(hudless, uiColorAndAlpha, depth, motionVectors, requestedMode, enable ? "active-frame" : "inactive-frame");
}

void Streamline::LogDLSSGPresentState(bool active, uint64_t presentID)
{
	const auto settings = Upscaling::GetSingleton()->settings;
	if (!settings.debugLogging || !active || !initialized || !featureDLSSG || dlssgDisabledAfterError || !slDLSSGGetState) {
		return;
	}

	static bool loggedFirstActive = false;
	constexpr uint64_t kSampleInterval = 120;
	if (loggedFirstActive && presentID % kSampleInterval != 0) {
		return;
	}

	sl::DLSSGState state{};
	const auto stateResult = slDLSSGGetState(viewport, state, nullptr);
	if (stateResult != sl::Result::eOk) {
		logger::warn("[Streamline] slDLSSGGetState failed after Present: {}", ResultToString(stateResult));
		return;
	}

	loggedFirstActive = true;
	logger::info(
		"[Streamline] DLSS-G present state present={} frame={} status={} presentedSinceLast={} maxGenerated={} fenceValue={}",
		presentID,
		frameID,
		EnumToString(state.status),
		state.numFramesActuallyPresented,
		state.numFramesToGenerateMax,
		state.lastPresentInputsProcessingCompletionFenceValue);
}


void Streamline::AdvanceFrame()
{
	if (initialized) {
		frameID++;
	}
}

bool Streamline::Upscale(
	ID3D12GraphicsCommandList* a_commandList,
	ID3D12Resource* a_color,
	ID3D12Resource* a_output,
	ID3D12Resource* a_depth,
	ID3D12Resource* a_motionVectors,
	float2 a_jitter,
	float2 a_renderSize,
	float2 a_displaySize,
	uint a_qualityMode)
{
	if (!initialized || !featureDLSS || !slDLSSSetOptions || !slEvaluateFeature || !a_commandList || !a_color || !a_output || !a_depth || !a_motionVectors)
		return false;

	UpdateConstants(a_jitter);
	if (!frameToken)
		return false;

	sl::DLSSMode dlssMode = sl::DLSSMode::eDLAA;
	switch (a_qualityMode) {
	case 1:
		dlssMode = sl::DLSSMode::eMaxQuality;
		break;
	case 2:
		dlssMode = sl::DLSSMode::eBalanced;
		break;
	case 3:
		dlssMode = sl::DLSSMode::eMaxPerformance;
		break;
	case 4:
		dlssMode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		break;
	}

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = dlssMode;
	dlssOptions.outputWidth = std::max(1u, static_cast<uint32_t>(a_displaySize.x));
	dlssOptions.outputHeight = std::max(1u, static_cast<uint32_t>(a_displaySize.y));
	dlssOptions.colorBuffersHDR = sl::Boolean::eFalse;
	dlssOptions.useAutoExposure = sl::Boolean::eTrue;

	// Apply DLSS preset per-quality-mode.
	// 0 = Auto (eDefault — DLSS auto-selects), 10 = J, 11 = K, 12 = L, 13 = M.
	const auto presetSetting = Upscaling::GetSingleton()->settings.dlssPreset;
	const auto resolvePreset = [](int presetValue) -> sl::DLSSPreset {
		if (presetValue >= 10 && presetValue <= 15)
			return static_cast<sl::DLSSPreset>(presetValue);
		return sl::DLSSPreset::eDefault;
	};
	const auto p = resolvePreset(presetSetting);
	dlssOptions.dlaaPreset = p;
	dlssOptions.qualityPreset = p;
	dlssOptions.balancedPreset = p;
	dlssOptions.performancePreset = p;
	dlssOptions.ultraPerformancePreset = p;
	dlssOptions.ultraQualityPreset = p;

	const auto optionsResult = slDLSSSetOptions(viewport, dlssOptions);
	if (optionsResult != sl::Result::eOk) {
		logger::warn("[Streamline] Could not enable DLSS: {}", ResultToString(optionsResult));
		return false;
	}

	if (Upscaling::GetSingleton()->settings.debugLogging && presetSetting != 0) {
		logger::debug("[Streamline] DLSS preset overridden to {} (mode={})", presetSetting, EnumToString(dlssMode));
	}

	sl::Extent lowResExtent{ 0, 0, std::max(1u, static_cast<uint32_t>(a_renderSize.x)), std::max(1u, static_cast<uint32_t>(a_renderSize.y)) };
	sl::Extent fullExtent{ 0, 0, std::max(1u, static_cast<uint32_t>(a_displaySize.x)), std::max(1u, static_cast<uint32_t>(a_displaySize.y)) };

	sl::Resource colorIn{ sl::ResourceType::eTex2d, a_color, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource colorOut{ sl::ResourceType::eTex2d, a_output, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource depth{ sl::ResourceType::eTex2d, a_depth, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource mvec{ sl::ResourceType::eTex2d, a_motionVectors, D3D12_RESOURCE_STATE_COMMON };

	sl::ResourceTag colorInTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &lowResExtent };
	sl::ResourceTag colorOutTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
	sl::ResourceTag depthTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &lowResExtent };
	sl::ResourceTag mvecTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &lowResExtent };

	sl::ViewportHandle view(viewport);
	const sl::BaseStructure* inputs[] = { &view, &colorInTag, &colorOutTag, &depthTag, &mvecTag };
	const auto evalResult = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), reinterpret_cast<sl::CommandBuffer*>(a_commandList));
	if (evalResult != sl::Result::eOk) {
		logger::warn("[Streamline] DLSS evaluation failed: {}", ResultToString(evalResult));
		return false;
	}

	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug(
			"[Streamline] DLSS evaluated (frame={}, render={}x{}, output={}x{}, mode={})",
			frameID,
			static_cast<uint32_t>(a_renderSize.x),
			static_cast<uint32_t>(a_renderSize.y),
			dlssOptions.outputWidth,
			dlssOptions.outputHeight,
			EnumToString(dlssMode));
	}

	return true;
}

void Streamline::UpdateConstants(float2 a_jitter)
{
	if (!slGetNewFrameToken || !slSetConstants)
		return;

	if (constantsFrameID == frameID) {
		if (ShouldTraceStreamlineFrame(frameID)) {
			logger::debug("[Streamline] DLSS constants already set for frame {}", frameID);
		}
		return;
	}

	sl::Constants slConstants{};
	auto dx12 = DX12SwapChain::GetSingleton();
	const float aspectRatio =
		dx12->swapChainDesc.Height != 0 ?
			static_cast<float>(dx12->swapChainDesc.Width) / static_cast<float>(dx12->swapChainDesc.Height) :
			(16.0f / 9.0f);

	slConstants.cameraViewToClip = IdentityMatrix();
	slConstants.clipToCameraView = IdentityMatrix();
	slConstants.clipToPrevClip = IdentityMatrix();
	slConstants.prevClipToClip = IdentityMatrix();
	slConstants.cameraPinholeOffset = { 0.0f, 0.0f };
	slConstants.cameraPos = { 0.0f, 0.0f, 0.0f };
	slConstants.cameraUp = { 0.0f, 1.0f, 0.0f };
	slConstants.cameraRight = { 1.0f, 0.0f, 0.0f };
	slConstants.cameraFwd = { 0.0f, 0.0f, 1.0f };
	slConstants.cameraNear = 0.0f;
	slConstants.cameraFar = 1.0f;
	slConstants.cameraFOV = 1.0471976f;
	slConstants.cameraAspectRatio = aspectRatio;
	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.depthInverted = sl::Boolean::eFalse;
	slConstants.jitterOffset = { -a_jitter.x, -a_jitter.y };
	slConstants.mvecScale = { 1.0f, 1.0f };
	slConstants.reset = sl::Boolean::eFalse;
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	if (!EnsureFrameToken("DLSS constants")) {
		return;
	}

	const auto result = slSetConstants(slConstants, *frameToken, viewport);
	if (result != sl::Result::eOk) {
		logger::warn("[Streamline] Could not set DLSS constants: {}", ResultToString(result));
		return;
	}

	constantsFrameID = frameID;
	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug("[Streamline] DLSS constants set (frame={}, jitter={}, {})", frameID, a_jitter.x, a_jitter.y);
	}
}

void Streamline::DestroyDLSSResources()
{
	if (!featureDLSS || !slDLSSSetOptions)
		return;

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;
	slDLSSSetOptions(viewport, dlssOptions);

	if (slFreeResources)
		slFreeResources(sl::kFeatureDLSS, viewport);
}
