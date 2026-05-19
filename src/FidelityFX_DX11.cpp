#include "FidelityFX_DX11.h"

#include <algorithm>

#include "DX11Hooks.h"
#include "Upscaler.h"
#include "RE/CameraData.h"
#include "RE/SingletonAccessors.h"

// DX11 native FFX backend — static link, no runtime DLL loading.
#include <fsr3.0/backends/dx11/ffx_dx11.h>

#include <dxgi.h>  // IDXGISwapChain

FidelityFX_DX11::~FidelityFX_DX11()
{
	Shutdown();
}

bool FidelityFX_DX11::Initialize(
	ID3D11Device* device,
	uint32_t displayWidth, uint32_t displayHeight,
	uint32_t renderWidth, uint32_t renderHeight,
	DXGI_FORMAT backBufferFormat)
{
	if (m_initialized)
		return true;

	if (!device)
		return false;

	m_device = device;

	// Max contexts: frameinterpolation (1) + opticalflow (1) + internal proxy (1) + safety (5)
	static constexpr size_t kMaxContexts = 8;
	m_scratchBufferSize = ffxGetScratchMemorySizeDX11(kMaxContexts);
	m_scratchBuffer = malloc(m_scratchBufferSize);
	if (!m_scratchBuffer)
		return false;

	FfxDevice ffxDevice = ffxGetDeviceDX11(m_device);
	FfxErrorCode err = ffxGetInterfaceDX11(
		&m_backendInterface, ffxDevice, m_scratchBuffer, m_scratchBufferSize, kMaxContexts);
	if (err != FFX_OK) {
		logger::error("[FidelityFX_DX11] Failed to create backend interface: {}", static_cast<int>(err));
		free(m_scratchBuffer);
		m_scratchBuffer = nullptr;
		return false;
	}

	FfxFrameInterpolationContextDescription desc{};
	desc.flags = FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INVERTED
		| FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE
		| FFX_FRAMEINTERPOLATION_ENABLE_HDR_COLOR_INPUT;
	desc.maxRenderSize = { renderWidth, renderHeight };
	desc.displaySize = { displayWidth, displayHeight };
	desc.backBufferFormat = ffxGetSurfaceFormatDX11(backBufferFormat);
	desc.backendInterface = m_backendInterface;

	err = ffxFrameInterpolationContextCreate(&m_fiContext, &desc);
	if (err != FFX_OK) {
		logger::error("[FidelityFX_DX11] Failed to create frame interpolation context: {}",
			static_cast<int>(err));
		Shutdown();
		return false;
	}

	FfxFrameInterpolationSharedResourceDescriptions sharedDescs{};
	err = ffxFrameInterpolationGetSharedResourceDescriptions(&m_fiContext, &sharedDescs);
	if (err != FFX_OK) {
		logger::error("[FidelityFX_DX11] Failed to get shared resource descriptions: {}",
			static_cast<int>(err));
		Shutdown();
		return false;
	}

	m_displayWidth = displayWidth;
	m_displayHeight = displayHeight;
	m_renderWidth = renderWidth;
	m_renderHeight = renderHeight;
	m_backBufferFormat = backBufferFormat;

	m_reconstructedPrevNearestDepth = CreateSharedResource(
		sharedDescs.reconstructedPrevNearestDepth, L"FI_ReconstructedPrevDepth");
	m_dilatedDepth = CreateSharedResource(
		sharedDescs.dilatedDepth, L"FI_DilatedDepth");
	m_dilatedMotionVectors = CreateSharedResource(
		sharedDescs.dilatedMotionVectors, L"FI_DilatedMotionVectors");

	if (!m_reconstructedPrevNearestDepth || !m_dilatedDepth || !m_dilatedMotionVectors) {
		logger::error("[FidelityFX_DX11] Failed to create shared resources");
		Shutdown();
		return false;
	}

	m_initialized = true;
	logger::info("[FidelityFX_DX11] Initialized ({}x{} render, {}x{} display)",
		renderWidth, renderHeight, displayWidth, displayHeight);
	return true;
}

void FidelityFX_DX11::Shutdown()
{
	if (m_fiContext.data[0] != 0) {
		ffxFrameInterpolationContextDestroy(&m_fiContext);
		ZeroMemory(&m_fiContext, sizeof(m_fiContext));
	}

	DestroySharedResources();

	free(m_scratchBuffer);
	m_scratchBuffer = nullptr;
	m_scratchBufferSize = 0;
	m_device = nullptr;
	m_initialized = false;
}

