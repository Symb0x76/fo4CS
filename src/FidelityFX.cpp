#include "FidelityFX.h"

#include <algorithm>
#include <array>

#include "RE/CameraData.h"
#include "RE/SingletonAccessors.h"
#include "Upscaler.h"

#include "Diagnostics/HangTrace.h"
#include "DX12SwapChain.h"

#if defined(FALLOUT_PRE_NG)
// FSR 3.0 — D3D11-native combined upscale + frame generation
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/backends/dx11/ffx_dx11.h>

// The DX11 backend implementation exports the resource parameter as non-const,
// while the SDK header declares it const. Declare the exported overload so MSVC
// links the static library symbol instead of the mismatched C declaration.
FfxResource ffxGetResourceDX11(
	ID3D11Resource* dx11Resource,
	FfxResourceDescription ffxResDescription,
	wchar_t const* ffxResName,
	FfxResourceStates state);
#else
// FSR 3.1 — D3D12-interop, separate upscaling + frame generation contexts
#include <dx12/ffx_api_dx12.hpp>
#endif

#if defined(FALLOUT_PRE_NG)
// FSR 3.0 — static linking via ffx_fsr3_x64 + ffx_backend_dx11_x64.
// No runtime DLL loading or stubs needed — the C API is directly linked.

namespace
{
	std::string WideToUtf8(const wchar_t* a_message)
	{
		if (!a_message)
			return {};

		const auto size = WideCharToMultiByte(CP_UTF8, 0, a_message, -1, nullptr, 0, nullptr, nullptr);
		if (size <= 1)
			return {};

		std::string result(static_cast<size_t>(size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, a_message, -1, result.data(), size, nullptr, nullptr);
		result.pop_back();
		return result;
	}

	void FfxMessageCallback(FfxMsgType a_type, const wchar_t* a_message)
	{
		const auto message = WideToUtf8(a_message);
		switch (a_type) {
		case FFX_MESSAGE_TYPE_ERROR:
			logger::error("[FidelityFX] FSR 3.0 SDK: {}", message);
			break;
		case FFX_MESSAGE_TYPE_WARNING:
			logger::warn("[FidelityFX] FSR 3.0 SDK: {}", message);
			break;
		default:
			logger::info("[FidelityFX] FSR 3.0 SDK: {}", message);
			break;
		}
	}

	FfxResource GetFfxResourceDX11(ID3D11Resource* a_resource, FfxResourceStates a_state)
	{
		const auto description = a_resource ? GetFfxResourceDescriptionDX11(a_resource) : FfxResourceDescription{};
		return ffxGetResourceDX11(a_resource, description, nullptr, a_state);
	}

	FfxInterface g_untracedDX11Interface{};

	const wchar_t* GetFfxEffectName(FfxEffect a_effect)
	{
		switch (a_effect) {
		case FFX_EFFECT_FSR3UPSCALER:
			return L"FSR3Upscaler";
		case FFX_EFFECT_OPTICALFLOW:
			return L"OpticalFlow";
		case FFX_EFFECT_FRAMEINTERPOLATION:
			return L"FrameInterpolation";
		default:
			return L"Unknown";
		}
	}

	FfxErrorCode TraceCreateBackendContextDX11(FfxInterface* a_backendInterface, FfxUInt32* a_effectContextId)
	{
		const auto result = g_untracedDX11Interface.fpCreateBackendContext(a_backendInterface, a_effectContextId);
		if (result != FFX_OK) {
			logger::error("[FidelityFX] DX11 backend CreateBackendContext failed (error=0x{:08X})", static_cast<uint32_t>(result));
		}
		return result;
	}

	FfxErrorCode TraceCreateResourceDX11(FfxInterface* a_backendInterface, const FfxCreateResourceDescription* a_desc, FfxUInt32 a_effectContextId, FfxResourceInternal* a_outResource)
	{
		const auto result = g_untracedDX11Interface.fpCreateResource(a_backendInterface, a_desc, a_effectContextId, a_outResource);
		if (result != FFX_OK) {
			const auto name = a_desc && a_desc->name ? a_desc->name : L"<null>";
			const auto& resDesc = a_desc ? a_desc->resourceDescription : FfxResourceDescription{};
			logger::error(
				"[FidelityFX] DX11 backend CreateResource failed (error=0x{:08X}, name={}, type={}, format={}, size={}x{}, mips={}, usage=0x{:X})",
				static_cast<uint32_t>(result),
				WideToUtf8(name),
				static_cast<uint32_t>(resDesc.type),
				static_cast<uint32_t>(resDesc.format),
				resDesc.width,
				resDesc.height,
				resDesc.mipCount,
				static_cast<uint32_t>(resDesc.usage));
		}
		return result;
	}

	FfxErrorCode TraceCreatePipelineDX11(
		FfxInterface* a_backendInterface,
		FfxEffect a_effect,
		FfxPass a_pass,
		uint32_t a_permutationOptions,
		const FfxPipelineDescription* a_pipelineDescription,
		FfxUInt32 a_effectContextId,
		FfxPipelineState* a_outPipeline)
	{
		const auto result = g_untracedDX11Interface.fpCreatePipeline(
			a_backendInterface,
			a_effect,
			a_pass,
			a_permutationOptions,
			a_pipelineDescription,
			a_effectContextId,
			a_outPipeline);
		if (result != FFX_OK) {
			const auto name = a_pipelineDescription && a_pipelineDescription->name ? a_pipelineDescription->name : L"<null>";
			logger::error(
				"[FidelityFX] DX11 backend CreatePipeline failed (error=0x{:08X}, effect={}, pass={}, permutation=0x{:X}, name={})",
				static_cast<uint32_t>(result),
				WideToUtf8(GetFfxEffectName(a_effect)),
				a_pass,
				a_permutationOptions,
				WideToUtf8(name));
		}
		return result;
	}

	void InstallDX11BackendTrace(FfxInterface& a_interface)
	{
		g_untracedDX11Interface = a_interface;
		a_interface.fpCreateBackendContext = TraceCreateBackendContextDX11;
		a_interface.fpCreateResource = TraceCreateResourceDX11;
		a_interface.fpCreatePipeline = TraceCreatePipelineDX11;
	}
}

#else
ffxFunctions ffxModule;

// Stubs: forward the FFX SDK C++ wrapper calls to the runtime-loaded function pointers
extern "C" ffxReturnCode_t ffxCreateContext(ffxContext* context, ffxCreateContextDescHeader* desc, const ffxAllocationCallbacks* memCb)
{
	return ffxModule.CreateContext(context, desc, memCb);
}
extern "C" ffxReturnCode_t ffxDestroyContext(ffxContext* context, const ffxAllocationCallbacks* memCb)
{
	return ffxModule.DestroyContext(context, memCb);
}
extern "C" ffxReturnCode_t ffxConfigure(ffxContext* context, const ffxConfigureDescHeader* desc)
{
	return ffxModule.Configure(context, desc);
}
extern "C" ffxReturnCode_t ffxDispatch(ffxContext* context, const ffxDispatchDescHeader* desc)
{
	return ffxModule.Dispatch(context, desc);
}
extern "C" ffxReturnCode_t ffxQuery(ffxContext* context, ffxQueryDescHeader* desc)
{
	return ffxModule.Query(context, desc);
}
#endif

void FidelityFX::LoadFFX()
{
#if defined(FALLOUT_PRE_NG)
	// FSR 3.0 is statically linked via ffx_fsr3_x64 + ffx_backend_dx11_x64.
	// No runtime DLL loading needed — the D3D11 backend is compiled in.
	featureFSR = !fsr3Unavailable;
	logger::info("[FidelityFX] FSR 3.0 D3D11-native backend active (statically linked)");
#else
	struct RuntimePath
	{
		const wchar_t* path;
		const char* label;
	};
	static constexpr std::array<RuntimePath, 2> runtimePaths{ {
		{ L"Data\\F4SE\\Plugins\\FidelityFX\\amd_fidelityfx_dx12.dll", "shared" },
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
#endif
}

void FidelityFX::SetupFrameGeneration()
{
	featureFrameGen = false;

#if defined(FALLOUT_PRE_NG)
	// FSR 3.0 handles frame generation via the combined upscale dispatch.
	// No separate FG context needed — featureFrameGen is set when the FSR 3.0
	// context is created in CreateFSR3Context().
	if (fsr3Initialized)
		featureFrameGen = true;
	return;
#else
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

	ffx::CreateContextDescFrameGenerationVersion createFgVersion{};
	createFgVersion.version = FFX_FRAMEGENERATION_VERSION;

	ffx::CreateBackendDX12Desc createBackend{};
	createBackend.device = dx12SwapChain->d3d12Device.get();

	if (ffx::CreateContext(frameGenContext, nullptr, createFg, createFgVersion, createBackend) != ffx::ReturnCode::Ok) {
		logger::error("[FidelityFX] Failed to create frame generation context");
		frameGenContext = nullptr;
		return;
	}

	featureFrameGen = true;
#endif
}

#if !defined(FALLOUT_PRE_NG)
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

	dispatchUpscale.cameraNear = fo4cs::RE::GetCameraNear();
	dispatchUpscale.cameraFar = fo4cs::RE::GetCameraFar();

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

void FidelityFX::Present(bool a_useFrameGen)
{
	if (!featureFrameGen || frameGenContext == nullptr)
	return;

	auto upscaling = Upscaling::GetSingleton();
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	auto commandList = dx12SwapChain->commandLists[dx12SwapChain->frameIndex].get();
	
	auto HUDLessColor = upscaling->HUDLessBufferShared12[dx12SwapChain->frameIndex].get();
	auto depth = upscaling->depthBufferShared12[dx12SwapChain->frameIndex].get();
	auto motionVectors = upscaling->motionVectorBufferShared12[dx12SwapChain->frameIndex].get();
	const bool canUseFrameGen = a_useFrameGen &&
		commandList &&
		HUDLessColor &&
		depth &&
		motionVectors;

	if (a_useFrameGen && !canUseFrameGen) {
		static bool loggedMissingResources = false;
		if (!loggedMissingResources) {
			logger::warn("[FidelityFX] Frame generation resources are not ready; skipping generated frames");
			loggedMissingResources = true;
		}
	}

	ffx::ConfigureDescFrameGeneration configParameters{};

	if (canUseFrameGen) {
		configParameters.frameGenerationEnabled = true;

		configParameters.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
			try {
				return ffxModule.Dispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
			} catch (...) {
				return FFX_API_RETURN_ERROR;
			}
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

	if (canUseFrameGen) {
		static int fgFailuresSinceLastSuccess = 0;
		static constexpr int kMaxConsecutiveFrameGenFailures = 5;
		ffx::DispatchDescFrameGenerationPrepareV2 dispatchParameters{};

		dispatchParameters.commandList = commandList;

		static auto gameViewport = fo4cs::RE::GetGraphicsState();
			static auto renderTargetManager = fo4cs::RE::GetRenderTargetManager();

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

	dispatchParameters.cameraNear = fo4cs::RE::GetCameraNear();
	dispatchParameters.cameraFar = fo4cs::RE::GetCameraFar();

		dispatchParameters.cameraFovAngleVertical = 1.0f;
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;

		dispatchParameters.frameID = frameID;

		dispatchParameters.depth = ffxApiGetResourceDX12(depth);
		dispatchParameters.motionVectors = ffxApiGetResourceDX12(motionVectors);

		static bool wasFrameGenActive = false;
		const bool frameGenJustResumed = canUseFrameGen && !wasFrameGenActive;
		wasFrameGenActive = canUseFrameGen;
		dispatchParameters.reset = frameGenJustResumed;

		dispatchParameters.cameraPosition[0] = 0.0f; dispatchParameters.cameraPosition[1] = 0.0f; dispatchParameters.cameraPosition[2] = 0.0f;
		dispatchParameters.cameraUp[0] = 0.0f; dispatchParameters.cameraUp[1] = 1.0f; dispatchParameters.cameraUp[2] = 0.0f;
		dispatchParameters.cameraRight[0] = 1.0f; dispatchParameters.cameraRight[1] = 0.0f; dispatchParameters.cameraRight[2] = 0.0f;
		dispatchParameters.cameraForward[0] = 0.0f; dispatchParameters.cameraForward[1] = 0.0f; dispatchParameters.cameraForward[2] = 1.0f;


		if (dispatchParameters.renderSize.width > 0 && dispatchParameters.renderSize.height > 0) {
			ffx::ReturnCode dispatchResult = ffx::Dispatch(frameGenContext, dispatchParameters);
			if (dispatchResult != ffx::ReturnCode::Ok) {
				fgFailuresSinceLastSuccess++;

				if (fgFailuresSinceLastSuccess == 1 || fgFailuresSinceLastSuccess % 300 == 0) {
					logger::critical("[FidelityFX] Failed to dispatch frame generation! Error: {}, consecutive failures: {}",
						static_cast<uint32_t>(dispatchResult), fgFailuresSinceLastSuccess);
				}

				ffx::ConfigureDescFrameGeneration disableConfig{};
				disableConfig.frameGenerationEnabled = false;
				disableConfig.frameGenerationCallbackUserContext = nullptr;
				disableConfig.frameGenerationCallback = nullptr;
				disableConfig.HUDLessColor = FfxApiResource({});
				disableConfig.swapChain = dx12SwapChain->swapChain;
				ffx::Configure(frameGenContext, disableConfig);

				if (fgFailuresSinceLastSuccess >= kMaxConsecutiveFrameGenFailures) {
					featureFrameGen = false;
					logger::error("[FidelityFX] Disabling FSR frame generation after {} consecutive dispatch failures; last error={}",
						fgFailuresSinceLastSuccess, static_cast<uint32_t>(dispatchResult));
				}
			} else {
				if (fgFailuresSinceLastSuccess > 0) {
					logger::info("[FidelityFX] Frame generation dispatch recovered after {} failures",
						fgFailuresSinceLastSuccess);
				}
				fgFailuresSinceLastSuccess = 0;
			}
		}
	}

	frameID++;
}

#endif // !FALLOUT_PRE_NG

#if defined(FALLOUT_PRE_NG)

void FidelityFX::CreateFSR3Context(
	ID3D11Device* a_device,
	uint32_t a_maxRenderWidth, uint32_t a_maxRenderHeight,
	uint32_t a_outputWidth, uint32_t a_outputHeight)
{
	fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:CreateFSR3Context:enter");
	if (fsr3Initialized) {
		fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:CreateFSR3Context:exit:already-initialized");
		return;
	}

	if (fsr3Unavailable) {
		featureFSR = false;
		featureFrameGen = false;
		fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:CreateFSR3Context:exit:unavailable");
		return;
	}

	featureFSR = false;
	featureFrameGen = false;

	fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:ffxGetDeviceDX11:begin");
	auto fsrDevice = ffxGetDeviceDX11(a_device);
	fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:ffxGetDeviceDX11:end");

	fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:ffxGetScratchMemorySizeDX11:begin");
	const size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(FFX_FSR3_CONTEXT_COUNT);
	fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:ffxGetScratchMemorySizeDX11:end");

	auto createWithFlags = [&](uint32_t a_flags, const char* a_label) -> FfxErrorCode {
		fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:calloc-scratch:begin");
		void* scratchBuffer = calloc(scratchBufferSize, 1);
		fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:calloc-scratch:end");
		if (!scratchBuffer) {
			logger::critical("[FidelityFX] FSR 3.0: Failed to allocate scratch buffer (size={})", scratchBufferSize);
			return FFX_ERROR_OUT_OF_MEMORY;
		}
		memset(scratchBuffer, 0, scratchBufferSize);

		FfxInterface fsrInterface{};
		fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:ffxGetInterfaceDX11:begin");
		const auto interfaceResult = ffxGetInterfaceDX11(&fsrInterface, fsrDevice, scratchBuffer, scratchBufferSize, FFX_FSR3_CONTEXT_COUNT);
		fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:ffxGetInterfaceDX11:end");
		if (interfaceResult != FFX_OK) {
			logger::critical(
				"[FidelityFX] FSR 3.0: Failed to get D3D11 backend interface (error=0x{:08X}, scratchSize={}, contexts={})",
				static_cast<uint32_t>(interfaceResult),
				scratchBufferSize,
				FFX_FSR3_CONTEXT_COUNT);
			free(scratchBuffer);
			return interfaceResult;
		}
		InstallDX11BackendTrace(fsrInterface);

		FfxFsr3ContextDescription contextDesc{};
		contextDesc.flags = a_flags;
		contextDesc.maxRenderSize = { a_maxRenderWidth, a_maxRenderHeight };
		contextDesc.maxUpscaleSize = { a_outputWidth, a_outputHeight };
		contextDesc.displaySize = { a_outputWidth, a_outputHeight };
		contextDesc.backendInterfaceSharedResources = fsrInterface;
		contextDesc.backendInterfaceUpscaling = fsrInterface;
		contextDesc.fpMessage = FfxMessageCallback;

		logger::info(
			"[FidelityFX] FSR 3.0 context create input: mode={}, maxRender={}x{}, output={}x{}, flags=0x{:X}, scratchSize={}, contexts={}",
			a_label,
			a_maxRenderWidth,
			a_maxRenderHeight,
			a_outputWidth,
			a_outputHeight,
			contextDesc.flags,
			scratchBufferSize,
			FFX_FSR3_CONTEXT_COUNT);

		fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:ffxFsr3ContextCreate:begin");
		const auto createResult = ffxFsr3ContextCreate(&fsr3Context, &contextDesc);
		fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:ffxFsr3ContextCreate:end");
		if (createResult == FFX_OK) {
			fsr3ScratchBuffer = scratchBuffer;
			return FFX_OK;
		}

		free(scratchBuffer);
		return createResult;
	};

	auto createResult = createWithFlags(
		FFX_FSR3_ENABLE_AUTO_EXPOSURE |
			FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE |
			FFX_FSR3_ENABLE_UPSCALING_ONLY,
		"sample-upscaling-only");
	if (createResult != FFX_OK) {
		logger::warn(
			"[FidelityFX] FSR 3.0 HDR upscaling-only context failed (error=0x{:08X}); retrying SDR upscaling-only",
			static_cast<uint32_t>(createResult));
		memset(&fsr3Context, 0, sizeof(fsr3Context));
		createResult = createWithFlags(
			FFX_FSR3_ENABLE_AUTO_EXPOSURE |
				FFX_FSR3_ENABLE_UPSCALING_ONLY,
			"sdr-upscaling-only");
	}
	if (createResult != FFX_OK) {
		logger::critical("[FidelityFX] FSR 3.0: Failed to create context (error=0x{:08X})", static_cast<uint32_t>(createResult));
		fsr3ScratchBuffer = nullptr;
		featureFSR = false;
		featureFrameGen = false;
		fsr3Unavailable = true;
		fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:CreateFSR3Context:exit:create-failed");
		return;
	}

	fsr3Initialized = true;
	featureFSR = true;
	featureFrameGen = false;
	logger::info("[FidelityFX] FSR 3.0 D3D11 upscaling context created ({}x{} -> {}x{})",
		a_maxRenderWidth, a_maxRenderHeight, a_outputWidth, a_outputHeight);
	fo4cs::Diagnostics::WriteHangTraceLine("FidelityFX:CreateFSR3Context:exit:success");
}

void FidelityFX::DestroyFSR3Context()
{
	if (fsr3Initialized) {
		ffxFsr3ContextDestroy(&fsr3Context);
		free(fsr3ScratchBuffer);
		fsr3ScratchBuffer = nullptr;
		fsr3Initialized = false;
	}
	featureFrameGen = false;
	featureFSR = false;
}

bool FidelityFX::Upscale(
	ID3D11DeviceContext* a_context,
	ID3D11Texture2D* a_color,
	ID3D11Texture2D* a_output,
	ID3D11Texture2D* a_depth,
	ID3D11Texture2D* a_motionVectors,
	float2 a_jitter,
	float2 a_renderSize,
	float2 a_displaySize,
	uint a_qualityMode)
{
	(void)a_qualityMode;
	if (!fsr3Initialized || !a_context || !a_color || !a_output || !a_depth || !a_motionVectors)
		return false;

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
	float deltaTime = static_cast<float>(currentFrameTime.QuadPart - lastFrameTime.QuadPart)
		/ static_cast<float>(frequency.QuadPart);
	lastFrameTime = currentFrameTime;

	FfxFsr3DispatchUpscaleDescription dispatch{};
	dispatch.commandList = ffxGetCommandListDX11(a_context);
	dispatch.color = GetFfxResourceDX11(a_color, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatch.depth = GetFfxResourceDX11(a_depth, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatch.motionVectors = GetFfxResourceDX11(a_motionVectors, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatch.exposure = GetFfxResourceDX11(nullptr, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatch.reactive = GetFfxResourceDX11(nullptr, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatch.transparencyAndComposition = GetFfxResourceDX11(nullptr, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatch.upscaleOutput = GetFfxResourceDX11(a_output, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	dispatch.jitterOffset.x = -a_jitter.x;
	dispatch.jitterOffset.y = -a_jitter.y;
	dispatch.motionVectorScale.x = a_renderSize.x;
	dispatch.motionVectorScale.y = a_renderSize.y;
	dispatch.renderSize.width = static_cast<uint32_t>(a_renderSize.x);
	dispatch.renderSize.height = static_cast<uint32_t>(a_renderSize.y);
	dispatch.upscaleSize.width = static_cast<uint32_t>(a_displaySize.x);
	dispatch.upscaleSize.height = static_cast<uint32_t>(a_displaySize.y);

	dispatch.enableSharpening = false;
	dispatch.sharpness = 0.0f;
	dispatch.frameTimeDelta = std::max(deltaTime * 1000.f, 0.0f);
	dispatch.preExposure = 1.0f;
	dispatch.reset = false;

	dispatch.cameraNear = fo4cs::RE::GetCameraNear();
	dispatch.cameraFar = fo4cs::RE::GetCameraFar();
	dispatch.cameraFovAngleVertical = 1.0f;
	dispatch.viewSpaceToMetersFactor = 0.01428222656f;
	dispatch.flags = 0;
	dispatch.frameID = 0;

	if (ffxFsr3ContextDispatchUpscale(&fsr3Context, &dispatch) != FFX_OK) {
		logger::error("[FidelityFX] FSR 3.0 dispatch failed");
		return false;
	}

	return true;
}

void FidelityFX::DestroyUpscaling()
{
	DestroyFSR3Context();
}

void FidelityFX::Present(bool /*a_useFrameGeneration*/)
{
	// FSR 3.0 handles frame generation within the upscale dispatch.
	// No separate Present-time FG dispatch needed.
}

#endif // FALLOUT_PRE_NG
