#include "Upscaler.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <d3dcompiler.h>

#include "DX12SwapChain.h"
#include "DirectXMath.h"
#include "FidelityFX.h"
#include "Streamline.h"

void InstallUpscalerRenderBackendHooks();

enum class RenderTarget
{
	kFrameBuffer = 0,

	kRefractionNormal = 1,
	
	kMainPreAlpha = 2,
	kMain = 3,
	kMainTemp = 4,

	kSSRRaw = 7,
	kSSRBlurred = 8,
	kSSRBlurredExtra = 9,

	kMainVerticalBlur = 14,
	kMainHorizontalBlur = 15,

	kSSRDirection = 10,
	kSSRMask = 11,

	kUI = 17,
	kUITemp = 18,

	kGbufferNormal = 20,
	kGbufferNormalSwap = 21,
	kGbufferAlbedo = 22,
	kGbufferEmissive = 23,
	kGbufferMaterial = 24, //  Glossiness, Specular, Backlighting, SSS

	kSSAO = 28,

	kTAAAccumulation = 26,
	kTAAAccumulationSwap = 27,

	kMotionVectors = 29,

	kUIDownscaled = 36,
	kUIDownscaledComposite = 37,

	kMainDepthMips = 39,

	kUnkMask = 57,

	kSSAOTemp = 48,
	kSSAOTemp2 = 49,
	kSSAOTemp3 = 50,

	kDiffuseBuffer = 58,
	kSpecularBuffer = 59,

	kDownscaledHDR = 64,
	kDownscaledHDRLuminance2 = 65,
	kDownscaledHDRLuminance3 = 66,
	kDownscaledHDRLuminance4 = 67,
	kDownscaledHDRLuminance5Adaptation = 68,
	kDownscaledHDRLuminance6AdaptationSwap = 69,
	kDownscaledHDRLuminance6 = 70,

	kCount = 101
};

enum class DepthStencilTarget
{
	kMainOtherOther = 0,
	kMainOther = 1,
	kMain = 2,
	kMainCopy = 3,
	kMainCopyCopy = 4,

	kShadowMap = 8,

	kCount = 13
};

namespace
{
	struct IniSource
	{
		std::filesystem::path path;
		const char* label;
	};

	bool LoadIniIfExists(CSimpleIniA& ini, const IniSource& source, const char* component)
	{
		std::error_code ec;
		if (!std::filesystem::exists(source.path, ec)) {
			return false;
		}

		if (ini.LoadFile(source.path.string().c_str()) < 0) {
			logger::warn("[{}] Failed to load {} settings from {}", component, source.label, source.path.string());
			return false;
		}

		logger::debug("[{}] Loaded {} settings from {}", component, source.label, source.path.string());
		return true;
	}

	int ClampIntSetting(int value, int minValue, int maxValue, const char* settingName)
	{
		const int clamped = std::clamp(value, minValue, maxValue);
		if (clamped != value) {
			logger::warn("[Upscaler] {}={} is out of range, clamping to {}", settingName, value, clamped);
		}
		return clamped;
	}

	std::optional<std::string> GetEnvironmentValue(const char* name)
	{
		char buffer[64]{};
		const auto length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(std::size(buffer)));
		if (length == 0 || length >= std::size(buffer)) {
			return std::nullopt;
		}
		return std::string(buffer, length);
	}

	bool IsTruthy(std::string_view value)
	{
		return value == "1" || value == "true" || value == "TRUE" || value == "on" || value == "ON";
	}

	bool IsFrameGenPluginVisible()
	{
		std::error_code ec;
		return std::filesystem::exists("Data\\F4SE\\Plugins\\FrameGen.dll", ec);
	}

	std::optional<int> ParseIntSetting(std::string_view value)
	{
		try {
			return std::stoi(std::string(value));
		} catch (...) {
			return std::nullopt;
		}
	}

#ifdef FO4CS_ENABLE_DEBUG_SETTINGS
	constexpr bool kDebugSettingsSupported = true;
#else
	constexpr bool kDebugSettingsSupported = false;