bool FidelityFX_DX11::Prepare(
	ID3D11DeviceContext* context,
	ID3D11Resource* depth,
	ID3D11Resource* motionVectors,
	float2 jitter,
	float2 renderSize,
	float frameTimeDelta,
	float cameraNear,
	float cameraFar,
	uint64_t frameID)
{
	if (!m_initialized || !context || !depth || !motionVectors)
		return false;

	FfxCommandList cmdList = ffxGetCommandListDX11(context);

	FfxFrameInterpolationPrepareDescription prep{};
	prep.commandList = cmdList;
	prep.renderSize = { static_cast<uint32_t>(renderSize.x), static_cast<uint32_t>(renderSize.y) };
	prep.jitterOffset = { jitter.x, jitter.y };
	prep.motionVectorScale = { renderSize.x, renderSize.y };
	prep.frameTimeDelta = frameTimeDelta;
	prep.cameraNear = cameraNear;
	prep.cameraFar = cameraFar;
	prep.viewSpaceToMetersFactor = 0.01428222656f;
	prep.cameraFovAngleVertical = 1.0f;
	prep.frameID = frameID;

	prep.depth = ffxGetResourceDX11(
		depth, GetFfxResourceDescriptionDX11(depth), L"FI_Depth", FFX_RESOURCE_STATE_COMPUTE_READ);
	prep.motionVectors = ffxGetResourceDX11(
		motionVectors, GetFfxResourceDescriptionDX11(motionVectors), L"FI_MV", FFX_RESOURCE_STATE_COMPUTE_READ);

	prep.dilatedDepth = ffxGetResourceDX11(
		m_dilatedDepth.get(), GetFfxResourceDescriptionDX11(m_dilatedDepth.get()), L"FI_DilatedDepthOut");
	prep.dilatedMotionVectors = ffxGetResourceDX11(
		m_dilatedMotionVectors.get(), GetFfxResourceDescriptionDX11(m_dilatedMotionVectors.get()), L"FI_DilatedMVOut");
	prep.reconstructedPrevDepth = ffxGetResourceDX11(
		m_reconstructedPrevNearestDepth.get(), GetFfxResourceDescriptionDX11(m_reconstructedPrevNearestDepth.get()), L"FI_RecPrevDepth");

	FfxErrorCode err = ffxFrameInterpolationPrepare(&m_fiContext, &prep);
	if (err != FFX_OK) {
		logger::error("[FidelityFX_DX11] Prepare failed: {}", static_cast<int>(err));
		return false;
	}

	return true;
}

bool FidelityFX_DX11::Dispatch(
	ID3D11DeviceContext* context,
	ID3D11Resource* currentBackBuffer,
	ID3D11Resource* hudLessBackBuffer,
	ID3D11Resource* output,
	float frameTimeDelta,
	bool reset,
	uint64_t frameID)
{
	if (!m_initialized || !context || !currentBackBuffer || !output)
		return false;

	FfxCommandList cmdList = ffxGetCommandListDX11(context);

	FfxFrameInterpolationDispatchDescription disp{};
	disp.commandList = cmdList;
	disp.displaySize = { m_displayWidth, m_displayHeight };
	disp.renderSize = { m_renderWidth, m_renderHeight };
	disp.frameTimeDelta = frameTimeDelta;
	disp.reset = reset;
	disp.cameraNear = fo4cs::RE::GetCameraNear();
	disp.cameraFar = fo4cs::RE::GetCameraFar();
	disp.cameraFovAngleVertical = 1.0f;
	disp.viewSpaceToMetersFactor = 0.01428222656f;
	disp.frameID = frameID;
	disp.backBufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
	disp.minMaxLuminance[0] = 0.0f;
	disp.minMaxLuminance[1] = 1000.0f;

	disp.currentBackBuffer = ffxGetResourceDX11(
		currentBackBuffer, GetFfxResourceDescriptionDX11(currentBackBuffer), L"FI_CurBackBuf");

	if (hudLessBackBuffer) {
		disp.currentBackBuffer_HUDLess = ffxGetResourceDX11(
			hudLessBackBuffer, GetFfxResourceDescriptionDX11(hudLessBackBuffer), L"FI_HUDLess");
	}

	disp.output = ffxGetResourceDX11(
		output, GetFfxResourceDescriptionDX11(output), L"FI_Output");

	disp.interpolationRect = { 0, 0, static_cast<int32_t>(m_displayWidth), static_cast<int32_t>(m_displayHeight) };

	disp.dilatedDepth = ffxGetResourceDX11(
		m_dilatedDepth.get(), GetFfxResourceDescriptionDX11(m_dilatedDepth.get()), L"FI_DilatedDepthIn");
	disp.dilatedMotionVectors = ffxGetResourceDX11(
		m_dilatedMotionVectors.get(), GetFfxResourceDescriptionDX11(m_dilatedMotionVectors.get()), L"FI_DilatedMVIn");
	disp.reconstructedPrevDepth = ffxGetResourceDX11(
		m_reconstructedPrevNearestDepth.get(), GetFfxResourceDescriptionDX11(m_reconstructedPrevNearestDepth.get()), L"FI_RecPrevDepthIn");

	FfxErrorCode err = ffxFrameInterpolationDispatch(&m_fiContext, &disp);
	if (err != FFX_OK) {
		logger::error("[FidelityFX_DX11] Dispatch failed: {}", static_cast<int>(err));
		return false;
	}

	return true;
}

