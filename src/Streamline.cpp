#include "Streamline.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "DX12SwapChain.h"

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

	void StreamlineLogCallback(sl::LogType type, const char* msg)
	{
		switch (type) {
		case sl::LogType::eError:
			logger::error("[Streamline][SDK] {}", msg ? msg : "");
			break;
		case sl::LogType::eWarn:
			logger::warn("[Streamline][SDK] {}", msg ? msg : "");
			break;
		default:
			logger::info("[Streamline][SDK] {}", msg ? msg : "");
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
			addUnique(pluginDir / L"Upscaling" / L"Streamline");
			addUnique(pluginDir / L"FrameGeneration" / L"Streamline");
		}

		if (const auto exeDir = GetModuleDirectory(nullptr); !exeDir.empty()) {
			addUnique(exeDir / L"Data" / L"F4SE" / L"Plugins" / L"Streamline");
			addUnique(exeDir / L"Data" / L"F4SE" / L"Plugins" / L"Upscaling" / L"Streamline");
			addUnique(exeDir / L"Data" / L"F4SE" / L"Plugins" / L"FrameGeneration" / L"Streamline");
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
	slSetTagForFrame = (PFun_slSetTagForFrame*)GetProcAddress(interposer, "slSetTagForFrame");
	slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");

	if (!slInit) {
		logger::error("[Streamline] Failed to get slInit");
		return;
	}

	sl::Preferences pref{};
	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };
	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);
	pref.logLevel = sl::LogLevel::eDefault;
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
	pref.flags = sl::PreferenceFlags::eUseManualHooking |
	             sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	if (slInit(pref, sl::kSDKVersion) != sl::Result::eOk) {
		logger::error("[Streamline] slInit failed");
		return;
	}

	initialized = true;
	logger::info("[Streamline] Initialized successfully");
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

	// Check DLSS-G support on this adapter
	DXGI_ADAPTER_DESC adapterDesc{};
	adapter->GetDesc(&adapterDesc);
	logger::info(
		"[Streamline] Checking DLSS-G support on adapter '{}' (vendor=0x{:X}, device=0x{:X})",
		WideToUtf8(adapterDesc.Description),
		adapterDesc.VendorId,
		adapterDesc.DeviceId);

	sl::AdapterInfo adapterInfo{};
	adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
	adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

	if (slGetFeatureRequirements) {
		sl::FeatureRequirements requirements{};
		const auto requirementsResult = slGetFeatureRequirements(sl::kFeatureDLSS_G, requirements);
		logger::info(
			"[Streamline] DLSS-G requirements query: {}",
			ResultToString(requirementsResult));
		if (requirementsResult == sl::Result::eOk) {
			logger::info(
				"[Streamline] DLSS-G requirements: flags=0x{:X}, osDetected={}, osRequired={}, driverDetected={}, driverRequired={}",
				static_cast<uint32_t>(requirements.flags),
				requirements.osVersionDetected.toStr(),
				requirements.osVersionRequired.toStr(),
				requirements.driverVersionDetected.toStr(),
				requirements.driverVersionRequired.toStr());
		}
	}

	bool loaded = false;
	const auto loadedResult = slIsFeatureLoaded ? slIsFeatureLoaded(sl::kFeatureDLSS_G, loaded) : sl::Result::eErrorMissingOrInvalidAPI;
	logger::info(
		"[Streamline] DLSS-G loaded query: {} (loaded={})",
		ResultToString(loadedResult),
		loaded);

	if (loadedResult == sl::Result::eOk && loaded) {
		const auto supportResult = slIsFeatureSupported ? slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo) : sl::Result::eErrorMissingOrInvalidAPI;
		featureDLSSG = supportResult == sl::Result::eOk;
		logger::info(
			"[Streamline] DLSS-G support query: {}",
			ResultToString(supportResult));
	}

	if (featureDLSSG) {
		slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", (void*&)slDLSSGGetState);
		slGetFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", (void*&)slDLSSGSetOptions);
		logger::info("[Streamline] DLSS-G is supported and ready");
	} else {
		logger::info("[Streamline] DLSS-G is not supported on this adapter");
	}
}

void Streamline::TagResourcesAndConfigure(
	ID3D12Resource* hudless,
	ID3D12Resource* depth,
	ID3D12Resource* motionVectors,
	bool enable)
{
	if (!initialized || !featureDLSSG)
		return;

	auto dx12 = DX12SwapChain::GetSingleton();

	// Advance frame token
	uint32_t fid = (uint32_t)frameID;
	if (slGetNewFrameToken(frameToken, &fid) != sl::Result::eOk)
		return;

	// Tag resources
	sl::Resource hudlessRes{ sl::ResourceType::eTex2d, hudless, nullptr, nullptr, 0 };
	sl::Resource depthRes{ sl::ResourceType::eTex2d, depth, nullptr, nullptr, 0 };
	sl::Resource mvecRes{ sl::ResourceType::eTex2d, motionVectors, nullptr, nullptr, 0 };

	sl::Extent fullExtent{ 0, 0, dx12->swapChainDesc.Width, dx12->swapChainDesc.Height };

	sl::ResourceTag tags[] = {
		{ &hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
		{ &depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
		{ &mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent },
	};

	if (slSetTagForFrame && frameToken) {
		slSetTagForFrame(*frameToken, viewport, tags, _countof(tags), nullptr);
	}

	// Configure DLSS-G
	if (slDLSSGSetOptions) {
		sl::DLSSGOptions opts{};
		opts.mode = enable ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
		opts.numBackBuffers = 2;
		opts.colorWidth = dx12->swapChainDesc.Width;
		opts.colorHeight = dx12->swapChainDesc.Height;
		opts.colorBufferFormat = (uint32_t)dx12->swapChainDesc.Format;
		opts.mvecDepthWidth = dx12->swapChainDesc.Width;
		opts.mvecDepthHeight = dx12->swapChainDesc.Height;
		if (slDLSSGSetOptions(viewport, opts) != sl::Result::eOk)
			logger::warn("[Streamline] slDLSSGSetOptions failed");
	}
}

void Streamline::AdvanceFrame()
{
	if (initialized)
		frameID++;
}