#endif

	void LoadSharedDebugSettings(Upscaling::Settings& settings)
	{
		if constexpr (kDebugSettingsSupported) {
			settings.debugLogging = true;
			settings.streamlineLogLevel = 2;
			settings.debugFrameLogCount = 240;
		} else {
			settings.debugLogging = false;
			settings.streamlineLogLevel = 0;
			settings.debugFrameLogCount = 0;
		}
	}

	void ApplyDebugEnvironmentOverrides(Upscaling::Settings& settings)
	{
		if constexpr (!kDebugSettingsSupported) {
			return;
		}

		if (const auto value = GetEnvironmentValue("FO4CS_DEBUG_LOG")) {
			settings.debugLogging = IsTruthy(*value);
		}
		if (const auto value = GetEnvironmentValue("FO4CS_STREAMLINE_LOG_LEVEL")) {
			if (const auto parsed = ParseIntSetting(*value)) {
				settings.streamlineLogLevel = ClampIntSetting(*parsed, 0, 2, "FO4CS_STREAMLINE_LOG_LEVEL");
			}
		}
		if (const auto value = GetEnvironmentValue("FO4CS_DEBUG_FRAMES")) {
			if (const auto parsed = ParseIntSetting(*value)) {
				settings.debugFrameLogCount = ClampIntSetting(*parsed, 0, 600, "FO4CS_DEBUG_FRAMES");
			}
		}
	}

	void ConfigureDebugLogging(const Upscaling::Settings& settings)
	{
		if (!settings.debugLogging) {
			return;
		}

		spdlog::set_level(spdlog::level::debug);
		if (auto log = spdlog::default_logger()) {
			log->flush_on(spdlog::level::warn);
		}
	}
}

ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const char* ProgramType, const char* Program = "main")
{
	auto rendererData = fo4cs::GetRendererData();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Compiler setup
	uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

	ID3DBlob* shaderBlob;
	ID3DBlob* shaderErrors;

	std::string str;
	std::wstring path{ FilePath };
	std::transform(path.begin(), path.end(), std::back_inserter(str), [](wchar_t c) {
		return (char)c;
	});
	if (!std::filesystem::exists(FilePath)) {
		logger::error("Failed to compile shader; {} does not exist", str);
		return nullptr;
	}
	if (FAILED(D3DCompileFromFile(FilePath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, Program, ProgramType, flags, 0, &shaderBlob, &shaderErrors))) {
		logger::warn("Shader compilation failed:\n\n{}", shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
		return nullptr;
	}
	if (shaderErrors)
		logger::debug("Shader logs:\n{}", static_cast<char*>(shaderErrors->GetBufferPointer()));

	ID3D11ComputeShader* regShader;
	DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
	return regShader;
}

ID3D11DeviceChild* CompileFrameGenerationShader(const wchar_t* fileName, const char* programType, const char* program = "main")
{
	static constexpr std::array<std::wstring_view, 1> shaderDirectories{
		L"Data\\F4SE\\Plugins\\FrameGen"
	};

	for (const auto directory : shaderDirectories) {
		const auto path = std::filesystem::path(directory) / fileName;
		std::error_code ec;
		if (std::filesystem::exists(path, ec)) {
			return CompileShader(path.c_str(), programType, program);
		}
	}

	logger::error("[FrameGen] Failed to compile shader; {} was not found in FrameGen", std::filesystem::path(fileName).string());
	return nullptr;
}

void Upscaling::LoadFrameGenerationSettings()
{
	const std::vector<IniSource> iniSources{
		{ std::filesystem::path("Data\\MCM\\Config\\FrameGen\\settings.ini"), "MCM default" },
		{ std::filesystem::path("Data\\MCM\\Settings\\FrameGen.ini"), "MCM user override" }
	};

	CSimpleIniA ini;
	ini.SetUnicode();

	bool loadedAny = false;
	for (const auto& source : iniSources) {
		loadedAny = LoadIniIfExists(ini, source, "Frame Generation") || loadedAny;
	}

	if (!loadedAny) {
		logger::warn("[FrameGen] Settings file not found, using defaults");
	}

	settings.frameGenerationMode = ini.GetBoolValue("Settings", "bFrameGenerationMode", true);
	settings.frameLimitMode = ini.GetBoolValue("Settings", "bFrameLimitMode", true);
	settings.frameGenerationBackend = static_cast<int>(ini.GetLongValue("Settings", "iFrameGenerationBackend", 0));
	LoadSharedDebugSettings(settings);
	ApplyDebugEnvironmentOverrides(settings);
	ConfigureDebugLogging(settings);
}

void Upscaling::LoadReflexSettings()
{
	const std::vector<IniSource> iniSources{
		{ std::filesystem::path("Data\\MCM\\Config\\Reflex\\settings.ini"), "MCM default" },
		{ std::filesystem::path("Data\\MCM\\Settings\\Reflex.ini"), "MCM user override" }
	};

	CSimpleIniA ini;
	ini.SetUnicode();

	bool loadedAny = false;
	for (const auto& source : iniSources) {
		loadedAny = LoadIniIfExists(ini, source, "Reflex") || loadedAny;
	}

	if (!loadedAny) {
		logger::warn("[Reflex] Settings file not found, using defaults");
	}

	settings.reflexMode = ClampIntSetting(
		static_cast<int>(ini.GetLongValue("Settings", "iReflexMode", settings.reflexMode)),
		0,
		2,
		"iReflexMode");
	settings.reflexSleepMode = ini.GetBoolValue("Settings", "bReflexSleepMode", settings.reflexSleepMode);
	LoadSharedDebugSettings(settings);
	ApplyDebugEnvironmentOverrides(settings);
	ConfigureDebugLogging(settings);

	logger::info(
		"[Settings] Reflex(mode={}, sleep={}), Debug(enabled={}, streamlineLogLevel={}, frames={})",
		settings.reflexMode,
		settings.reflexSleepMode,
		settings.debugLogging,
		settings.streamlineLogLevel,
		settings.debugFrameLogCount);
}

void Upscaling::LoadSettings()
{
	LoadFrameGenerationSettings();

	if (pluginMode == PluginMode::kUpscaler && !IsFrameGenPluginVisible()) {
		settings.frameGenerationMode = false;
		settings.frameLimitMode = false;
		logger::info("[FrameGen] FrameGen.dll is not visible; disabling frame generation in Upscaler mode");
	}

	const std::vector<IniSource> upscalerIniSources{
		{ std::filesystem::path("Data\\MCM\\Config\\Upscaler\\settings.ini"), "MCM default" },
		{ std::filesystem::path("Data\\MCM\\Settings\\Upscaler.ini"), "MCM user override" }
	};

	CSimpleIniA upscalerIni;
	upscalerIni.SetUnicode();

	bool loadedUpscalerSettings = false;
	for (const auto& source : upscalerIniSources) {
		loadedUpscalerSettings = LoadIniIfExists(upscalerIni, source, "Upscaler") || loadedUpscalerSettings;
	}

	if (!loadedUpscalerSettings) {
		logger::warn("[Upscaler] Settings file not found, using defaults");
	}

	settings.upscaleMethodPreference = ClampIntSetting(
		static_cast<int>(upscalerIni.GetLongValue("Settings", "iUpscaleMethodPreference", settings.upscaleMethodPreference)),
		0,
		2,
		"iUpscaleMethodPreference");
	settings.qualityMode = ClampIntSetting(
		static_cast<int>(upscalerIni.GetLongValue("Settings", "iQualityMode", settings.qualityMode)),
		0,
		4,
		"iQualityMode");
	settings.dlssPreset = ClampIntSetting(
		static_cast<int>(upscalerIni.GetLongValue("Settings", "iDLSSPreset", settings.dlssPreset)),
		0,
		15,
		"iDLSSPreset");

	logger::info(
		"[Settings] FrameGen(enabled={}, limiter={}, backend={}), Upscaler(method={}, quality={}, dlssPreset={}), Reflex(mode={}), Debug(enabled={}, streamlineLogLevel={}, frames={})",
		settings.frameGenerationMode,
		settings.frameLimitMode,
		settings.frameGenerationBackend,
		settings.upscaleMethodPreference,
		settings.qualityMode,
		settings.dlssPreset,
		settings.reflexMode,
		settings.debugLogging,
		settings.streamlineLogLevel,
		settings.debugFrameLogCount);
}

Upscaling::UpscaleMethod Upscaling::GetPreferredUpscaleMethod() const
{
	switch (static_cast<UpscaleMethod>(settings.upscaleMethodPreference)) {
	case UpscaleMethod::kFSR:
		return UpscaleMethod::kFSR;
	case UpscaleMethod::kDLSS:
		return UpscaleMethod::kDLSS;
	default:
		return UpscaleMethod::kDisabled;
	}
}

bool Upscaling::UsesDLSSUpscaling() const
{
	return pluginMode == PluginMode::kUpscaler && GetPreferredUpscaleMethod() == UpscaleMethod::kDLSS;
}

bool Upscaling::UsesFSRUpscaling() const
{
	return pluginMode == PluginMode::kUpscaler && GetPreferredUpscaleMethod() == UpscaleMethod::kFSR;
}

bool Upscaling::UsesDLSSFrameGeneration() const
{
	if (pluginMode == PluginMode::kReflex || !settings.frameGenerationMode)
		return false;

	switch (settings.frameGenerationBackend) {
	case 0: // Auto: follow upscale preference
		return GetPreferredUpscaleMethod() == UpscaleMethod::kDLSS;
	case 1: // Force NVIDIA DLSS-G
		return true;
	case 2: // Force AMD FSR FG
		return false;
	default: // Off
		return false;
	}
}

bool Upscaling::UsesFSRFrameGeneration() const
{
	if (pluginMode == PluginMode::kReflex || !settings.frameGenerationMode)
		return false;

	switch (settings.frameGenerationBackend) {
	case 0: // Auto: follow upscale preference
		return GetPreferredUpscaleMethod() == UpscaleMethod::kFSR;
	case 1: // Force NVIDIA DLSS-G
		return false;
	case 2: // Force AMD FSR FG
		return true;
	default: // Off
		return false;
	}
}

bool Upscaling::UsesReflex() const
{
	return UsesDLSSFrameGeneration() || (pluginMode == PluginMode::kReflex && settings.reflexMode > 0);
}

void Upscaling::PostPostLoad()
{
	highFPSPhysicsFixLoaded = GetModuleHandleA("Data\\F4SE\\Plugins\\HighFPSPhysicsFix.dll") != nullptr;

	logger::debug("[FrameGen] HighFPSPhysicsFix.dll loaded: {}", highFPSPhysicsFixLoaded);

	renderBackendEnabled = pluginMode == PluginMode::kUpscaler &&
		((UsesDLSSUpscaling() && Streamline::GetSingleton()->featureDLSS) ||
		 (UsesFSRUpscaling() && d3d12Interop && FidelityFX::GetSingleton()->featureFSR));
	InstallHooks();
}

void Upscaling::CreateFrameGenerationResources()
{
	logger::debug("[FrameGen] Creating resources");
	
	setupBuffers = true;

	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto& main = rendererData->renderTargets[(uint)RenderTarget::kMain];

	for (int index = 0; index < 2; index++) {
		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

		reinterpret_cast<ID3D11Texture2D*>(main.texture)->GetDesc(&texDesc);
		reinterpret_cast<ID3D11ShaderResourceView*>(main.srView)->GetDesc(&srvDesc);
		reinterpret_cast<ID3D11RenderTargetView*>(main.rtView)->GetDesc(&rtvDesc);

		texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

		uavDesc.Format = texDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;

		texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

		// Save the render-target dimensions (kMain / internal resolution)
		// for depth + motion-vector buffers, then override for HUDLess and
		// UI buffers which DLSS-G requires at backbuffer (swap chain) size.
		const auto renderWidth = texDesc.Width;
		const auto renderHeight = texDesc.Height;
		auto dx12SwapChain = DX12SwapChain::GetSingleton();

		// ---- HUDLess, UI, reticle: backbuffer resolution ----
		texDesc.Width = dx12SwapChain->swapChainDesc.Width;
		texDesc.Height = dx12SwapChain->swapChainDesc.Height;
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		HUDLessBufferShared[index] = new Texture2D(texDesc);
		HUDLessBufferShared[index]->CreateSRV(srvDesc);
		HUDLessBufferShared[index]->CreateRTV(rtvDesc);
		HUDLessBufferShared[index]->CreateUAV(uavDesc);

		uiColorAndAlphaBufferShared[index] = new Texture2D(texDesc);
		uiColorAndAlphaBufferShared[index]->CreateSRV(srvDesc);
		uiColorAndAlphaBufferShared[index]->CreateRTV(rtvDesc);
		uiColorAndAlphaBufferShared[index]->CreateUAV(uavDesc);

		reticleColorAndAlphaBufferShared[index] = new Texture2D(texDesc);
		reticleColorAndAlphaBufferShared[index]->CreateSRV(srvDesc);
		reticleColorAndAlphaBufferShared[index]->CreateRTV(rtvDesc);
		reticleColorAndAlphaBufferShared[index]->CreateUAV(uavDesc);

		// ---- Depth: internal render resolution ----
		texDesc.Width = renderWidth;
		texDesc.Height = renderHeight;
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		depthBufferShared[index] = new Texture2D(texDesc);
		depthBufferShared[index]->CreateSRV(srvDesc);
		depthBufferShared[index]->CreateRTV(rtvDesc);
		depthBufferShared[index]->CreateUAV(uavDesc);

		auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
		D3D11_TEXTURE2D_DESC texDescMotionVector{};
		reinterpret_cast<ID3D11Texture2D*>(motionVector.texture)->GetDesc(&texDescMotionVector);

		// ---- Motion vectors: internal render resolution ----
		texDesc.Width = renderWidth;
		texDesc.Height = renderHeight;
		texDesc.Format = texDescMotionVector.Format;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		motionVectorBufferShared[index] = new Texture2D(texDesc);
		motionVectorBufferShared[index]->CreateSRV(srvDesc);
		motionVectorBufferShared[index]->CreateRTV(rtvDesc);
		motionVectorBufferShared[index]->CreateUAV(uavDesc);

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(HUDLessBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&HUDLessBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(depthBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&depthBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(uiColorAndAlphaBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&uiColorAndAlphaBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(motionVectorBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&motionVectorBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->ClearRenderTargetView(HUDLessBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(uiColorAndAlphaBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(reticleColorAndAlphaBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(depthBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(motionVectorBufferShared[index]->rtv.get(), clearColor);
	}

	copyDepthToSharedBufferCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"CopyDepthToSharedBufferCS.hlsl", "cs_5_0");
	generateSharedBuffersCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"GenerateSharedBuffersCS.hlsl", "cs_5_0");
	buildUIColorAndAlphaCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"BuildUIColorAndAlphaCS.hlsl", "cs_5_0");
	buildReticleUIColorAndAlphaCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"BuildReticleUIColorAndAlphaCS.hlsl", "cs_5_0");
	patchHUDLessReticleCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"PatchHUDLessReticleCS.hlsl", "cs_5_0");
}

void Upscaling::PreAlpha()
{
	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	auto& colorMain = rendererData->renderTargets[(uint)RenderTarget::kMain];
	auto& colorPostAlpha = rendererData->renderTargets[(uint)RenderTarget::kMainTemp];

	context->CopyResource(reinterpret_cast<ID3D11Texture2D*>(colorMain.texture), reinterpret_cast<ID3D11Texture2D*>(colorPostAlpha.texture));

	// DLSS-G HUDLess: capture from kFrameBuffer at PreAlpha time.
	// At this point post-processing (bloom, tonemapping) is complete but
	// reticle and UI have not been drawn yet.  This guarantees that
	// FinalColor - HUDLess = UI contribution only (no post-process noise).
	if (!d3d12Interop)
		return;
	if (!setupBuffers)
		CreateFrameGenerationResources();

	auto& frameBuffer = rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer];
	const auto frameIndex = DX12SwapChain::GetSingleton()->frameIndex;
	context->CopyResource(HUDLessBufferShared[frameIndex]->resource.get(), reinterpret_cast<ID3D11Texture2D*>(frameBuffer.texture));
}

void Upscaling::PostAlpha()
{
	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();

	auto rendererData = fo4cs::GetRendererData();

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	{
		auto& colorPreAlpha = rendererData->renderTargets[(uint)RenderTarget::kMain];
		auto& colorPostAlpha = rendererData->renderTargets[(uint)RenderTarget::kMainTemp];

		auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
		auto& depth = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];

		{
			uint32_t dispatchX = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Width) / 8.0f);
			uint32_t dispatchY = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Height) / 8.0f);

			ID3D11ShaderResourceView* views[4] = { 
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPreAlpha.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPostAlpha.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(motionVector.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth)
			};

			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[2] = { motionVectorBufferShared[dx12SwapChain->frameIndex]->uav.get(), depthBufferShared[dx12SwapChain->frameIndex]->uav.get()};
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(generateSharedBuffersCS, nullptr, 0);

			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* views[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[2] = { nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		if (reticleColorAndAlphaBufferShared[frameIndex] && buildReticleUIColorAndAlphaCS) {
			const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(dx12SwapChain->swapChainDesc.Width) / 8.0f));
			const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(dx12SwapChain->swapChainDesc.Height) / 8.0f));

			ID3D11ShaderResourceView* reticleViews[2] = {
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPreAlpha.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPostAlpha.srView)
			};
			context->CSSetShaderResources(0, ARRAYSIZE(reticleViews), reticleViews);

			ID3D11UnorderedAccessView* reticleUAVs[1] = { reticleColorAndAlphaBufferShared[frameIndex]->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(reticleUAVs), reticleUAVs, nullptr);

			context->CSSetShader(buildReticleUIColorAndAlphaCS, nullptr, 0);
			context->Dispatch(dispatchX, dispatchY, 1);

			ID3D11ShaderResourceView* nullReticleViews[2] = { nullptr, nullptr };
			context->CSSetShaderResources(0, ARRAYSIZE(nullReticleViews), nullReticleViews);

			ID3D11UnorderedAccessView* nullReticleUAVs[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullReticleUAVs), nullReticleUAVs, nullptr);

			context->CSSetShader(shader, nullptr, 0);
		}
	}
}