winrt::com_ptr<ID3D11Texture2D> FidelityFX_DX11::CreateSharedResource(
	const FfxCreateResourceDescription& desc, const wchar_t* name)
{
	auto& rd = desc.resourceDescription;

	D3D11_TEXTURE2D_DESC texDesc{};
	texDesc.Width = rd.width;
	texDesc.Height = rd.height;
	texDesc.MipLevels = rd.mipCount;
	texDesc.ArraySize = 1;
	texDesc.Format = static_cast<DXGI_FORMAT>(rd.format);
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	winrt::com_ptr<ID3D11Texture2D> result;
	HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, result.put());
	if (FAILED(hr)) {
		logger::error("[FidelityFX_DX11] Failed to create shared resource '{}': {:x}",
			name ? reinterpret_cast<const char*>(name) : "unnamed", static_cast<uint32_t>(hr));
		return nullptr;
	}

	return result;
}

void FidelityFX_DX11::DestroySharedResources()
{
	m_reconstructedPrevNearestDepth = nullptr;
	m_dilatedDepth = nullptr;
	m_dilatedMotionVectors = nullptr;
}

// ---------------------------------------------------------------------------
// PreNG D3D11 FrameGen integration — extern functions called from DX11Hooks.cpp
// ---------------------------------------------------------------------------

#if defined(FALLOUT_PRE_NG)

bool PreNG_FrameGen_IsActive()
{
	return FidelityFX_DX11::GetSingleton()->IsReady();
}

