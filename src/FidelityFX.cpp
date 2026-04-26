#include "FidelityFX.h"

#include <algorithm>
#include <array>

#include "Upscaler.h"

#include "DX12SwapChain.h"
#include <dx12/ffx_api_dx12.hpp>

ffxFunctions ffxModule;

void FidelityFX::LoadFFX()
{
	struct RuntimePath
	{
		const wchar_t* path;
		const char* label;
	};
	static constexpr std::array<RuntimePath, 2> runtimePaths{ {
		{ L"Data\\F4SE\\Plugins\\FidelityFX\\amd_fidelityfx_dx12.dll", "shared" },
		{ L"Data\\F4SE\\Plugins\\FrameGeneration\\FidelityFX\\amd_fidelityfx_dx12.dll", "legacy FrameGeneration" }
	} };

	for (const auto& runtimePath : runtimePaths) {
		module = LoadLibrary(runtimePath.path);
		if (module) {
			logger::info("[FidelityFX] Loaded {} runtime", runtimePath.label);
			break;
		}
	}

	if (module) {
		ffxLoadFunctions(&ffxModule, module);
		featureFSR = true;
	} else {
		logger::warn("[FidelityFX] amd_fidelityfx_dx12.dll is not loaded");
	}
}

void FidelityFX::SetupFrameGeneration()
{
	featureFrameGen = false;

	if (!module) {
		logger::warn("[FidelityFX] Runtime is not loaded, skipping frame generation context");
		return;
	}

	auto dx12SwapChain = DX12SwapChain::GetSingleton();

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { dx12SwapChain->swapChainDesc.Width, dx12SwapChain->swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
	createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(dx12SwapChain->swapChainDesc.Format);

	ffx::CreateBackendDX12Desc createBackend{};
	createBackend.device = dx12SwapChain->d3d12Device.get();

	if (ffx::CreateContext(frameGenContext, nullptr, createFg, createBackend) != ffx::ReturnCode::Ok) {
		logger::error("[FidelityFX] Failed to create frame generation context");
		frameGenContext = nullptr;
		return;
	}

	featureFrameGen = true;
}

bool FidelityFX::SetupUpscaling(ID3D12Device* a_device, uint32_t a_maxRenderWidth, uint32_t a_maxRenderHeight, uint32_t a_outputWidth, uint32_t a_outputHeight)
{
	if (!module || !a_device)
		return false;

	const auto maxRenderWidth = std::max(1u, a_maxRenderWidth);
	const auto maxRenderHeight = std::max(1u, a_maxRenderHeight);
	const auto outputWidth = std::max(1u, a_outputWidth);
	const auto outputHeight = std::max(1u, a_outputHeight);

	if (upscaleContext != nullptr &&
		upscaleMaxRenderWidth == maxRenderWidth &&
		upscaleMaxRenderHeight == maxRenderHeight &&
		upscaleMaxOutputWidth == outputWidth &&
		upscaleMaxOutputHeight == outputHeight) {
		return true;
	}

	DestroyUpscaling();

	ffx::CreateContextDescUpscale createUpscale{};
	createUpscale.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION | FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
	createUpscale.maxRenderSize = { maxRenderWidth, maxRenderHeight };
	createUpscale.maxUpscaleSize = { outputWidth, outputHeight };
	createUpscale.fpMessage = nullptr;

	ffx::CreateBackendDX12Desc backendDesc{};
	backendDesc.device = a_device;

	if (ffx::CreateContext(upscaleContext, nullptr, createUpscale, backendDesc) != ffx::ReturnCode::Ok) {
		logger::error("[FidelityFX] Failed to create upscaler context");
		upscaleContext = nullptr;
		return false;
	}

	upscaleMaxRenderWidth = maxRenderWidth;
	upscaleMaxRenderHeight = maxRenderHeight;
	upscaleMaxOutputWidth = outputWidth;
	upscaleMaxOutputHeight = outputHeight;
	return true;
}

bool FidelityFX::Upscale(
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
	if (!featureFSR || !upscaleContext || !a_commandList || !a_color || !a_output || !a_depth || !a_motionVectors)
		return false;

	ffx::DispatchDescUpscale dispatchUpscale{};
	dispatchUpscale.commandList = a_commandList;
	dispatchUpscale.color = ffxApiGetResourceDX12(a_color, FFX_API_RESOURCE_STATE_COMMON);
	dispatchUpscale.depth = ffxApiGetResourceDX12(a_depth, FFX_API_RESOURCE_STATE_COMMON);
	dispatchUpscale.motionVectors = ffxApiGetResourceDX12(a_motionVectors, FFX_API_RESOURCE_STATE_COMMON);
	dispatchUpscale.exposure = FfxApiResource({});
	dispatchUpscale.reactive = FfxApiResource({});
	dispatchUpscale.transparencyAndComposition = FfxApiResource({});
	dispatchUpscale.output = ffxApiGetResourceDX12(a_output, FFX_API_RESOURCE_STATE_COMMON);
	dispatchUpscale.jitterOffset.x = -a_jitter.x;
	dispatchUpscale.jitterOffset.y = -a_jitter.y;
	dispatchUpscale.motionVectorScale.x = a_renderSize.x;
	dispatchUpscale.motionVectorScale.y = a_renderSize.y;
	dispatchUpscale.renderSize.width = std::max(1u, static_cast<uint32_t>(a_renderSize.x));
	dispatchUpscale.renderSize.height = std::max(1u, static_cast<uint32_t>(a_renderSize.y));
	dispatchUpscale.upscaleSize.width = std::max(1u, static_cast<uint32_t>(a_displaySize.x));
	dispatchUpscale.upscaleSize.height = std::max(1u, static_cast<uint32_t>(a_displaySize.y));
	dispatchUpscale.enableSharpening = false;
	dispatchUpscale.sharpness = 0.0f;
	static LARGE_INTEGER frequency = []() {
		LARGE_INTEGER freq{};
		QueryPerformanceFrequency(&freq);
		return freq;
	}();
	static LARGE_INTEGER lastFrameTime = []() {
		LARGE_INTEGER time{};
		QueryPerformanceCounter(&time);
		return time;
	}();
	LARGE_INTEGER currentFrameTime{};
	QueryPerformanceCounter(&currentFrameTime);
	dispatchUpscale.frameTimeDelta = std::max(
		static_cast<float>(currentFrameTime.QuadPart - lastFrameTime.QuadPart) * 1000.0f / static_cast<float>(frequency.QuadPart),
		0.0f);
	lastFrameTime = currentFrameTime;
	dispatchUpscale.preExposure = 1.0f;
	dispatchUpscale.reset = false;

#if defined(FALLOUT_POST_NG)
	dispatchUpscale.cameraNear = *(float*)REL::ID(2712882).address();
	dispatchUpscale.cameraFar = *(float*)REL::ID(2712883).address();
#else
	dispatchUpscale.cameraNear = *(float*)REL::ID(57985).address();
	dispatchUpscale.cameraFar = *(float*)REL::ID(958877).address();
#endif

	dispatchUpscale.cameraFovAngleVertical = 1.0f;
	dispatchUpscale.viewSpaceToMetersFactor = 0.01428222656f;
	dispatchUpscale.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;

	switch (a_qualityMode) {
	case 0:
		dispatchUpscale.flags = 0;
		break;
	default:
		break;
	}

	if (ffx::Dispatch(upscaleContext, dispatchUpscale) != ffx::ReturnCode::Ok) {
		logger::error("[FidelityFX] Failed to dispatch upscaling");
		return false;
	}

	return true;
}

void FidelityFX::DestroyUpscaling()
{
	if (upscaleContext != nullptr) {
		ffx::DestroyContext(upscaleContext);
		upscaleContext = nullptr;
	}

	upscaleMaxRenderWidth = 0;
	upscaleMaxRenderHeight = 0;
	upscaleMaxOutputWidth = 0;
	upscaleMaxOutputHeight = 0;
}

[[nodiscard]] static RE::BSGraphics::State* State_GetSingleton()
{
#if defined(FALLOUT_POST_NG)
	REL::Relocation<RE::BSGraphics::State*> singleton{ REL::ID(2704621) };
#else
	REL::Relocation<RE::BSGraphics::State*> singleton{ REL::ID(600795) };
#endif
	return singleton.get();
}


[[nodiscard]] static RE::BSGraphics::RenderTargetManager* RenderTargetManager_GetSingleton()
{
#if defined(FALLOUT_POST_NG)
	REL::Relocation<RE::BSGraphics::RenderTargetManager*> singleton{ REL::ID(2666735) };
#else
	REL::Relocation<RE::BSGraphics::RenderTargetManager*> singleton{ REL::ID(1508457) };
#endif
	return singleton.get();
}

void FidelityFX::Present(bool a_useFrameGeneration)
{
	if (!featureFrameGen || frameGenContext == nullptr)
		return;

	auto upscaling = Upscaling::GetSingleton();
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	auto commandList = dx12SwapChain->commandLists[dx12SwapChain->frameIndex].get();
	
	auto HUDLessColor = upscaling->HUDLessBufferShared12[dx12SwapChain->frameIndex].get();
	auto depth = upscaling->depthBufferShared12[dx12SwapChain->frameIndex].get();
	auto motionVectors = upscaling->motionVectorBufferShared12[dx12SwapChain->frameIndex].get();
	const bool canUseFrameGeneration = a_useFrameGeneration &&
		commandList &&
		HUDLessColor &&
		depth &&
		motionVectors;

	if (a_useFrameGeneration && !canUseFrameGeneration) {
		static bool loggedMissingResources = false;
		if (!loggedMissingResources) {
			logger::warn("[FidelityFX] Frame generation resources are not ready; skipping generated frames");
			loggedMissingResources = true;
		}
	}

	ffx::ConfigureDescFrameGeneration configParameters{};

	if (canUseFrameGeneration) {
		configParameters.frameGenerationEnabled = true;

		configParameters.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
			return ffxModule.Dispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
			};
		configParameters.frameGenerationCallbackUserContext = &frameGenContext;

		configParameters.HUDLessColor = ffxApiGetResourceDX12(HUDLessColor);

	}
	else {
		configParameters.frameGenerationEnabled = false;

		configParameters.frameGenerationCallbackUserContext = nullptr;
		configParameters.frameGenerationCallback = nullptr;

		configParameters.HUDLessColor = FfxApiResource({});
	}

	configParameters.presentCallback = nullptr;
	configParameters.presentCallbackUserContext = nullptr;

	static uint64_t frameID = 0;
	configParameters.frameID = frameID;
	configParameters.swapChain = dx12SwapChain->swapChain;
	configParameters.onlyPresentGenerated = false;
	configParameters.allowAsyncWorkloads = true;
	configParameters.flags = 0;

	configParameters.generationRect.left = (dx12SwapChain->swapChainDesc.Width - dx12SwapChain->swapChainDesc.Width) / 2;
	configParameters.generationRect.top = (dx12SwapChain->swapChainDesc.Height - dx12SwapChain->swapChainDesc.Height) / 2;
	configParameters.generationRect.width = dx12SwapChain->swapChainDesc.Width;
	configParameters.generationRect.height = dx12SwapChain->swapChainDesc.Height;

	if (ffx::Configure(frameGenContext, configParameters) != ffx::ReturnCode::Ok) {
		logger::critical("[FidelityFX] Failed to configure frame generation!");
	}

	static LARGE_INTEGER frequency = []() {
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		return freq;
		}();

	static LARGE_INTEGER lastFrameTime = []() {
		LARGE_INTEGER time;
		QueryPerformanceCounter(&time);
		return time;
		}();

	LARGE_INTEGER currentFrameTime;
	QueryPerformanceCounter(&currentFrameTime);

	float deltaTime = static_cast<float>(currentFrameTime.QuadPart - lastFrameTime.QuadPart) / static_cast<float>(frequency.QuadPart);
	
	lastFrameTime = currentFrameTime;

	if (canUseFrameGeneration) {
		ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

		dispatchParameters.commandList = commandList;

		static auto gameViewport = State_GetSingleton();
		static auto renderTargetManager = RenderTargetManager_GetSingleton();

		auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
		auto renderSize = float2(screenSize.x * renderTargetManager->dynamicWidthRatio, screenSize.y * renderTargetManager->dynamicHeightRatio);

		dispatchParameters.motionVectorScale.x = renderSize.x;
		dispatchParameters.motionVectorScale.y = renderSize.y;
		dispatchParameters.renderSize.width = static_cast<uint>(renderSize.x);
		dispatchParameters.renderSize.height = static_cast<uint>(renderSize.y);
		
		float2 jitter;
		jitter.x = -gameViewport->offsetX * screenSize.x / 2.0f;
		jitter.y = gameViewport->offsetY * screenSize.y / 2.0f;

		dispatchParameters.jitterOffset.x = -jitter.x / renderTargetManager->dynamicWidthRatio;
		dispatchParameters.jitterOffset.y = -jitter.y / renderTargetManager->dynamicHeightRatio;

		dispatchParameters.frameTimeDelta = deltaTime * 1000.f;

#if defined(FALLOUT_POST_NG)
		dispatchParameters.cameraNear = *(float*)REL::ID(2712882).address();
		dispatchParameters.cameraFar = *(float*)REL::ID(2712883).address();
#else
		dispatchParameters.cameraNear = *(float*)REL::ID(57985).address();
		dispatchParameters.cameraFar = *(float*)REL::ID(958877).address();
#endif

		dispatchParameters.cameraFovAngleVertical = 1.0f;
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;

		dispatchParameters.frameID = frameID;

		dispatchParameters.depth = ffxApiGetResourceDX12(depth);
		dispatchParameters.motionVectors = ffxApiGetResourceDX12(motionVectors);

		if (ffx::Dispatch(frameGenContext, dispatchParameters) != ffx::ReturnCode::Ok) {
			logger::critical("[FidelityFX] Failed to dispatch frame generation!");
		}
	}

	frameID++;
}