void Upscaling::CopyBuffersToSharedResources()
{
	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();
auto rendererData = fo4cs::GetRendererData();


	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	
	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
	context->CopyResource(motionVectorBufferShared[dx12SwapChain->frameIndex]->resource.get(), reinterpret_cast<ID3D11Texture2D*>(motionVector.texture));
		
	{
		auto& depth = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];

		{
			uint32_t dispatchX = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Width) / 8.0f);
			uint32_t dispatchY = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Height) / 8.0f);


			ID3D11ShaderResourceView* views[1] = { reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth) };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { depthBufferShared[dx12SwapChain->frameIndex]->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(copyDepthToSharedBufferCS, nullptr, 0);

			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* views[1] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}	
}

bool Upscaling::BuildUIColorAndAlphaResource(ID3D11Texture2D* a_finalFrame)
{
	if (!d3d12Interop || !a_finalFrame)
		return false;

	if (!setupBuffers)
		CreateFrameGenerationResources();

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;
	if (!HUDLessBufferShared[frameIndex] || !uiColorAndAlphaBufferShared[frameIndex] || !reticleColorAndAlphaBufferShared[frameIndex] || !buildUIColorAndAlphaCS)
		return false;

	auto rendererData = fo4cs::GetRendererData();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	D3D11_TEXTURE2D_DESC finalDesc{};
	a_finalFrame->GetDesc(&finalDesc);
	if (finalDesc.Width == 0 || finalDesc.Height == 0)
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC finalSrvDesc{};
	finalSrvDesc.Format = finalDesc.Format;
	finalSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	finalSrvDesc.Texture2D.MostDetailedMip = 0;
	finalSrvDesc.Texture2D.MipLevels = 1;

	winrt::com_ptr<ID3D11ShaderResourceView> finalFrameSRV;
	if (FAILED(device->CreateShaderResourceView(a_finalFrame, &finalSrvDesc, finalFrameSRV.put()))) {
		static bool loggedSRVFailure = false;
		if (!loggedSRVFailure) {
			logger::warn("[FrameGen] Could not create final-frame SRV; DLSS-G UI alpha tag is unavailable");
			loggedSRVFailure = true;
		}
		return false;
	}

	context->OMSetRenderTargets(0, nullptr, nullptr);

	const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(finalDesc.Width) / 8.0f));
	const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(finalDesc.Height) / 8.0f));

	ID3D11ShaderResourceView* views[3] = {
		finalFrameSRV.get(),
		HUDLessBufferShared[frameIndex]->srv.get(),
		reticleColorAndAlphaBufferShared[frameIndex]->srv.get()
	};
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11UnorderedAccessView* uavs[1] = { uiColorAndAlphaBufferShared[frameIndex]->uav.get() };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	context->CSSetShader(buildUIColorAndAlphaCS, nullptr, 0);
	context->Dispatch(dispatchX, dispatchY, 1);

	ID3D11ShaderResourceView* nullViews[3] = { nullptr, nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);

	ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

	ID3D11ComputeShader* shader = nullptr;
	context->CSSetShader(shader, nullptr, 0);
	return true;
}