void PreNG_FrameGen_PresentCallback(IDXGISwapChain* a_swapChain)
{
	static uint64_t s_frameID = 0;
	static LARGE_INTEGER s_lastFrameTime = []() {
		LARGE_INTEGER t{};
		QueryPerformanceCounter(&t);
		return t;
	}();
	static LARGE_INTEGER s_frequency = []() {
		LARGE_INTEGER f{};
		QueryPerformanceFrequency(&f);
		return f;
	}();

	auto* fi = FidelityFX_DX11::GetSingleton();
	auto* upscaling = Upscaling::GetSingleton();

	if (!fi->IsReady() || !upscaling->setupBuffers)
		return;

	// --- Frame pacing (mirrors DX12SwapChain::Present logic) ---
	// GameFrameLimiter: fixes engine 55fps cap bug when HighFPSPhysicsFix is absent
	if (!upscaling->highFPSPhysicsFixLoaded)
		upscaling->GameFrameLimiter();

	// FrameLimiter: caps game to half refresh rate when FrameGen is active,
	// allowing FrameGen to fill gaps for true frame doubling.
	bool const useFrameGen = true;
	upscaling->FrameLimiter(useFrameGen);

	auto* rendererData = fo4cs::GetRendererData();
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context)
		return;

	winrt::com_ptr<ID3D11Texture2D> backBufferTex;
	if (FAILED(a_swapChain->GetBuffer(0, IID_PPV_ARGS(backBufferTex.put()))) || !backBufferTex)
		return;

	// Render target indices from Upscaler.cpp enums:
	//   RenderTarget::kMain = 3, RenderTarget::kMotionVectors = 29
	//   DepthStencilTarget::kMain = 0
	static constexpr uint kMainRT = 3;
	static constexpr uint kMainDS = 0;
	static constexpr uint kMotionVectorsRT = 29;

	auto& main = rendererData->renderTargets[kMainRT];
	auto& depthRT = rendererData->depthStencilTargets[kMainDS];
	auto& mvRT = rendererData->renderTargets[kMotionVectorsRT];

	auto* depthTex = reinterpret_cast<ID3D11Texture2D*>(depthRT.texture);
	auto* mvTex = reinterpret_cast<ID3D11Texture2D*>(mvRT.texture);
	auto* mainTex = reinterpret_cast<ID3D11Texture2D*>(main.texture);

	if (!depthTex || !mvTex || !mainTex)
		return;

	auto* gameViewport = fo4cs::RE::GetGraphicsState();
	auto* rtManager = fo4cs::RE::GetRenderTargetManager();

	float2 screenSize = { static_cast<float>(gameViewport->screenWidth), static_cast<float>(gameViewport->screenHeight) };
	float2 renderSize = { screenSize.x * rtManager->dynamicWidthRatio, screenSize.y * rtManager->dynamicHeightRatio };

	float2 jitter = {};
	jitter.x = -gameViewport->offsetX * screenSize.x / 2.0f;
	jitter.y = gameViewport->offsetY * screenSize.y / 2.0f;
	jitter.x /= rtManager->dynamicWidthRatio;
	jitter.y /= rtManager->dynamicHeightRatio;

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	float deltaMs = static_cast<float>(now.QuadPart - s_lastFrameTime.QuadPart) * 1000.0f / static_cast<float>(s_frequency.QuadPart);
	s_lastFrameTime = now;

	float cameraNear = fo4cs::RE::GetCameraNear();
	float cameraFar = fo4cs::RE::GetCameraFar();

	uint64_t frameID = s_frameID++;
	static bool s_firstFrame = true;
	bool const reset = s_firstFrame;
	s_firstFrame = false;

	fi->Prepare(context, depthTex, mvTex, jitter, renderSize, deltaMs, cameraNear, cameraFar, frameID);

	ID3D11Resource* hudLessRes = nullptr;
	if (upscaling->HUDLessBufferShared[0] && upscaling->HUDLessBufferShared[0]->resource)
		hudLessRes = upscaling->HUDLessBufferShared[0]->resource.get();

	fi->Dispatch(context, main.texture, hudLessRes, backBufferTex.get(), deltaMs, reset, frameID);
}

void PreNG_FrameGen_InitForSwapChain(ID3D11Device* a_device, IDXGISwapChain* a_swapChain)
{
	auto* fi = FidelityFX_DX11::GetSingleton();
	if (fi->IsReady())
		return;

	DXGI_SWAP_CHAIN_DESC scDesc{};
	if (FAILED(a_swapChain->GetDesc(&scDesc)))
		return;

	auto* rtManager = fo4cs::RE::GetRenderTargetManager();

	uint32_t displayW = scDesc.BufferDesc.Width;
	uint32_t displayH = scDesc.BufferDesc.Height;
	float ratioW = rtManager->dynamicWidthRatio;
	float ratioH = rtManager->dynamicHeightRatio;
	// dynamicWidthRatio/HeightRatio may be 0 during early init — fall back to 1.0
	if (ratioW <= 0.0f) ratioW = 1.0f;
	if (ratioH <= 0.0f) ratioH = 1.0f;
	uint32_t renderW = std::max(1u, static_cast<uint32_t>(displayW * ratioW));
	uint32_t renderH = std::max(1u, static_cast<uint32_t>(displayH * ratioH));

	logger::info("[FrameGen] PreNG InitForSwapChain ({}x{} display, {}x{} render, fmt={})",
		displayW, displayH, renderW, renderH, static_cast<uint32_t>(scDesc.BufferDesc.Format));

	fi->Initialize(a_device, displayW, displayH, renderW, renderH, scDesc.BufferDesc.Format);

	DX11Hooks::SetPresentCallback(&PreNG_FrameGen_PresentCallback);
	logger::info("[FrameGen] PreNG D3D11-native FrameGen initialized ({}x{} → {}x{})",
		renderW, renderH, displayW, displayH);
}

#endif  // FALLOUT_PRE_NG
