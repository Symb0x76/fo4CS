#include "FidelityFX_DX11.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "DX11Hooks.h"
#include "Upscaler.h"
#include "RE/CameraData.h"
#include "RE/SingletonAccessors.h"

// DX11 native FFX backend — static link, no runtime DLL loading.
#include <fsr3.0/backends/dx11/ffx_dx11.h>

#include <dxgi.h>  // IDXGISwapChain

namespace
{
	std::string WideToUtf8(const wchar_t* value)
	{
		if (!value)
			return {};

		const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
		if (size <= 1)
			return {};

		std::string result(static_cast<std::size_t>(size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
		result.resize(static_cast<std::size_t>(size - 1));
		return result;
	}

	DXGI_FORMAT ToDxgiFormat(FfxSurfaceFormat format)
	{
		switch (format) {
		case FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS:
			return DXGI_FORMAT_R32G32B32A32_TYPELESS;
		case FFX_SURFACE_FORMAT_R32G32B32A32_UINT:
			return DXGI_FORMAT_R32G32B32A32_UINT;
		case FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case FFX_SURFACE_FORMAT_R32G32_FLOAT:
			return DXGI_FORMAT_R32G32_FLOAT;
		case FFX_SURFACE_FORMAT_R8_UINT:
			return DXGI_FORMAT_R8_UINT;
		case FFX_SURFACE_FORMAT_R32_UINT:
			return DXGI_FORMAT_R32_UINT;
		case FFX_SURFACE_FORMAT_R10G10B10A2_UNORM:
			return DXGI_FORMAT_R10G10B10A2_UNORM;
		case FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS:
			return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		case FFX_SURFACE_FORMAT_R8G8B8A8_UNORM:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case FFX_SURFACE_FORMAT_R8G8B8A8_SNORM:
			return DXGI_FORMAT_R8G8B8A8_SNORM;
		case FFX_SURFACE_FORMAT_R8G8B8A8_SRGB:
			return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case FFX_SURFACE_FORMAT_R11G11B10_FLOAT:
			return DXGI_FORMAT_R11G11B10_FLOAT;
		case FFX_SURFACE_FORMAT_R16G16_FLOAT:
			return DXGI_FORMAT_R16G16_FLOAT;
		case FFX_SURFACE_FORMAT_R16G16_UINT:
			return DXGI_FORMAT_R16G16_UINT;
		case FFX_SURFACE_FORMAT_R16G16_SINT:
			return DXGI_FORMAT_R16G16_SINT;
		case FFX_SURFACE_FORMAT_R16_FLOAT:
			return DXGI_FORMAT_R16_FLOAT;
		case FFX_SURFACE_FORMAT_R16_UINT:
			return DXGI_FORMAT_R16_UINT;
		case FFX_SURFACE_FORMAT_R16_UNORM:
			return DXGI_FORMAT_R16_UNORM;
		case FFX_SURFACE_FORMAT_R16_SNORM:
			return DXGI_FORMAT_R16_SNORM;
		case FFX_SURFACE_FORMAT_R8_UNORM:
			return DXGI_FORMAT_R8_UNORM;
		case FFX_SURFACE_FORMAT_R8G8_UNORM:
			return DXGI_FORMAT_R8G8_UNORM;
		case FFX_SURFACE_FORMAT_R8G8_UINT:
			return DXGI_FORMAT_R8G8_UINT;
		case FFX_SURFACE_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_R32_FLOAT;
		case FFX_SURFACE_FORMAT_UNKNOWN:
		default:
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	UINT ToD3D11BindFlags(FfxResourceUsage usage)
	{
		const auto flags = static_cast<std::uint32_t>(usage);
		UINT bindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (flags & FFX_RESOURCE_USAGE_RENDERTARGET)
			bindFlags |= D3D11_BIND_RENDER_TARGET;
		if (flags & FFX_RESOURCE_USAGE_UAV)
			bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		if (flags & FFX_RESOURCE_USAGE_DEPTHTARGET)
			bindFlags |= D3D11_BIND_DEPTH_STENCIL;
		return bindFlags;
	}

#if defined(FALLOUT_PRE_NG)
	enum class PreNGFrameGenBlockReason
	{
		kUIUnavailable,
		kBlockingMenuOpen,
		kRuntimeFrameUnavailable,
		kHUDLessUnavailable
	};

	constexpr std::array<std::string_view, 3> kPreNGFrameGenBlockingMenus{
		"MainMenu",
		"LoadingMenu",
		"FaderMenu"
	};

	const char* ToString(PreNGFrameGenBlockReason reason)
	{
		switch (reason) {
		case PreNGFrameGenBlockReason::kUIUnavailable:
			return "UI unavailable";
		case PreNGFrameGenBlockReason::kBlockingMenuOpen:
			return "startup/loading menu active";
		case PreNGFrameGenBlockReason::kRuntimeFrameUnavailable:
			return "runtime frame unavailable";
		case PreNGFrameGenBlockReason::kHUDLessUnavailable:
			return "HUDLess frame unavailable";
		default:
			return "unknown";
		}
	}

	std::optional<PreNGFrameGenBlockReason> GetPreNGFrameGenBlockReason()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui)
			return PreNGFrameGenBlockReason::kUIUnavailable;

		const auto isMenuOpen = [ui](std::string_view menu) {
			return ui->GetMenuOpen(menu.data());
		};
		if (std::ranges::any_of(kPreNGFrameGenBlockingMenus, isMenuOpen))
			return PreNGFrameGenBlockReason::kBlockingMenuOpen;

		return std::nullopt;
	}

	std::optional<PreNGFrameGenBlockReason> s_lastLoggedPreNGFrameGenBlock;

	bool IsPreNGFrameGenDispatchAllowed(bool a_log, std::optional<PreNGFrameGenBlockReason> a_extraBlock = std::nullopt)
	{
		const auto blockReason = a_extraBlock ? a_extraBlock : GetPreNGFrameGenBlockReason();
		if (!blockReason)
			return true;

		if (a_log && s_lastLoggedPreNGFrameGenBlock != blockReason) {
			logger::info("[FrameGen] PreNG D3D11 dispatch held: {}", ToString(*blockReason));
			logger::default_logger()->flush();
			s_lastLoggedPreNGFrameGenBlock = blockReason;
		}

		return false;
	}

	void ResetPreNGFrameGenDispatchBlockLog()
	{
		s_lastLoggedPreNGFrameGenBlock.reset();
	}

#endif
}

FidelityFX_DX11::~FidelityFX_DX11()
{
	Shutdown();
}

bool FidelityFX_DX11::EnsureBackend(ID3D11Device* device)
{
	if (m_backendInitialized)
		return true;

	if (!device)
		return false;

	m_device = device;

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
		m_scratchBufferSize = 0;
		m_device = nullptr;
		return false;
	}

	m_backendInitialized = true;
	return true;
}

bool FidelityFX_DX11::Initialize(
	ID3D11Device* device,
	uint32_t displayWidth, uint32_t displayHeight,
	uint32_t renderWidth, uint32_t renderHeight,
	DXGI_FORMAT backBufferFormat)
{
	if (m_initialized)
		return true;

	if (!EnsureBackend(device))
		return false;

	FfxFrameInterpolationContextDescription desc{};
	// Depth flags reflect Fallout 4 PreNG: reversed-z with infinite far plane.
	// Do NOT set ENABLE_HDR_COLOR_INPUT — FO4 outputs SDR sRGB.
	desc.flags = FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INVERTED
		| FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE;
	desc.maxRenderSize = { renderWidth, renderHeight };
	desc.displaySize = { displayWidth, displayHeight };
	desc.backBufferFormat = ffxGetSurfaceFormatDX11(backBufferFormat);
	desc.backendInterface = m_backendInterface;

	logger::debug("[FidelityFX_DX11] Creating FFX context: flags=0x{:X}, maxRender={}x{}, display={}x{}, fmt={}",
		desc.flags, desc.maxRenderSize.width, desc.maxRenderSize.height,
		desc.displaySize.width, desc.displaySize.height, static_cast<uint32_t>(desc.backBufferFormat));
	logger::default_logger()->flush();

	FfxErrorCode err = FFX_OK;
	try {
		err = ffxFrameInterpolationContextCreate(&m_fiContext, &desc);
	} catch (const std::exception& e) {
		logger::error("[FidelityFX_DX11] C++ exception from ffxFrameInterpolationContextCreate: {}",
			e.what());
		logger::default_logger()->flush();
		Shutdown();
		return false;
	} catch (...) {
		logger::error("[FidelityFX_DX11] Unknown C++ exception from ffxFrameInterpolationContextCreate");
		logger::default_logger()->flush();
		Shutdown();
		return false;
	}
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

	DestroyUpscaling();
	DestroySharedResources();

	free(m_scratchBuffer);
	m_scratchBuffer = nullptr;
	m_scratchBufferSize = 0;
	m_device = nullptr;
	m_backendInitialized = false;
	m_initialized = false;
}

bool FidelityFX_DX11::InitializeUpscaling(ID3D11Device* device, uint32_t maxRenderWidth, uint32_t maxRenderHeight, uint32_t outputWidth, uint32_t outputHeight)
{
	maxRenderWidth = std::max(1u, maxRenderWidth);
	maxRenderHeight = std::max(1u, maxRenderHeight);
	outputWidth = std::max(1u, outputWidth);
	outputHeight = std::max(1u, outputHeight);

	if (m_upscaleInitialized &&
		m_upscaleMaxRenderWidth == maxRenderWidth &&
		m_upscaleMaxRenderHeight == maxRenderHeight &&
		m_upscaleOutputWidth == outputWidth &&
		m_upscaleOutputHeight == outputHeight) {
		return true;
	}

	if (!EnsureBackend(device))
		return false;

	DestroyUpscaling();

	FfxFsr3UpscalerContextDescription desc{};
	desc.flags = FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED |
		FFX_FSR3UPSCALER_ENABLE_DEPTH_INFINITE |
		FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE |
		FFX_FSR3UPSCALER_ENABLE_DYNAMIC_RESOLUTION;
	desc.maxRenderSize = { maxRenderWidth, maxRenderHeight };
	desc.maxUpscaleSize = { outputWidth, outputHeight };
	desc.fpMessage = nullptr;
	desc.backendInterface = m_backendInterface;

	FfxErrorCode err = FFX_OK;
	try {
		err = ffxFsr3UpscalerContextCreate(&m_upscaleContext, &desc);
	} catch (const std::exception& e) {
		logger::error("[FidelityFX_DX11] C++ exception from ffxFsr3UpscalerContextCreate: {}", e.what());
		DestroyUpscaling();
		return false;
	} catch (...) {
		logger::error("[FidelityFX_DX11] Unknown C++ exception from ffxFsr3UpscalerContextCreate");
		DestroyUpscaling();
		return false;
	}
	if (err != FFX_OK) {
		logger::error("[FidelityFX_DX11] Failed to create FSR upscaler context: {}", static_cast<int>(err));
		DestroyUpscaling();
		return false;
	}

	FfxFsr3UpscalerSharedResourceDescriptions sharedDescs{};
	err = ffxFsr3UpscalerGetSharedResourceDescriptions(&m_upscaleContext, &sharedDescs);
	if (err != FFX_OK) {
		logger::error("[FidelityFX_DX11] Failed to get FSR upscaler shared resource descriptions: {}", static_cast<int>(err));
		DestroyUpscaling();
		return false;
	}

	m_upscaleReconstructedPrevNearestDepth = CreateSharedResource(sharedDescs.reconstructedPrevNearestDepth, L"FSR_ReconstructedPrevDepth");
	m_upscaleDilatedDepth = CreateSharedResource(sharedDescs.dilatedDepth, L"FSR_DilatedDepth");
	m_upscaleDilatedMotionVectors = CreateSharedResource(sharedDescs.dilatedMotionVectors, L"FSR_DilatedMotionVectors");
	if (!m_upscaleReconstructedPrevNearestDepth || !m_upscaleDilatedDepth || !m_upscaleDilatedMotionVectors) {
		logger::error("[FidelityFX_DX11] Failed to create FSR upscaler shared resources");
		DestroyUpscaling();
		return false;
	}

	m_upscaleMaxRenderWidth = maxRenderWidth;
	m_upscaleMaxRenderHeight = maxRenderHeight;
	m_upscaleOutputWidth = outputWidth;
	m_upscaleOutputHeight = outputHeight;
	m_upscaleLastRenderWidth = 0;
	m_upscaleLastRenderHeight = 0;
	m_upscaleLastOutputWidth = 0;
	m_upscaleLastOutputHeight = 0;
	m_upscaleLastQualityMode = 0xFFFFFFFFu;
	m_upscaleNeedsReset = true;
	m_upscaleInitialized = true;
	logger::info("[FidelityFX_DX11] FSR upscaler initialized (maxRender={}x{}, output={}x{})",
		maxRenderWidth,
		maxRenderHeight,
		outputWidth,
		outputHeight);
	return true;
}

bool FidelityFX_DX11::Upscale(
	ID3D11DeviceContext* context,
	ID3D11Resource* color,
	ID3D11Resource* output,
	ID3D11Resource* depth,
	ID3D11Resource* motionVectors,
	float2 jitter,
	float2 renderSize,
	float2 displaySize,
	uint qualityMode)
{
	if (!m_upscaleInitialized || !context || !color || !output || !depth || !motionVectors)
		return false;

	FfxFsr3UpscalerDispatchDescription dispatch{};
	dispatch.commandList = ffxGetCommandListDX11(context);
	dispatch.color = ffxGetResourceDX11(color, GetFfxResourceDescriptionDX11(color), L"FSR_Color", FFX_RESOURCE_STATE_COMPUTE_READ);
	dispatch.depth = ffxGetResourceDX11(depth, GetFfxResourceDescriptionDX11(depth), L"FSR_Depth", FFX_RESOURCE_STATE_COMPUTE_READ);
	dispatch.motionVectors = ffxGetResourceDX11(motionVectors, GetFfxResourceDescriptionDX11(motionVectors), L"FSR_MotionVectors", FFX_RESOURCE_STATE_COMPUTE_READ);
	dispatch.output = ffxGetResourceDX11(output, GetFfxResourceDescriptionDX11(output), L"FSR_Output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
	dispatch.dilatedDepth = ffxGetResourceDX11(
		m_upscaleDilatedDepth.get(), GetFfxResourceDescriptionDX11(m_upscaleDilatedDepth.get()), L"FSR_DilatedDepth", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
	dispatch.dilatedMotionVectors = ffxGetResourceDX11(
		m_upscaleDilatedMotionVectors.get(), GetFfxResourceDescriptionDX11(m_upscaleDilatedMotionVectors.get()), L"FSR_DilatedMotionVectors", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
	dispatch.reconstructedPrevNearestDepth = ffxGetResourceDX11(
		m_upscaleReconstructedPrevNearestDepth.get(), GetFfxResourceDescriptionDX11(m_upscaleReconstructedPrevNearestDepth.get()), L"FSR_ReconstructedPrevDepth", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
	dispatch.jitterOffset = { -jitter.x, -jitter.y };
	dispatch.motionVectorScale = { renderSize.x, renderSize.y };
	dispatch.renderSize = {
		std::max(1u, static_cast<uint32_t>(renderSize.x)),
		std::max(1u, static_cast<uint32_t>(renderSize.y))
	};
	dispatch.upscaleSize = {
		std::max(1u, static_cast<uint32_t>(displaySize.x)),
		std::max(1u, static_cast<uint32_t>(displaySize.y))
	};
	const bool resetHistory =
		m_upscaleNeedsReset ||
		m_upscaleLastRenderWidth != dispatch.renderSize.width ||
		m_upscaleLastRenderHeight != dispatch.renderSize.height ||
		m_upscaleLastOutputWidth != dispatch.upscaleSize.width ||
		m_upscaleLastOutputHeight != dispatch.upscaleSize.height ||
		m_upscaleLastQualityMode != qualityMode;
	dispatch.enableSharpening = false;
	dispatch.sharpness = 0.0f;

	static LARGE_INTEGER frequency = []() {
		LARGE_INTEGER value{};
		QueryPerformanceFrequency(&value);
		return value;
	}();
	static LARGE_INTEGER lastFrameTime = []() {
		LARGE_INTEGER value{};
		QueryPerformanceCounter(&value);
		return value;
	}();
	LARGE_INTEGER currentFrameTime{};
	QueryPerformanceCounter(&currentFrameTime);
	dispatch.frameTimeDelta = std::max(
		static_cast<float>(currentFrameTime.QuadPart - lastFrameTime.QuadPart) * 1000.0f / static_cast<float>(frequency.QuadPart),
		0.0f);
	lastFrameTime = currentFrameTime;
	dispatch.preExposure = 1.0f;
	dispatch.reset = resetHistory;
	dispatch.cameraNear = fo4cs::RE::GetCameraNear();
	dispatch.cameraFar = fo4cs::RE::GetCameraFar();
	dispatch.cameraFovAngleVertical = 1.0f;
	dispatch.viewSpaceToMetersFactor = 0.01428222656f;
	dispatch.flags = 0;

	FfxErrorCode err = FFX_OK;
	try {
		err = ffxFsr3UpscalerContextDispatch(&m_upscaleContext, &dispatch);
	} catch (const std::exception& e) {
		logger::error("[FidelityFX_DX11] C++ exception from ffxFsr3UpscalerContextDispatch: {}", e.what());
		DestroyUpscaling();
		return false;
	} catch (...) {
		logger::error("[FidelityFX_DX11] Unknown C++ exception from ffxFsr3UpscalerContextDispatch");
		DestroyUpscaling();
		return false;
	}
	if (err != FFX_OK) {
		logger::error("[FidelityFX_DX11] FSR upscaling dispatch failed: {}", static_cast<int>(err));
		return false;
	}

	m_upscaleNeedsReset = false;
	m_upscaleLastRenderWidth = dispatch.renderSize.width;
	m_upscaleLastRenderHeight = dispatch.renderSize.height;
	m_upscaleLastOutputWidth = dispatch.upscaleSize.width;
	m_upscaleLastOutputHeight = dispatch.upscaleSize.height;
	m_upscaleLastQualityMode = qualityMode;

	static bool s_loggedFirstDispatch = false;
	if (!s_loggedFirstDispatch) {
		logger::info("[FidelityFX_DX11] First FSR upscaling dispatch submitted (render={}x{}, output={}x{}, quality={})",
			dispatch.renderSize.width,
			dispatch.renderSize.height,
			dispatch.upscaleSize.width,
			dispatch.upscaleSize.height,
			qualityMode);
		s_loggedFirstDispatch = true;
	}

	return true;
}

void FidelityFX_DX11::DestroyUpscaling()
{
	if (m_upscaleContext.data[0] != 0) {
		ffxFsr3UpscalerContextDestroy(&m_upscaleContext);
		ZeroMemory(&m_upscaleContext, sizeof(m_upscaleContext));
	}
	m_upscaleReconstructedPrevNearestDepth = nullptr;
	m_upscaleDilatedDepth = nullptr;
	m_upscaleDilatedMotionVectors = nullptr;
	m_upscaleMaxRenderWidth = 0;
	m_upscaleMaxRenderHeight = 0;
	m_upscaleOutputWidth = 0;
	m_upscaleOutputHeight = 0;
	m_upscaleLastRenderWidth = 0;
	m_upscaleLastRenderHeight = 0;
	m_upscaleLastOutputWidth = 0;
	m_upscaleLastOutputHeight = 0;
	m_upscaleLastQualityMode = 0xFFFFFFFFu;
	m_upscaleNeedsReset = true;
	m_upscaleInitialized = false;
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

	FfxErrorCode err = FFX_OK;
	try {
		err = ffxFrameInterpolationPrepare(&m_fiContext, &prep);
	} catch (const std::exception& e) {
		logger::error("[FidelityFX_DX11] C++ exception from ffxFrameInterpolationPrepare: {}", e.what());
		logger::default_logger()->flush();
		Shutdown();
		return false;
	} catch (...) {
		logger::error("[FidelityFX_DX11] Unknown C++ exception from ffxFrameInterpolationPrepare");
		logger::default_logger()->flush();
		Shutdown();
		return false;
	}
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

	FfxErrorCode err = FFX_OK;
	try {
		err = ffxFrameInterpolationDispatch(&m_fiContext, &disp);
	} catch (const std::exception& e) {
		logger::error("[FidelityFX_DX11] C++ exception from ffxFrameInterpolationDispatch: {}", e.what());
		logger::default_logger()->flush();
		Shutdown();
		return false;
	} catch (...) {
		logger::error("[FidelityFX_DX11] Unknown C++ exception from ffxFrameInterpolationDispatch");
		logger::default_logger()->flush();
		Shutdown();
		return false;
	}
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
	const std::string resourceName = WideToUtf8(name ? name : desc.name);
	const std::string sdkName = WideToUtf8(desc.name);
	const DXGI_FORMAT dxgiFormat = ToDxgiFormat(rd.format);
	const UINT bindFlags = ToD3D11BindFlags(rd.usage);

	if (rd.type != FFX_RESOURCE_TYPE_TEXTURE2D || dxgiFormat == DXGI_FORMAT_UNKNOWN || rd.width == 0 || rd.height == 0) {
		logger::error(
			"[FidelityFX_DX11] Invalid shared resource description '{}' sdk='{}' type={} ffxFormat={} dxgiFormat={} size={}x{} mips={} usage=0x{:X}",
			resourceName,
			sdkName,
			static_cast<uint32_t>(rd.type),
			static_cast<uint32_t>(rd.format),
			static_cast<uint32_t>(dxgiFormat),
			rd.width,
			rd.height,
			rd.mipCount,
			static_cast<uint32_t>(rd.usage));
		return nullptr;
	}

	D3D11_TEXTURE2D_DESC texDesc{};
	texDesc.Width = rd.width;
	texDesc.Height = rd.height;
	texDesc.MipLevels = rd.mipCount ? rd.mipCount : 1;
	texDesc.ArraySize = 1;
	texDesc.Format = dxgiFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = bindFlags;

	winrt::com_ptr<ID3D11Texture2D> result;
	HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, result.put());
	if (FAILED(hr)) {
		logger::error(
			"[FidelityFX_DX11] Failed to create shared resource '{}' sdk='{}' hr=0x{:08X} ffxFormat={} dxgiFormat={} size={}x{} mips={} usage=0x{:X} bind=0x{:X}",
			resourceName,
			sdkName,
			static_cast<uint32_t>(hr),
			static_cast<uint32_t>(rd.format),
			static_cast<uint32_t>(dxgiFormat),
			rd.width,
			rd.height,
			texDesc.MipLevels,
			static_cast<uint32_t>(rd.usage),
			bindFlags);
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
	auto* upscaling = Upscaling::GetSingleton();
	const bool hudLessReady =
		upscaling->hudLessFrameValid[0] &&
		upscaling->hudLessFrameIDs[0] != 0;
	return FidelityFX_DX11::GetSingleton()->IsReady() && hudLessReady && IsPreNGFrameGenDispatchAllowed(false);
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

	if (!fi->IsReady())
		return;
	if (!IsPreNGFrameGenDispatchAllowed(true))
		return;

	auto* rendererData = fo4cs::GetRendererData();
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context)
		return;

	winrt::com_ptr<ID3D11Texture2D> backBufferTex;
	if (FAILED(a_swapChain->GetBuffer(0, IID_PPV_ARGS(backBufferTex.put()))) || !backBufferTex)
		return;

	static winrt::com_ptr<ID3D11Texture2D> s_presentCopy;
	static D3D11_TEXTURE2D_DESC s_presentCopyDesc{};
	D3D11_TEXTURE2D_DESC backBufferDesc{};
	backBufferTex->GetDesc(&backBufferDesc);
	D3D11_TEXTURE2D_DESC presentCopyDesc = backBufferDesc;
	presentCopyDesc.Usage = D3D11_USAGE_DEFAULT;
	presentCopyDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
	presentCopyDesc.CPUAccessFlags = 0;
	presentCopyDesc.MiscFlags = 0;

	const bool presentCopyValid =
		s_presentCopy &&
		s_presentCopyDesc.Width == presentCopyDesc.Width &&
		s_presentCopyDesc.Height == presentCopyDesc.Height &&
		s_presentCopyDesc.Format == presentCopyDesc.Format &&
		s_presentCopyDesc.SampleDesc.Count == presentCopyDesc.SampleDesc.Count &&
		s_presentCopyDesc.BindFlags == presentCopyDesc.BindFlags;
	if (!presentCopyValid) {
		winrt::com_ptr<ID3D11Device> device;
		backBufferTex->GetDevice(device.put());
		if (!device)
			return;

		s_presentCopy = nullptr;
		const HRESULT hr = device->CreateTexture2D(&presentCopyDesc, nullptr, s_presentCopy.put());
		if (FAILED(hr) || !s_presentCopy) {
			static bool s_loggedPresentCopyFailure = false;
			if (!s_loggedPresentCopyFailure) {
				logger::warn("[FrameGen] PreNG D3D11 dispatch waiting for present-copy texture (hr=0x{:08X})", static_cast<std::uint32_t>(hr));
				logger::default_logger()->flush();
				s_loggedPresentCopyFailure = true;
			}
			return;
		}
		s_presentCopyDesc = presentCopyDesc;
	}

	// Render target indices from Upscaler.cpp enums:
	//   RenderTarget::kMain = 3, RenderTarget::kMotionVectors = 29
	//   DepthStencilTarget::kMain = 2
	static constexpr uint kMainRT = 3;
	static constexpr uint kMainDS = 2;
	static constexpr uint kMotionVectorsRT = 29;

	auto& main = rendererData->renderTargets[kMainRT];
	auto& depthRT = rendererData->depthStencilTargets[kMainDS];
	auto& mvRT = rendererData->renderTargets[kMotionVectorsRT];

	auto* depthTex = reinterpret_cast<ID3D11Texture2D*>(depthRT.texture);
	auto* mvTex = reinterpret_cast<ID3D11Texture2D*>(mvRT.texture);
	auto* mainTex = reinterpret_cast<ID3D11Texture2D*>(main.texture);

	if (!depthTex || !mvTex || !mainTex) {
		static bool s_loggedMissingRenderTargets = false;
		if (!s_loggedMissingRenderTargets) {
			logger::warn("[FrameGen] PreNG D3D11 dispatch waiting for render targets (main={}, depth={}, motion={})",
				mainTex != nullptr,
				depthTex != nullptr,
				mvTex != nullptr);
			logger::default_logger()->flush();
			s_loggedMissingRenderTargets = true;
		}
		return;
	}

	auto* gameViewport = fo4cs::RE::GetGraphicsState();
	auto* rtManager = fo4cs::RE::GetRenderTargetManager();
	if (!gameViewport || !rtManager) {
		static bool s_loggedMissingState = false;
		if (!s_loggedMissingState) {
			logger::warn("[FrameGen] PreNG D3D11 dispatch waiting for render backend globals");
			logger::default_logger()->flush();
			s_loggedMissingState = true;
		}
		return;
	}

	float2 screenSize = { static_cast<float>(gameViewport->screenWidth), static_cast<float>(gameViewport->screenHeight) };
	float ratioW = rtManager->dynamicWidthRatio > 0.0f ? rtManager->dynamicWidthRatio : 1.0f;
	float ratioH = rtManager->dynamicHeightRatio > 0.0f ? rtManager->dynamicHeightRatio : 1.0f;
	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
		static bool s_loggedInvalidViewport = false;
		if (!s_loggedInvalidViewport) {
			logger::warn("[FrameGen] PreNG D3D11 dispatch waiting for valid viewport ({}x{})",
				screenSize.x,
				screenSize.y);
			logger::default_logger()->flush();
			s_loggedInvalidViewport = true;
		}
		return;
	}
	float2 renderSize = {
		static_cast<float>(std::max(1u, static_cast<uint32_t>(screenSize.x * ratioW))),
		static_cast<float>(std::max(1u, static_cast<uint32_t>(screenSize.y * ratioH)))
	};

	float2 jitter = {};
	jitter.x = gameViewport->offsetX * renderSize.x / 2.0f;
	jitter.y = -gameViewport->offsetY * renderSize.y / 2.0f;

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	float deltaMs = static_cast<float>(now.QuadPart - s_lastFrameTime.QuadPart) * 1000.0f / static_cast<float>(s_frequency.QuadPart);
	s_lastFrameTime = now;

	float cameraNear = fo4cs::RE::GetCameraNear();
	float cameraFar = fo4cs::RE::GetCameraFar();

	static uint64_t s_lastDispatchedHUDLessFrameID = 0;
	const auto hudLessFrameID = upscaling->hudLessFrameIDs[0];
	ID3D11Resource* hudLessRes = nullptr;
	if (upscaling->HUDLessBufferShared[0] && upscaling->HUDLessBufferShared[0]->resource &&
		upscaling->hudLessFrameValid[0] && hudLessFrameID != 0 &&
		hudLessFrameID != s_lastDispatchedHUDLessFrameID) {
		hudLessRes = upscaling->HUDLessBufferShared[0]->resource.get();
	}
	if (!hudLessRes) {
		IsPreNGFrameGenDispatchAllowed(true, PreNGFrameGenBlockReason::kHUDLessUnavailable);
		return;
	}

	context->CopyResource(s_presentCopy.get(), backBufferTex.get());

	// Frame pacing is applied only after all resources for this dispatch are current.
	if (!upscaling->highFPSPhysicsFixLoaded)
		upscaling->GameFrameLimiter();
	upscaling->FrameLimiter(true);

	uint64_t frameID = s_frameID++;
	static bool s_firstFrame = true;
	bool const reset = s_firstFrame;
	s_firstFrame = false;

	if (!fi->Prepare(context, depthTex, mvTex, jitter, renderSize, deltaMs, cameraNear, cameraFar, frameID))
		return;

	if (!fi->Dispatch(context, s_presentCopy.get(), hudLessRes, backBufferTex.get(), deltaMs, reset, frameID))
		return;
	s_lastDispatchedHUDLessFrameID = hudLessFrameID;
	upscaling->hudLessFrameValid[0] = false;
	ResetPreNGFrameGenDispatchBlockLog();

	static bool s_loggedFirstDispatch = false;
	if (!s_loggedFirstDispatch) {
		logger::info("[FrameGen] PreNG first D3D11 dispatch submitted (frameID={}, hudLess={})", frameID, hudLessRes != nullptr);
		logger::default_logger()->flush();
		s_loggedFirstDispatch = true;
	}
}

void PreNG_FrameGen_InitForSwapChain(ID3D11Device* a_device, IDXGISwapChain* a_swapChain)
{
	if (!Upscaling::GetSingleton()->UsesFSRFrameGeneration())
		return;

	auto* fi = FidelityFX_DX11::GetSingleton();
	if (fi->IsReady())
		return;

	DXGI_SWAP_CHAIN_DESC scDesc{};
	if (FAILED(a_swapChain->GetDesc(&scDesc)))
		return;

	uint32_t displayW = scDesc.BufferDesc.Width;
	uint32_t displayH = scDesc.BufferDesc.Height;
	float ratioW = 1.0f;
	float ratioH = 1.0f;
	if (auto* rtManager = fo4cs::RE::GetRenderTargetManager()) {
		ratioW = rtManager->dynamicWidthRatio;
		ratioH = rtManager->dynamicHeightRatio;
	}
	// dynamicWidthRatio/HeightRatio may be 0 during early init — fall back to 1.0
	if (ratioW <= 0.0f) ratioW = 1.0f;
	if (ratioH <= 0.0f) ratioH = 1.0f;
	uint32_t renderW = std::max(1u, static_cast<uint32_t>(displayW * ratioW));
	uint32_t renderH = std::max(1u, static_cast<uint32_t>(displayH * ratioH));

	logger::info("[FrameGen] PreNG InitForSwapChain ({}x{} display, {}x{} render, fmt={})",
		displayW, displayH, renderW, renderH, static_cast<uint32_t>(scDesc.BufferDesc.Format));
	logger::default_logger()->flush();

	if (!fi->Initialize(a_device, displayW, displayH, renderW, renderH, scDesc.BufferDesc.Format)) {
		logger::warn("[FrameGen] PreNG D3D11-native FrameGen unavailable; continuing without FrameGen");
		logger::default_logger()->flush();
		return;
	}

	DX11Hooks::SetPresentCallback(&PreNG_FrameGen_PresentCallback);
	logger::info("[FrameGen] PreNG D3D11-native FrameGen initialized ({}x{} -> {}x{})",
		renderW, renderH, displayW, displayH);
}

bool PreNG_FSR_Upscaling_Setup(ID3D11Device* a_device, uint32_t a_maxRenderWidth, uint32_t a_maxRenderHeight, uint32_t a_outputWidth, uint32_t a_outputHeight)
{
	return FidelityFX_DX11::GetSingleton()->InitializeUpscaling(a_device, a_maxRenderWidth, a_maxRenderHeight, a_outputWidth, a_outputHeight);
}

bool PreNG_FSR_Upscale(
	ID3D11DeviceContext* a_context,
	ID3D11Resource* a_color,
	ID3D11Resource* a_output,
	ID3D11Resource* a_depth,
	ID3D11Resource* a_motionVectors,
	float2 a_jitter,
	float2 a_renderSize,
	float2 a_displaySize,
	uint a_qualityMode)
{
	return FidelityFX_DX11::GetSingleton()->Upscale(
		a_context,
		a_color,
		a_output,
		a_depth,
		a_motionVectors,
		a_jitter,
		a_renderSize,
		a_displaySize,
		a_qualityMode);
}

void PreNG_FSR_Upscaling_Destroy()
{
	FidelityFX_DX11::GetSingleton()->DestroyUpscaling();
}

#endif  // FALLOUT_PRE_NG