void Upscaling::TimerSleepQPC(int64_t targetQPC)
{
	LARGE_INTEGER currentQPC;
	do {
		QueryPerformanceCounter(&currentQPC);
	} while (currentQPC.QuadPart < targetQPC);
}

void Upscaling::FrameLimiter(bool a_useFrameGeneration)
{
	static LARGE_INTEGER lastFrame = {};

	if (d3d12Interop && settings.frameLimitMode) {

		// Stick within VRR bounds
		double bestRefreshRate = refreshRate - (refreshRate * refreshRate) / 3600.0;

		LARGE_INTEGER qpf;
		QueryPerformanceFrequency(&qpf);

		int64_t targetFrameTicks = int64_t(double(qpf.QuadPart) / (bestRefreshRate * (a_useFrameGeneration ? 0.5 : 1.0)));

		LARGE_INTEGER timeNow;
		QueryPerformanceCounter(&timeNow);
		int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
		if (delta < targetFrameTicks) {
			TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
		}
	}

	QueryPerformanceCounter(&lastFrame);
}

void Upscaling::GameFrameLimiter()
{
	double bestRefreshRate = 60.0f;

	LARGE_INTEGER qpf;
	QueryPerformanceFrequency(&qpf);

	int64_t targetFrameTicks = int64_t(double(qpf.QuadPart) / bestRefreshRate);

	static LARGE_INTEGER lastFrame = {};
	LARGE_INTEGER timeNow;
	QueryPerformanceCounter(&timeNow);
	int64_t delta = timeNow.QuadPart - lastFrame.QuadPart;
	if (delta < targetFrameTicks) {
		TimerSleepQPC(lastFrame.QuadPart + targetFrameTicks);
	}
	QueryPerformanceCounter(&lastFrame);	
}

