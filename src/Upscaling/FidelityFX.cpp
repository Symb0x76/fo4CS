#include "Upscaling/FidelityFX.h"

#include <algorithm>
#include <array>

#include "Platform/RE/CameraData.h"
#include "Platform/RE/SingletonAccessors.h"
#include "Upscaling/Upscaler.h"

#include "Diagnostics/HangTrace.h"
#include "Render/DX12SwapChain.h"

#include <dx12/ffx_api_dx12.hpp>

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

// Target is full FSR 3.1 support against the FidelityFX SDK v1.1.4 runtime.
//
// extern/FidelityFX-SDK is pinned to v1.1.4 and include/ carries that tag's ffx-api
// headers verbatim, so what we compile against is what amd_fidelityfx_dx12.dll below
// actually implements. That pairing is deliberate: the headers previously came from the
// FSR 4.x API (FrameGeneration 4.0.0 / Upscaler 4.1.0) while the shipped runtime was
// 1.1.x, and reading the newer source to explain older runtime behaviour cost several
// rounds of misdiagnosis -- v1.1.x rejects a hudless format outside the backbuffer's
// precision group unconditionally, where 2.3.0 moved that same check behind
// FFX_FRAMEINTERPOLATION_ENABLE_DEBUG_CHECKING. Keep the three in step: submodule tag,
// include/ headers, and the packaged DLL.
//
// TODO(future contributor): FSR 4 support is out of scope. It is not a DLL swap -- SDK
// 2.x drops the monolithic amd_fidelityfx_dx12.dll for amd_fidelityfx_loader_dx12.dll
// plus per-effect modules (framegeneration, upscaler, ~65 MB), so the load below and the
// packaging step both change. Note the FSR 4 upscaler is RDNA-gated, so a fallback path
// is needed for non-AMD hardware.
void FidelityFX::LoadFFX()
{
	struct RuntimePath
	{
		const wchar_t* path;
		const char* label;
	};
	static constexpr std::array<RuntimePath, 1> runtimePaths{ {
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
}

void FidelityFX::DestroyFrameGeneration()
{
	if (frameGenContext != nullptr) {
		ffx::DestroyContext(frameGenContext);
		frameGenContext = nullptr;
	}

	featureFrameGen = false;
	frameGenEnabled = false;
	frameGenHudlessFormat = DXGI_FORMAT_UNKNOWN;
}

void FidelityFX::SetupFrameGeneration(DXGI_FORMAT a_hudlessFormat)
{
	featureFrameGen = false;
	frameGenEnabled = false;

	if (!module) {
		logger::warn("[FidelityFX] Runtime is not loaded, skipping frame generation context");
		return;
	}

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto backBufferFormat = dx12SwapChain->swapChainDesc.Format;

	// FFX sizes and formats its internal hudless-path resources when the context is
	// created, from backBufferFormat alone. It only learns that HUDLessColor uses a
	// different format if ffxCreateContextDescFrameGenerationHudless is chained onto
	// the create call. Without it, handing frame generation a HUDLess buffer whose
	// format disagrees with the backbuffer is fatal: the first dispatch mismatches
	// what FFX planned for and D3D12 removes the device, with
	// GetDeviceRemovedReason() reporting DXGI_ERROR_INVALID_CALL.
	//
	// #21 is what made this reachable, though not in the way its comment claimed. It
	// meant to make the buffer follow kFrameBuffer, but probed that target through
	// RenderTarget::texture, which is null for the swap-chain-backed slot. The probe
	// never fired and the buffer fell through to kMain's R11G11B10_FLOAT instead --
	// group 5 against the backbuffer's group 2. kFrameBuffer is in fact the backbuffer
	// at the backbuffer format, so the intended behaviour would have been correct.
	//
	// CreateFrameGenerationResources now takes the format from
	// dx12SwapChain->swapChainDesc.Format, the same field backBufferFormat is read from
	// just above, so on this path hudlessFormat == backBufferFormat by construction and
	// the descriptor below is never chained.
	//
	// The chaining stays as a net for any future source that genuinely diverges, and it
	// fails safe: the shipped amd_fidelityfx_dx12.dll is built from FFX v1.1.x, whose
	// frameinterpolationCreate() rejects a hudless format in a different precision group
	// with FFX_API_RETURN_ERROR_RUNTIME_ERROR (3). That leaves generation off rather than
	// removing the device. Note the check is unconditional in v1.1.x but sits behind
	// FFX_FRAMEINTERPOLATION_ENABLE_DEBUG_CHECKING in the v2.3.0 source vendored under
	// extern/FidelityFX-SDK, so the vendored tree does not describe the runtime we ship.
	const DXGI_FORMAT hudlessFormat =
		a_hudlessFormat == DXGI_FORMAT_UNKNOWN ? backBufferFormat : a_hudlessFormat;
	const bool hudlessFormatDiffers = hudlessFormat != backBufferFormat;

	ffx::CreateContextDescFrameGeneration createFg{};
	createFg.displaySize = { dx12SwapChain->swapChainDesc.Width, dx12SwapChain->swapChainDesc.Height };
	createFg.maxRenderSize = createFg.displaySize;
	createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
	createFg.backBufferFormat = ffxApiGetSurfaceFormatDX12(backBufferFormat);

	ffx::CreateContextDescFrameGenerationHudless createHudless{};
	createHudless.hudlessBackBufferFormat = ffxApiGetSurfaceFormatDX12(hudlessFormat);

	ffx::CreateBackendDX12Desc createBackend{};
	createBackend.device = dx12SwapChain->d3d12Device.get();

	// Chain the descriptors by hand rather than through ffx::CreateContext.
	//
	// v1.1.4's ffx::LinkHeaders declares its recursive case before the one- and
	// two-argument bases, so under two-phase lookup the recursive call sees only itself:
	// a three-descriptor chain recurses down to a one-argument call that has no
	// candidate and fails to compile. Two descriptors work because the call site sees
	// every overload. Linking here keeps include/ byte-identical to the SDK tag we ship
	// instead of patching a vendored header to get a third descriptor through.
	createFg.header.pNext = nullptr;
	createHudless.header.pNext = nullptr;
	createBackend.header.pNext = nullptr;

	if (hudlessFormatDiffers) {
		createFg.header.pNext = &createHudless.header;
		createHudless.header.pNext = &createBackend.header;
	} else {
		createFg.header.pNext = &createBackend.header;
	}

	const auto createResult = static_cast<ffx::ReturnCode>(
		ffxCreateContext(&frameGenContext, &createFg.header, nullptr));

	if (createResult != ffx::ReturnCode::Ok) {
		// Deliberately no retry without the hudless descriptor. That configuration is
		// the one known to remove the device, so falling back to it would trade a
		// disabled feature for a frozen game.
		logger::error("[FidelityFX] Failed to create frame generation context (error={}, backbuffer fmt={}, hudless fmt={}, hudlessDescChained={})",
			static_cast<uint32_t>(createResult),
			static_cast<uint32_t>(backBufferFormat),
			static_cast<uint32_t>(hudlessFormat),
			hudlessFormatDiffers);
		frameGenContext = nullptr;
		frameGenHudlessFormat = DXGI_FORMAT_UNKNOWN;
		return;
	}

	featureFrameGen = true;
	frameGenHudlessFormat = hudlessFormat;
	logger::info("[FidelityFX] FSR frame generation context created (display={}x{}, fmt={}, hudless fmt={}, hudlessDescChained={})",
		createFg.displaySize.width,
		createFg.displaySize.height,
		static_cast<uint32_t>(backBufferFormat),
		static_cast<uint32_t>(hudlessFormat),
		hudlessFormatDiffers);
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
	upscaleLastRenderWidth = 0;
	upscaleLastRenderHeight = 0;
	upscaleLastOutputWidth = 0;
	upscaleLastOutputHeight = 0;
	upscaleLastQualityMode = 0xFFFFFFFFu;
	upscaleNeedsReset = true;
	logger::info("[FidelityFX] FSR upscaler context created (maxRender={}x{}, output={}x{})",
		maxRenderWidth,
		maxRenderHeight,
		outputWidth,
		outputHeight);
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
	const bool resetHistory =
		upscaleNeedsReset ||
		upscaleLastRenderWidth != dispatchUpscale.renderSize.width ||
		upscaleLastRenderHeight != dispatchUpscale.renderSize.height ||
		upscaleLastOutputWidth != dispatchUpscale.upscaleSize.width ||
		upscaleLastOutputHeight != dispatchUpscale.upscaleSize.height ||
		upscaleLastQualityMode != a_qualityMode;
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
	dispatchUpscale.reset = resetHistory;

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

	upscaleNeedsReset = false;
	upscaleLastRenderWidth = dispatchUpscale.renderSize.width;
	upscaleLastRenderHeight = dispatchUpscale.renderSize.height;
	upscaleLastOutputWidth = dispatchUpscale.upscaleSize.width;
	upscaleLastOutputHeight = dispatchUpscale.upscaleSize.height;
	upscaleLastQualityMode = a_qualityMode;

	static bool loggedFirstDispatch = false;
	if (!loggedFirstDispatch) {
		logger::info("[FidelityFX] First FSR upscaling dispatch submitted (render={}x{}, output={}x{}, quality={})",
			dispatchUpscale.renderSize.width,
			dispatchUpscale.renderSize.height,
			dispatchUpscale.upscaleSize.width,
			dispatchUpscale.upscaleSize.height,
			a_qualityMode);
		loggedFirstDispatch = true;
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
	upscaleLastRenderWidth = 0;
	upscaleLastRenderHeight = 0;
	upscaleLastOutputWidth = 0;
	upscaleLastOutputHeight = 0;
	upscaleLastQualityMode = 0xFFFFFFFFu;
	upscaleNeedsReset = true;
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
	// A HUDLess format that disagrees with what the context was built against is fatal
	// (see SetupFrameGeneration). The buffers can be reallocated under a live context,
	// because CreateFrameGenerationResources runs from several resize and render-target
	// paths, so the disagreement can appear while generation is already running. The
	// context cannot be rebuilt at that moment: destroying it could race an
	// interpolation in flight on the FFX presenter thread. Drop generation for this
	// frame instead. The next frame arrives with frameGenEnabled false and takes the
	// rebuild below.
	const bool hudlessFormatStale =
		HUDLessColor != nullptr && HUDLessColor->GetDesc().Format != frameGenHudlessFormat;

	// PostDisplay is now the only writer of the HUDLess buffer, and it early-returns on a
	// loading menu or a missing frame-buffer RTV while Reset() clears the slot to black
	// each present. Generating from a black HUDLess is not a device hazard -- FFX reads a
	// valid resource either way -- but differencing it against the presented backbuffer
	// makes the entire frame read as UI. Skip the generated frame instead.
	const bool hudLessFrameReady = upscaling->hudLessFrameValid[dx12SwapChain->frameIndex];

	const bool canUseFrameGen = a_useFrameGen &&
		commandList &&
		HUDLessColor &&
		depth &&
		motionVectors &&
		!hudlessFormatStale &&
		hudLessFrameReady;

	if (a_useFrameGen && !canUseFrameGen) {
		static bool loggedMissingResources = false;
		if (!loggedMissingResources) {
			logger::warn("[FidelityFX] Frame generation resources are not ready; skipping generated frames");
			loggedMissingResources = true;
		}
	}

	// The context was created before the game's render targets existed, so it could
	// only assume the HUDLess buffer matched the backbuffer. Now that the buffer is
	// real, correct the context if that assumption was wrong -- see SetupFrameGeneration
	// for why a wrong assumption removes the device.
	//
	// The rebuild is gated on generation currently being off so no interpolation can be
	// in flight in the FFX presenter thread while the context is destroyed. That costs
	// nothing in practice: Present() runs for thousands of menu and loading frames with
	// generation disabled before it is first enabled, so the correction lands long
	// before the first generated frame.
	if (hudlessFormatStale && !frameGenEnabled) {
		const auto actualHudlessFormat = HUDLessColor->GetDesc().Format;
		logger::info("[FidelityFX] HUDLess format is {}, frame generation context was built for {}; rebuilding context",
			static_cast<uint32_t>(actualHudlessFormat),
			static_cast<uint32_t>(frameGenHudlessFormat));

		DestroyFrameGeneration();
		SetupFrameGeneration(actualHudlessFormat);

		if (!featureFrameGen || frameGenContext == nullptr) {
			logger::error("[FidelityFX] Frame generation context rebuild failed; frame generation stays off");
			return;
		}
	}

	ffx::ConfigureDescFrameGeneration configParameters{};

	// The frame-generation callback stays installed even while generation is off.
	//
	// ffxConfigure calls made by the app are *deferred*: in
	// FrameInterpolationSwapchainDX12::setFrameGenerationConfig, applyChangesNow is
	// only true when the swap chain re-applies its own cached descriptor from
	// ::present. Our descriptor is therefore copied into nextFrameGenerationConfig
	// and applied at some later present. In the meantime the swap chain can still
	// run presentInterpolated() for a frame that was queued while generation was
	// enabled, and dispatchInterpolationCommands() invokes
	// frameGenerationCallback(...) with no null check. Clearing the pointer on the
	// disable path is thus a null call inside amd_fidelityfx_dx12.dll, which is how
	// entering a LoadingMenu one frame after generation started could crash.
	//
	// AMD's own sample (Samples/Upscalers/FidelityFX_FSR/dx12/fsrapirendermodule.cpp)
	// installs the callback unconditionally and toggles frameGenerationEnabled only.
	// frameGenContext is a singleton member that outlives the swap chain, so the
	// user-context pointer stays valid for the lifetime of the process.
	configParameters.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t {
		try {
			return ffxModule.Dispatch(reinterpret_cast<ffxContext*>(pUserCtx), &params->header);
		} catch (...) {
			return FFX_API_RETURN_ERROR;
		}
		};
	configParameters.frameGenerationCallbackUserContext = &frameGenContext;

	configParameters.frameGenerationEnabled = canUseFrameGen;
	// COMMON, not the ffxApiGetResourceDX12 default of FFX_API_RESOURCE_STATE_COMPUTE_READ.
	// Every buffer handed to frame generation is a D3D11 texture opened in D3D12 through
	// WrappedResource::OpenSharedHandle, so it is permanently in D3D12_RESOURCE_STATE_COMMON.
	// Declaring COMPUTE_READ makes FFX emit a transition barrier whose before-state does not
	// match, which D3D12 treats as fatal: the device is removed and GetDeviceRemovedReason()
	// reports DXGI_ERROR_INVALID_CALL. Upscale() above already declares COMMON for the very
	// same depth and motion vector resources, which is why upscaling ran for thousands of
	// frames while the first frame-generation present killed the device.
	configParameters.HUDLessColor =
		canUseFrameGen ? ffxApiGetResourceDX12(HUDLessColor, FFX_API_RESOURCE_STATE_COMMON) : FfxApiResource({});

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
	} else {
		// Gates the context rebuild above: only rebuild while generation is off.
		frameGenEnabled = canUseFrameGen;
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
		auto* gameViewport = fo4cs::RE::GetGraphicsState();
		auto* renderTargetManager = fo4cs::RE::GetRenderTargetManager();

		if (!gameViewport || !renderTargetManager) {
			static bool loggedMissingGameState = false;
			if (!loggedMissingGameState) {
				logger::warn("[FidelityFX] Frame generation game render state is not ready; skipping generated frames");
				loggedMissingGameState = true;
			}
		} else {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
			ffx::DispatchDescFrameGenerationPrepare dispatchParameters{};

			dispatchParameters.commandList = commandList;

			auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
			auto renderSize = float2(
				static_cast<float>(std::max(1u, static_cast<uint32_t>(screenSize.x * renderTargetManager->dynamicWidthRatio))),
				static_cast<float>(std::max(1u, static_cast<uint32_t>(screenSize.y * renderTargetManager->dynamicHeightRatio))));

			dispatchParameters.motionVectorScale.x = renderSize.x;
			dispatchParameters.motionVectorScale.y = renderSize.y;
			dispatchParameters.renderSize.width = std::max(1u, static_cast<uint32_t>(renderSize.x));
			dispatchParameters.renderSize.height = std::max(1u, static_cast<uint32_t>(renderSize.y));

			float2 jitter;
			jitter.x = -gameViewport->offsetX * renderSize.x / 2.0f;
			jitter.y = gameViewport->offsetY * renderSize.y / 2.0f;

			dispatchParameters.jitterOffset.x = -jitter.x;
			dispatchParameters.jitterOffset.y = -jitter.y;

			// #27: After a pause (e.g. LoadingMenu) the QPC delta spans the whole
			// blocked period; the SDK's optical flow expects a per-frame delta.
			// Clamp the resume-frame step so interpolation is not fed a huge jump.
			dispatchParameters.frameTimeDelta = std::min(deltaTime * 1000.f, 250.0f);
			dispatchParameters.cameraNear = fo4cs::RE::GetCameraNear();
			dispatchParameters.cameraFar = fo4cs::RE::GetCameraFar();
			dispatchParameters.cameraFovAngleVertical = 1.0f;
			dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
			dispatchParameters.frameID = frameID;
			// COMMON for the same reason as HUDLessColor above: these are the identical
			// shared textures Upscale() dispatches with, and they never leave COMMON.
			dispatchParameters.depth = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_COMMON);
			dispatchParameters.motionVectors =
				ffxApiGetResourceDX12(motionVectors, FFX_API_RESOURCE_STATE_COMMON);

			static bool loggedFirstPrepare = false;
			if (!loggedFirstPrepare) {
				const auto hudDesc = HUDLessColor->GetDesc();
				const auto depthDesc = depth->GetDesc();
				const auto motionDesc = motionVectors->GetDesc();
				logger::info("[FidelityFX] First FSR frame generation prepare (render={}x{}, hud={}x{} fmt={}, depth={}x{} fmt={}, motion={}x{} fmt={})",
					dispatchParameters.renderSize.width,
					dispatchParameters.renderSize.height,
					hudDesc.Width,
					hudDesc.Height,
					static_cast<uint32_t>(hudDesc.Format),
					depthDesc.Width,
					depthDesc.Height,
					static_cast<uint32_t>(depthDesc.Format),
					motionDesc.Width,
					motionDesc.Height,
					static_cast<uint32_t>(motionDesc.Format));
				loggedFirstPrepare = true;
			}

			const auto dispatchResult = ffx::Dispatch(frameGenContext, dispatchParameters);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
			if (dispatchResult != ffx::ReturnCode::Ok) {
				fgFailuresSinceLastSuccess++;

				if (fgFailuresSinceLastSuccess == 1 || fgFailuresSinceLastSuccess % 300 == 0) {
					logger::critical("[FidelityFX] Failed to dispatch frame generation! Error: {}, consecutive failures: {}",
						static_cast<uint32_t>(dispatchResult), fgFailuresSinceLastSuccess);
				}

				// Same rule as the configure block above: turn generation off but
				// leave the callback installed, or a frame already queued for
				// interpolation will call a null pointer inside the FFX swap chain.
				ffx::ConfigureDescFrameGeneration disableConfig{};
				disableConfig.frameGenerationEnabled = false;
				disableConfig.frameGenerationCallback = configParameters.frameGenerationCallback;
				disableConfig.frameGenerationCallbackUserContext = &frameGenContext;
				disableConfig.HUDLessColor = FfxApiResource({});
				disableConfig.swapChain = dx12SwapChain->swapChain;
				disableConfig.frameID = frameID;
				ffx::Configure(frameGenContext, disableConfig);
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