/*
* Copyright (c) 2022-2023 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

double Upscaling::GetRefreshRate(HWND a_window)
{
	HMONITOR monitor = MonitorFromWindow(a_window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfoW(monitor, &info) != 0) {
		// using the CCD get the associated path and display configuration
		UINT32 requiredPaths, requiredModes;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, &requiredModes) == ERROR_SUCCESS) {
			std::vector<DISPLAYCONFIG_PATH_INFO> paths(requiredPaths);
			std::vector<DISPLAYCONFIG_MODE_INFO> modes2(requiredModes);
			if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &requiredPaths, paths.data(), &requiredModes, modes2.data(), nullptr) == ERROR_SUCCESS) {
				// iterate through all the paths until find the exact source to match
				for (auto& p : paths) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
					sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					sourceName.header.size = sizeof(sourceName);
					sourceName.header.adapterId = p.sourceInfo.adapterId;
					sourceName.header.id = p.sourceInfo.id;
					if (DisplayConfigGetDeviceInfo(&sourceName.header) == ERROR_SUCCESS) {
						// find the matched device which is associated with current device
						// there may be the possibility that display may be duplicated and windows may be one of them in such scenario
						// there may be two callback because source is same target will be different
						// as window is on both the display so either selecting either one is ok
						if (wcscmp(info.szDevice, sourceName.viewGdiDeviceName) == 0) {
							// get the refresh rate
							UINT numerator = p.targetInfo.refreshRate.Numerator;
							UINT denominator = p.targetInfo.refreshRate.Denominator;
							return (double)numerator / (double)denominator;
						}
					}
				}
			}
		}
	}
	logger::error("Failed to retrieve refresh rate from swap chain");
	return 60;
}

void Upscaling::PostDisplay()
{
	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();
	auto rendererData = fo4cs::GetRendererData();


	auto& swapChain = rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer];
	ID3D11Resource* swapChainResource = nullptr;
	reinterpret_cast<ID3D11RenderTargetView*>(swapChain.rtView)->GetResource(&swapChainResource);

	auto dx12SwapChain = DX12SwapChain::GetSingleton();

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	const auto frameIndex = dx12SwapChain->frameIndex;
	context->CopyResource(HUDLessBufferShared[frameIndex]->resource.get(), swapChainResource);
	if (swapChainResource)
		swapChainResource->Release();

}

void Upscaling::Reset()
{
	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();

	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	auto dx12SwapChain = DX12SwapChain::GetSingleton();

	FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->ClearRenderTargetView(HUDLessBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	if (uiColorAndAlphaBufferShared[dx12SwapChain->frameIndex])
		context->ClearRenderTargetView(uiColorAndAlphaBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	if (reticleColorAndAlphaBufferShared[dx12SwapChain->frameIndex])
		context->ClearRenderTargetView(reticleColorAndAlphaBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	context->ClearRenderTargetView(depthBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	context->ClearRenderTargetView(motionVectorBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
}

struct WindowSizeChanged
{
	static void thunk(RE::BSGraphics::Renderer*, unsigned int)
	{
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct SetUseDynamicResolutionViewportAsDefaultViewport
{
	static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_true)
	{
		func(This, a_true);
		if (!a_true) {
			Upscaling::GetSingleton()->Upscale();
			Upscaling::GetSingleton()->PostDisplay();
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

bool reticleFix = false;

struct DrawWorld_Forward
{
	static void thunk(void* a1)
	{		
		func(a1);

		if (!reticleFix)
			Upscaling::GetSingleton()->CopyBuffersToSharedResources();

		reticleFix = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct DrawWorld_Reticle
{
	static void thunk(void* a1)
	{
		auto upscaling = Upscaling::GetSingleton();
		upscaling->PreAlpha();
		func(a1);
		reticleFix = true;
		upscaling->PostAlpha();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::InstallHooks()
{
	if (GetSingleton()->pluginMode == PluginMode::kUpscaler)
		InstallUpscalerRenderBackendHooks();

#if defined(FALLOUT_POST_NG)
	stl::detour_thunk<WindowSizeChanged>(REL::ID(2276824));
	stl::write_thunk_call<SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(2318322).address() + 0xC5);
	stl::detour_thunk<DrawWorld_Forward>(REL::ID(2318315));
	stl::write_thunk_call<DrawWorld_Reticle>(REL::ID(2318315).address() + 0x53D);
#else
	// Fix game initialising twice
	stl::detour_thunk<WindowSizeChanged>(REL::ID(212827));

	// Watch frame presentation
	stl::write_thunk_call<SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(587723).address() + 0xE1);

	// Fix reticles on motion vectors and depth
	stl::detour_thunk<DrawWorld_Forward>(REL::ID(656535));
	stl::write_thunk_call<DrawWorld_Reticle>(REL::ID(338205).address() + 0x253);
#endif

	logger::debug("[Upscaler] Installed hooks");
}
