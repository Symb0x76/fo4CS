#include "DX12SwapChain.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <dx12/ffx_api_dx12.hpp>
#include <dx12/ffx_api_framegeneration_dx12.hpp>
#include <dxgi1_6.h>
#include <string_view>

#include <d3dcompiler.h>
#include <directx/d3dx12.h>

#include "FidelityFX.h"
#include "HDRCalibration.h"
#include "Overlay/Overlay.h"
#include "Streamline.h"
#include "Upscaler.h"

extern bool enbLoaded;

namespace
{
	constexpr const char* kColorSpaceShader = R"(
cbuffer HDRColorSpaceCB : register(b0)
{
    uint2 dimensions;
    float peakNits;
    float paperWhiteNits;
    float scRGBReferenceNits;
    uint hdrMode;
    float padding;
};

Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearSampler : register(s0);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexID : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };
    const float2 uvs[3] = {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };

    VSOut output;
    output.position = float4(positions[vertexID], 0.0, 1.0);
    output.uv = uvs[vertexID];
    return output;
}

float3 sRGBToLinear(float3 srgb)
{
    float3 lo = srgb / 12.92;
    float3 hi = pow((srgb + 0.055) / 1.055, 2.4);
    return lerp(lo, hi, step(0.04045, srgb));
}

static const float3x3 Rec709ToRec2020 = {
    { 0.6274, 0.3293, 0.0433 },
    { 0.0691, 0.9195, 0.0114 },
    { 0.0164, 0.0880, 0.8956 }
};

float3 LinearToPQ(float3 linearNits, float peakNitsValue)
{
    float3 y = saturate(linearNits / max(peakNitsValue, 1.0));
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float3 yPow = pow(y, m1);
    return pow((c1 + c2 * yPow) / (1.0 + c3 * yPow), m2);
}

float4 PSMain(VSOut input) : SV_Target
{
    float4 source = SourceTexture.SampleLevel(LinearSampler, input.uv, 0);
    float3 linearColor = sRGBToLinear(saturate(source.rgb));

    if (hdrMode == 1)
    {
        float scRGBScale = paperWhiteNits / max(scRGBReferenceNits, 1.0);
        return float4(linearColor * scRGBScale, source.a);
    }

    float3 rec2020 = mul(Rec709ToRec2020, linearColor);
    float3 pq = LinearToPQ(rec2020 * paperWhiteNits, peakNits);
    return float4(pq, source.a);
}
)";

struct ColorSpaceConstants
{
    std::uint32_t dimensions[2];
    float peakNits;
    float paperWhiteNits;
    float scRGBReferenceNits;
    std::uint32_t hdrMode;
    float padding;
};
std::string FormatHRESULT(HRESULT hr)
	{
		return std::format("0x{:08X}", static_cast<std::uint32_t>(hr));
	}

	enum class PresentTracePhase
	{
		kNone,
		kLoading,
		kGameplay
	};

	const char* GetPresentTracePhaseName(PresentTracePhase phase)
	{
		switch (phase) {
		case PresentTracePhase::kLoading:
			return "loading";
		case PresentTracePhase::kGameplay:
			return "gameplay";
		default:
			return "none";
		}
	}

	bool ShouldTracePresentFrame(uint64_t presentID, PresentTracePhase tracePhase)
	{
		const auto settings = Upscaling::GetSingleton()->settings;
		if (!settings.debugLogging || settings.debugFrameLogCount <= 0) {
			return false;
		}

		const auto bootstrapFrames = static_cast<uint64_t>(std::min(settings.debugFrameLogCount, 12));
		if (presentID < bootstrapFrames) {
			return true;
		}

		static PresentTracePhase previousTracePhase = PresentTracePhase::kNone;
		static uint64_t traceWindowStart = UINT64_MAX;

		if (tracePhase != PresentTracePhase::kNone && tracePhase != previousTracePhase) {
			traceWindowStart = presentID;
			logger::info(
				"[DX12SwapChain] Present trace window started at present={} phase={} for {} frames",
				presentID,
				GetPresentTracePhaseName(tracePhase),
				settings.debugFrameLogCount);
		}
		previousTracePhase = tracePhase;

		return traceWindowStart != UINT64_MAX &&
			presentID >= traceWindowStart &&
			presentID - traceWindowStart < static_cast<uint64_t>(settings.debugFrameLogCount);
	}

	struct ScopedPresentTraceFlag
	{
		explicit ScopedPresentTraceFlag(Upscaling* a_upscaling, bool a_enabled) :
			upscaling(a_upscaling)
		{
			if (upscaling) {
				upscaling->debugTraceCurrentPresent = a_enabled;
			}
		}

		~ScopedPresentTraceFlag()
		{
			if (upscaling) {
				upscaling->debugTraceCurrentPresent = false;
			}
		}

		Upscaling* upscaling;
	};

	const char* GetFrameGenerationBackendName(bool dlss, bool fsr)
	{
		if (dlss) {
			return "DLSS-G";
		}
		if (fsr) {
			return "FSR-FG";
		}
		return "none";
	}

	DXGI_FORMAT GetHDRSwapChainFormat(const HDRSettings& settings)
	{
		switch (settings.GetMode()) {
		case HDRMode::kScRGB:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case HDRMode::kHDR10:
			return DXGI_FORMAT_R10G10B10A2_UNORM;
		default:
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	DXGI_COLOR_SPACE_TYPE GetHDRColorSpace(const HDRSettings& settings)
	{
		switch (settings.GetMode()) {
		case HDRMode::kScRGB:
			return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
		case HDRMode::kHDR10:
			return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
		default:
			return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
		}
	}

	UINT16 NitsToDXGIHDRValue(float nits)
	{
		return static_cast<UINT16>(std::clamp(nits, 0.0f, 65535.0f));
	}

	winrt::com_ptr<ID3DBlob> CompileColorSpaceShader(const char* entryPoint, const char* target)
	{
		winrt::com_ptr<ID3DBlob> shaderBlob;
		winrt::com_ptr<ID3DBlob> errorBlob;
		const auto result = D3DCompile(
			kColorSpaceShader,
			std::strlen(kColorSpaceShader),
			"HDRColorSpace",
			nullptr,
			nullptr,
			entryPoint,
			target,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0,
			shaderBlob.put(),
			errorBlob.put());
		if (FAILED(result)) {
			logger::warn("[HDR] Color space shader compile failed: {}", errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "unknown");
			DX::ThrowIfFailed(result);
		}
		return shaderBlob;
	}
}

void DX12SwapChain::CreateD3D12Device(IDXGIAdapter* a_adapter)
{
	DX::ThrowIfFailed(D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&d3d12Device)));
	if (ID3D12Device* upgradedDevice = d3d12Device.get(); Streamline::GetSingleton()->UpgradeD3D12DeviceForDLSSG(&upgradedDevice) && upgradedDevice != d3d12Device.get()) {
		d3d12Device.attach(upgradedDevice);
	}

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	queueDesc.NodeMask = 0;

	DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
	Streamline::GetSingleton()->LogD3D12CommandQueueProxyState(commandQueue.get());

	for (int i = 0; i < 2; i++) {
		DX::ThrowIfFailed(d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i])));
		DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[i].get(), nullptr, IID_PPV_ARGS(&commandLists[i])));
		commandLists[i]->Close();
	}
}

void DX12SwapChain::CreateSwapChain(IDXGIFactory4* a_dxgiFactory, DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
	hdrSettings = LoadHDRSettingsFromINI();
	swapChainDesc = {};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Width = a_swapChainDesc.BufferDesc.Width;
	swapChainDesc.Height = a_swapChainDesc.BufferDesc.Height;
	swapChainDesc.Format = a_swapChainDesc.BufferDesc.Format;
	if (const auto hdrFormat = GetHDRSwapChainFormat(hdrSettings); hdrFormat != DXGI_FORMAT_UNKNOWN) {
		swapChainDesc.Format = hdrFormat;
	}
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	BOOL allowTearing = FALSE;
	if (winrt::com_ptr<IDXGIFactory5> dxgiFactory5; SUCCEEDED(a_dxgiFactory->QueryInterface(IID_PPV_ARGS(dxgiFactory5.put())))) {
		DX::ThrowIfFailed(dxgiFactory5->CheckFeatureSupport(
			DXGI_FEATURE_PRESENT_ALLOW_TEARING,
			&allowTearing,
			sizeof(allowTearing)
		));
	}

	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
	if (allowTearing) {
		swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	}

	auto upscaling = Upscaling::GetSingleton();
	auto fidelityFX = FidelityFX::GetSingleton();
	auto streamline = Streamline::GetSingleton();
	const bool useFidelityFXSwapChain = upscaling->UsesFSRFrameGeneration() && fidelityFX->module;
	IDXGIFactory4* dxgiFactory = a_dxgiFactory;
	winrt::com_ptr<IDXGIFactory4> streamlineFactory;
	const bool useStreamlineFactory = streamline->UpgradeDXGIFactoryForDLSSG(&dxgiFactory);
	if (useStreamlineFactory && dxgiFactory != a_dxgiFactory) {
		streamlineFactory.attach(dxgiFactory);
	}
	logger::info(
		"[DX12SwapChain] Creating D3D12 proxy swap chain {}x{} fmt={} flags=0x{:X} backend={} hdrMode={}",
		swapChainDesc.Width,
		swapChainDesc.Height,
		static_cast<uint32_t>(swapChainDesc.Format),
		swapChainDesc.Flags,
		useFidelityFXSwapChain ? "FidelityFX" : "native",
		hdrSettings.hdrMode);

	const auto createNativeSwapChain = [&]() {
		winrt::com_ptr<IDXGISwapChain1> nativeSwapChain;
		DX::ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
			commandQueue.get(),
			a_swapChainDesc.OutputWindow,
			&swapChainDesc,
			nullptr,
			nullptr,
			nativeSwapChain.put()));
		DX::ThrowIfFailed(nativeSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain)));
	};

	if (useFidelityFXSwapChain) {
		ffx::CreateContextDescFrameGenerationSwapChainForHwndDX12 ffxSwapChainDesc{};

		ffxSwapChainDesc.desc = &swapChainDesc;
		ffxSwapChainDesc.dxgiFactory = a_dxgiFactory;
		ffxSwapChainDesc.fullscreenDesc = nullptr;
		ffxSwapChainDesc.gameQueue = commandQueue.get();
		ffxSwapChainDesc.hwnd = a_swapChainDesc.OutputWindow;
		ffxSwapChainDesc.swapchain = &swapChain;

		if (ffx::CreateContext(fidelityFX->swapChainContext, nullptr, ffxSwapChainDesc) != ffx::ReturnCode::Ok || !swapChain) {
			logger::error("[FidelityFX] Failed to create swap chain context, using native D3D12 swap chain");
			swapChain = nullptr;
			fidelityFX->swapChainContext = nullptr;
			createNativeSwapChain();
		}
	} else {
		createNativeSwapChain();
	}

	if (!useStreamlineFactory) {
		streamline->UpgradeSwapChainForDLSSG(&swapChain);
	}

	if (hdrSettings.IsEnabled()) {
		const auto colorSpace = GetHDRColorSpace(hdrSettings);
		const auto colorSpaceResult = swapChain->SetColorSpace1(colorSpace);
		if (FAILED(colorSpaceResult)) {
			logger::warn("[HDR] SetColorSpace1({}) failed: {}", static_cast<uint32_t>(colorSpace), FormatHRESULT(colorSpaceResult));
		} else {
			logger::info("[HDR] Color space set to {}", static_cast<uint32_t>(colorSpace));
		}

		if (hdrSettings.GetMode() == HDRMode::kHDR10) {
			DXGI_HDR_METADATA_HDR10 metadata{};
			metadata.RedPrimary[0] = 34000;
			metadata.RedPrimary[1] = 16000;
			metadata.GreenPrimary[0] = 13250;
			metadata.GreenPrimary[1] = 34500;
			metadata.BluePrimary[0] = 7500;
			metadata.BluePrimary[1] = 3000;
			metadata.WhitePoint[0] = 15635;
			metadata.WhitePoint[1] = 16450;
			metadata.MaxMasteringLuminance = static_cast<UINT>(std::clamp(hdrSettings.peakLuminance, 80.0f, 10000.0f) * 10000.0f);
			metadata.MinMasteringLuminance = 0;
			metadata.MaxContentLightLevel = NitsToDXGIHDRValue(hdrSettings.peakLuminance);
			metadata.MaxFrameAverageLightLevel = NitsToDXGIHDRValue(hdrSettings.paperWhiteLuminance);
			const auto metadataResult = swapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata);
			if (FAILED(metadataResult)) {
				logger::warn("[HDR] SetHDRMetaData failed: {}", FormatHRESULT(metadataResult));
			} else {
				logger::info("[HDR] HDR10 metadata set (peak={}, paperWhite={})", hdrSettings.peakLuminance, hdrSettings.paperWhiteLuminance);
			}
		}
	}

	DX::ThrowIfFailed(swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainBuffers[0])));
	DX::ThrowIfFailed(swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainBuffers[1])));

	frameIndex = swapChain->GetCurrentBackBufferIndex();
	logger::info("[DX12SwapChain] Swap chain ready (frameIndex={}, buffers={})", frameIndex, swapChainDesc.BufferCount);

	if (useFidelityFXSwapChain && fidelityFX->swapChainContext != nullptr)
		fidelityFX->SetupFrameGeneration();

	swapChainProxy = new DXGISwapChainProxy(swapChain);

	if (!settingsOverlay) {
		settingsOverlay = Overlay::GetSingleton();
		settingsOverlay->Initialize(d3d12Device.get(), commandQueue.get(), swapChain, swapChainDesc.Format, a_swapChainDesc.OutputWindow);
		if (Streamline::GetSingleton()->initialized) {
		}
	}

}

void DX12SwapChain::CreateInterop()
{
	HANDLE sharedFenceHandle;
	DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
	DX::ThrowIfFailed(d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
	DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
	CloseHandle(sharedFenceHandle);
	d3d12FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (!d3d12FenceEvent) {
		DX::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}

	D3D11_TEXTURE2D_DESC texDesc11{};
	texDesc11.Width = swapChainDesc.Width;
	texDesc11.Height = swapChainDesc.Height;
	texDesc11.MipLevels = 1;
	texDesc11.ArraySize = 1;
	texDesc11.Format = swapChainDesc.Format;
	texDesc11.SampleDesc.Count = 1;
	texDesc11.SampleDesc.Quality = 0;
	texDesc11.Usage = D3D11_USAGE_DEFAULT;
	texDesc11.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	texDesc11.CPUAccessFlags = 0;
	texDesc11.MiscFlags = 0;

	if (enbLoaded)
		swapChainBufferProxyENB = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
	else
		swapChainBufferProxy = new Texture2D(texDesc11);

	swapChainBufferWrapped[0] = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
	swapChainBufferWrapped[1] = new WrappedResource(texDesc11, d3d11Device.get(), d3d12Device.get());
}

DXGISwapChainProxy* DX12SwapChain::GetSwapChainProxy()
{
	return swapChainProxy;
}

void DX12SwapChain::SetD3D11Device(ID3D11Device* a_d3d11Device)
{
	DX::ThrowIfFailed(a_d3d11Device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));
}

void DX12SwapChain::SetD3D11DeviceContext(ID3D11DeviceContext* a_d3d11Context)
{
	DX::ThrowIfFailed(a_d3d11Context->QueryInterface(IID_PPV_ARGS(&d3d11Context)));
}

HRESULT DX12SwapChain::GetBuffer(void** ppSurface)
{
	if (enbLoaded)
		*ppSurface = swapChainBufferProxyENB->resource11;
	else
		*ppSurface = swapChainBufferProxy->resource.get();
	return S_OK;
}

HRESULT DX12SwapChain::Present(UINT SyncInterval, UINT Flags)
{
	static std::atomic_uint64_t presentCounter{ 0 };
	static bool lastFrameGenerationActive = false;
	static const char* lastFrameGenerationBackend = "none";

	const auto presentID = presentCounter.fetch_add(1, std::memory_order_relaxed);
	auto upscaling = Upscaling::GetSingleton();
	auto streamline = Streamline::GetSingleton();
	bool gameActive = false;
	bool inMenuMode = true;
	bool uiDirectionalBlock = false;
	bool loadingMenuOpen = false;
	if (auto main = RE::Main::GetSingleton()) {
		gameActive = main->gameActive;
		inMenuMode = main->inMenuMode;
	}
	if (auto ui = RE::UI::GetSingleton()) {
		uiDirectionalBlock = ui->movementToDirectionalCount != 0;
		loadingMenuOpen = ui->GetMenuOpen("LoadingMenu");
	}

	const auto tracePhase = loadingMenuOpen ? PresentTracePhase::kLoading :
		(gameActive && !inMenuMode ? PresentTracePhase::kGameplay : PresentTracePhase::kNone);
	const bool traceFrame = ShouldTracePresentFrame(presentID, tracePhase);
	ScopedPresentTraceFlag scopedTraceFlag(upscaling, traceFrame);
	const char* stage = "begin";
	const auto trace = [&](const char* nextStage) {
		stage = nextStage;
	};

	try {
		if (traceFrame) {
			logger::debug("[DX12SwapChain] Present#{} begin frameIndex={}", presentID, frameIndex);
		}
		trace("reflex-sleep");
		streamline->SleepReflexFrame("present");


		if (auto latestHDRSettings = ReloadCalibrationSettings(); latestHDRSettings.calibrationActive) {
			hdrSettings = latestHDRSettings;
			WaitForCommandAllocator(frameIndex);
			trace("reset-command-list-calibration");
			DX::ThrowIfFailed(commandAllocators[frameIndex]->Reset());
			DX::ThrowIfFailed(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr));

			if (!calibrationOverlay) {
				calibrationOverlay = new HDRCalibrationOverlay();
			}
			if (calibrationOverlay->Render(d3d12Device.get(), commandQueue.get(), swapChain, commandLists[frameIndex].get(), swapChainBuffers[frameIndex].get(), swapChainDesc.Format, hdrSettings)) {
				trace("close-command-list-calibration");
				DX::ThrowIfFailed(commandLists[frameIndex]->Close());
				ID3D12CommandList* calibrationLists[] = { commandLists[frameIndex].get() };
				commandQueue->ExecuteCommandLists(1, calibrationLists);
			streamline->SetPCLMarker(sl::PCLMarker::ePresentStart, "present-start");
				const auto presentResult = swapChain->Present(SyncInterval, Flags);
				if (FAILED(presentResult)) {
					logger::error("[DX12SwapChain] IDXGISwapChain::Present failed during calibration: {}", FormatHRESULT(presentResult));
					streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame(); return presentResult;
				}
				DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));
				commandAllocatorFenceValues[frameIndex] = fenceValue;
				DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
				fenceValue++;
				streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame();
				frameIndex = swapChain->GetCurrentBackBufferIndex();
				return S_OK;
			}

			DX::ThrowIfFailed(commandLists[frameIndex]->Close());
			ID3D12CommandList* fallbackLists[] = { commandLists[frameIndex].get() };
			commandQueue->ExecuteCommandLists(1, fallbackLists);
			streamline->SetPCLMarker(sl::PCLMarker::ePresentStart, "present-start");
			const auto fallbackPresentResult = swapChain->Present(SyncInterval, Flags);
			if (FAILED(fallbackPresentResult)) {
				logger::error("[DX12SwapChain] IDXGISwapChain::Present failed after calibration fallback: {}", FormatHRESULT(fallbackPresentResult));
				streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame(); return fallbackPresentResult;
			}
			DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));
			commandAllocatorFenceValues[frameIndex] = fenceValue;
			DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
			fenceValue++;
			streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame();
			frameIndex = swapChain->GetCurrentBackBufferIndex();
			return S_OK;
		}

		ID3D11Texture2D* finalFrame = enbLoaded ? swapChainBufferProxyENB->resource11 : swapChainBufferProxy->resource.get();

		trace("copy-d3d11-proxy-to-shared");
		if (enbLoaded)
			d3d11Context->CopyResource(swapChainBufferWrapped[frameIndex]->resource11, finalFrame);
		else
			d3d11Context->CopyResource(swapChainBufferWrapped[frameIndex]->resource11, finalFrame);

		trace("wait-d3d11-to-d3d12");
		DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
		DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
		fenceValue++;

		WaitForCommandAllocator(frameIndex);
		trace("reset-command-list");
		DX::ThrowIfFailed(commandAllocators[frameIndex]->Reset());
		DX::ThrowIfFailed(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr));

		auto fakeSwapChain = swapChainBufferWrapped[frameIndex]->resource.get();
		auto realSwapChain = swapChainBuffers[frameIndex].get();

		if (hdrSettings.IsEnabled()) {
			trace("hdr-color-space-conversion");
			EnsureColorSpaceResources();

			ColorSpaceConstants constants{};
			constants.dimensions[0] = swapChainDesc.Width;
			constants.dimensions[1] = swapChainDesc.Height;
			constants.peakNits = hdrSettings.peakLuminance;
			constants.paperWhiteNits = hdrSettings.paperWhiteLuminance;
			constants.scRGBReferenceNits = hdrSettings.scRGBReferenceLuminance;
			constants.hdrMode = static_cast<std::uint32_t>(hdrSettings.hdrMode);
			std::memcpy(colorSpaceMappedConstants, &constants, sizeof(constants));

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = swapChainDesc.Format;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Texture2D.MipLevels = 1;

			auto srvCpu = colorSpaceSrvHeap->GetCPUDescriptorHandleForHeapStart();
			d3d12Device->CreateShaderResourceView(fakeSwapChain, &srvDesc, srvCpu);

			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = swapChainDesc.Format;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			auto rtvCpu = colorSpaceRtvHeap->GetCPUDescriptorHandleForHeapStart();
			d3d12Device->CreateRenderTargetView(realSwapChain, &rtvDesc, rtvCpu);

			{
				std::vector<D3D12_RESOURCE_BARRIER> barriers;
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
				commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
			}

			ID3D12DescriptorHeap* heaps[] = { colorSpaceSrvHeap.get() };
			commandLists[frameIndex]->SetDescriptorHeaps(1, heaps);

			D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(swapChainDesc.Width), static_cast<float>(swapChainDesc.Height), 0.0f, 1.0f };
			D3D12_RECT scissor{ 0, 0, static_cast<LONG>(swapChainDesc.Width), static_cast<LONG>(swapChainDesc.Height) };
			commandLists[frameIndex]->RSSetViewports(1, &viewport);
			commandLists[frameIndex]->RSSetScissorRects(1, &scissor);
			commandLists[frameIndex]->OMSetRenderTargets(1, &rtvCpu, FALSE, nullptr);
			commandLists[frameIndex]->SetGraphicsRootSignature(colorSpaceRootSignature.get());
			commandLists[frameIndex]->SetPipelineState(colorSpacePipelineState.get());
			commandLists[frameIndex]->SetGraphicsRootConstantBufferView(0, colorSpaceConstantBuffer->GetGPUVirtualAddress());
			commandLists[frameIndex]->SetGraphicsRootDescriptorTable(1, colorSpaceSrvHeap->GetGPUDescriptorHandleForHeapStart());
			commandLists[frameIndex]->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandLists[frameIndex]->DrawInstanced(3, 1, 0, 0);

			{
				std::vector<D3D12_RESOURCE_BARRIER> barriers;
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
				commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
			}
		} else {
			trace("copy-shared-to-backbuffer");
			{
				std::vector<D3D12_RESOURCE_BARRIER> barriers;
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST));
				commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
			}

			commandLists[frameIndex]->CopyResource(realSwapChain, fakeSwapChain);

			{
				std::vector<D3D12_RESOURCE_BARRIER> barriers;
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON));
				barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT));
				commandLists[frameIndex]->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
			}
		}

		bool useFrameGenerationThisFrame = false;
		auto fidelityFX = FidelityFX::GetSingleton();
		const bool useDLSSFrameGeneration = upscaling->UsesDLSSFrameGeneration() && streamline->featureDLSSG;
		const bool useFSRFrameGeneration = upscaling->UsesFSRFrameGeneration() && fidelityFX->featureFrameGen;
		const bool frameGenerationBackendAvailable = useDLSSFrameGeneration || useFSRFrameGeneration;
		const char* frameGenerationBackend = GetFrameGenerationBackendName(useDLSSFrameGeneration, useFSRFrameGeneration);

		bool skipFgAfterLoading = false;
		{
			static bool wasLoading = false;
			if (!loadingMenuOpen && wasLoading) {
				skipFgAfterLoading = true;
				upscaling->postLoadingSkipUpscale = true;
			}
			wasLoading = loadingMenuOpen;
		}

		useFrameGenerationThisFrame = frameGenerationBackendAvailable && gameActive && !inMenuMode && !loadingMenuOpen && !uiDirectionalBlock && !skipFgAfterLoading;

		if (upscaling->pluginMode != Upscaling::PluginMode::kReflex &&
			(presentID == 0 || lastFrameGenerationActive != useFrameGenerationThisFrame || std::string_view(lastFrameGenerationBackend) != frameGenerationBackend)) {
			logger::info(
				"[FrameGen] present={} backend={} active={} available={} phase={} gameActive={} inMenu={} loadingMenu={} uiBlock={}",
				presentID,
				frameGenerationBackend,
				useFrameGenerationThisFrame,
				frameGenerationBackendAvailable,
				GetPresentTracePhaseName(tracePhase),
				gameActive,
				inMenuMode,
				loadingMenuOpen,
				uiDirectionalBlock);
			lastFrameGenerationActive = useFrameGenerationThisFrame;
			lastFrameGenerationBackend = frameGenerationBackend;
		}

		trace("frame-generation");
		if (useDLSSFrameGeneration) {
			const bool dlssgTagged = streamline->TagResourcesAndConfigure(
				upscaling->HUDLessBufferShared12[frameIndex].get(),
				nullptr,
				upscaling->depthBufferShared12[frameIndex].get(),
				upscaling->motionVectorBufferShared12[frameIndex].get(),
				useFrameGenerationThisFrame);
			if (!dlssgTagged && useFrameGenerationThisFrame) {
				logger::warn("[FrameGen] DLSS-G skipped this frame after Streamline tagging/configuration failure");
				useFrameGenerationThisFrame = false;
			}
		}

		if (useFSRFrameGeneration) {
			fidelityFX->Present(useFrameGenerationThisFrame);
		}


	trace("overlay");
	if (settingsOverlay && settingsOverlay->IsVisible()) {
		auto* backBuffer = swapChainBuffers[frameIndex].get();
		CD3DX12_RESOURCE_BARRIER toRT = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		commandLists[frameIndex]->ResourceBarrier(1, &toRT);
		settingsOverlay->Render(commandLists[frameIndex].get(), backBuffer, swapChainDesc.Format);
		CD3DX12_RESOURCE_BARRIER toPresent = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		commandLists[frameIndex]->ResourceBarrier(1, &toPresent);
	}

		trace("close-command-list");
		DX::ThrowIfFailed(commandLists[frameIndex]->Close());

		trace("execute-command-list");
		ID3D12CommandList* commandListsToExecute[] = { commandLists[frameIndex].get() };
		commandQueue->ExecuteCommandLists(1, commandListsToExecute);

		// Fix FPS cap being e.g. 55 instead of 60
		if (!upscaling->highFPSPhysicsFixLoaded && SyncInterval > 0)
			SyncInterval = 1;

		streamline->SetPCLMarker(sl::PCLMarker::ePresentStart, "present-start");
		trace("present");
		const auto presentResult = swapChain->Present(SyncInterval, Flags);
		if (FAILED(presentResult)) {
			logger::error("[DX12SwapChain] IDXGISwapChain::Present failed: {}", FormatHRESULT(presentResult));
			streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame(); return presentResult;
		}

		streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end");

		if (useDLSSFrameGeneration) {
			trace("dlssg-present-state");
			streamline->LogDLSSGPresentState(useFrameGenerationThisFrame, presentID);
		}

		trace("wait-d3d12-to-d3d11");
		DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));
		commandAllocatorFenceValues[frameIndex] = fenceValue;
		DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
		fenceValue++;

		streamline->AdvanceFrame();

		trace("skip-frame-latency-wait");

		trace("update-frame-index");
		frameIndex = swapChain->GetCurrentBackBufferIndex();

		trace("reset-shared-resources");
		if (frameGenerationBackendAvailable) {
			upscaling->Reset();
		}

		trace("game-frame-limiter");
		if (upscaling->pluginMode != Upscaling::PluginMode::kReflex && !upscaling->highFPSPhysicsFixLoaded)
			upscaling->GameFrameLimiter();

		trace("frame-limiter");
		if (upscaling->pluginMode != Upscaling::PluginMode::kReflex && SyncInterval == 0)
			upscaling->FrameLimiter(useFrameGenerationThisFrame);

		if (traceFrame) {
			logger::debug("[DX12SwapChain] Present#{} completed (nextFrameIndex={})", presentID, frameIndex);
		}

		return S_OK;
	} catch (const winrt::hresult_error& e) {
		const auto hr = static_cast<HRESULT>(e.code());
		logger::error("[DX12SwapChain] Present failed at stage '{}' with HRESULT {}", stage, FormatHRESULT(hr));
		commandLists[frameIndex]->Close();
		commandAllocators[frameIndex]->Reset();
		commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr);
		streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame();
		return hr;
	} catch (const std::exception& e) {
		logger::error("[DX12SwapChain] Present failed at stage '{}': {}", stage, e.what());
		commandLists[frameIndex]->Close();
		commandAllocators[frameIndex]->Reset();
		commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr);
		streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame();
		return DXGI_ERROR_DEVICE_REMOVED;
	} catch (...) {
		logger::error("[DX12SwapChain] Present failed at stage '{}' with unknown exception", stage);
		commandLists[frameIndex]->Close();
		commandAllocators[frameIndex]->Reset();
		commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr);
		streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame();
		return DXGI_ERROR_DEVICE_REMOVED;
	}
}

HRESULT DX12SwapChain::GetDevice(REFIID uuid, void** ppDevice)
{
	if (uuid == __uuidof(ID3D11Device) || uuid == __uuidof(ID3D11Device1) || uuid == __uuidof(ID3D11Device2) || uuid == __uuidof(ID3D11Device3) || uuid == __uuidof(ID3D11Device4) || uuid == __uuidof(ID3D11Device5)) {
		*ppDevice = d3d11Device.get();
		return S_OK;
	}

	return swapChain->GetDevice(uuid, ppDevice);
}

void DX12SwapChain::WaitForCommandAllocator(UINT a_index)
{
	const auto waitFenceValue = commandAllocatorFenceValues[a_index];
	if (waitFenceValue == 0 || d3d12Fence->GetCompletedValue() >= waitFenceValue) {
		return;
	}

	DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(waitFenceValue, d3d12FenceEvent));
	const auto waitResult = WaitForSingleObject(d3d12FenceEvent, 1000);
	if (waitResult == WAIT_OBJECT_0) {
		return;
	}

	logger::warn("[DX12SwapChain] Timed out waiting for command allocator {} fence={} completed={}",
		a_index,
		waitFenceValue,
		d3d12Fence->GetCompletedValue());
	DX::ThrowIfFailed(HRESULT_FROM_WIN32(waitResult == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError()));
}

ID3D12GraphicsCommandList4* DX12SwapChain::BeginInteropCommandList()
{
	DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
	DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
	fenceValue++;

	WaitForCommandAllocator(frameIndex);
	DX::ThrowIfFailed(commandAllocators[frameIndex]->Reset());
	DX::ThrowIfFailed(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr));
	return commandLists[frameIndex].get();
}

void DX12SwapChain::ExecuteInteropCommandListAndWait()
{
	DX::ThrowIfFailed(commandLists[frameIndex]->Close());

	ID3D12CommandList* lists[] = { commandLists[frameIndex].get() };
	commandQueue->ExecuteCommandLists(1, lists);

	DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));
	commandAllocatorFenceValues[frameIndex] = fenceValue;
	DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
	fenceValue++;
}

void DX12SwapChain::EnsureColorSpaceResources()
{
	if (colorSpacePipelineState && colorSpaceConstantBuffer) {
		return;
	}

	CD3DX12_DESCRIPTOR_RANGE srvRange{};
	srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_ROOT_PARAMETER rootParams[2]{};
	rootParams[0].InitAsConstantBufferView(0);
	rootParams[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_STATIC_SAMPLER_DESC staticSampler{};
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.ShaderRegister = 0;
	staticSampler.RegisterSpace = 0;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc{};
	rootSigDesc.Init(2, rootParams, 1, &staticSampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	winrt::com_ptr<ID3DBlob> signatureBlob;
	winrt::com_ptr<ID3DBlob> errorBlob;
	DX::ThrowIfFailed(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.put(), errorBlob.put()));
	DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(colorSpaceRootSignature.put())));

	const auto vertexShader = CompileColorSpaceShader("VSMain", "vs_5_0");
	const auto pixelShader = CompileColorSpaceShader("PSMain", "ps_5_0");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = colorSpaceRootSignature.get();
	psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
	psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = swapChainDesc.Format;
	psoDesc.SampleDesc.Count = 1;
	DX::ThrowIfFailed(d3d12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(colorSpacePipelineState.put())));

	const auto constantBufferSize = static_cast<UINT64>((sizeof(ColorSpaceConstants) + 255) & ~255);
	const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);
	DX::ThrowIfFailed(d3d12Device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(colorSpaceConstantBuffer.put())));
	DX::ThrowIfFailed(colorSpaceConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&colorSpaceMappedConstants)));

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	DX::ThrowIfFailed(d3d12Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(colorSpaceSrvHeap.put())));

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	DX::ThrowIfFailed(d3d12Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(colorSpaceRtvHeap.put())));

	logger::info("[HDR] Color space conversion resources created for fmt={}", static_cast<std::uint32_t>(swapChainDesc.Format));
}

void DX12SwapChain::DestroyColorSpaceResources()
{
	colorSpaceMappedConstants = nullptr;
	colorSpaceConstantBuffer = nullptr;
	colorSpacePipelineState = nullptr;
	colorSpaceRootSignature = nullptr;
	colorSpaceSrvHeap = nullptr;
	colorSpaceRtvHeap = nullptr;
}

WrappedResource::WrappedResource(D3D11_TEXTURE2D_DESC a_texDesc, ID3D11Device5* a_d3d11Device, ID3D12Device* a_d3d12Device)
{
	// Create D3D11 shared texture directly instead of wrapping D3D12 resource
	a_texDesc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	DX::ThrowIfFailed(a_d3d11Device->CreateTexture2D(&a_texDesc, nullptr, &resource11));

	// Get shared handle from D3D11 texture to enable D3D12 access
	winrt::com_ptr<IDXGIResource1> dxgiResource;
	DX::ThrowIfFailed(resource11->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
	HANDLE sharedHandle = nullptr;
	DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedHandle));

	// Open the shared D3D11 texture as D3D12 resource
	DX::ThrowIfFailed(a_d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(resource.put())));
	CloseHandle(sharedHandle);

	if (a_texDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = a_texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;

		DX::ThrowIfFailed(a_d3d11Device->CreateShaderResourceView(resource11, &srvDesc, &srv));
	}

	if (a_texDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
		if (a_texDesc.ArraySize > 1) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			uavDesc.Texture2DArray.FirstArraySlice = 0;
			uavDesc.Texture2DArray.ArraySize = a_texDesc.ArraySize;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
		}
		else {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = a_texDesc.Format;
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;

			DX::ThrowIfFailed(a_d3d11Device->CreateUnorderedAccessView(resource11, &uavDesc, &uav));
		}
	}

	if (a_texDesc.BindFlags & D3D11_BIND_RENDER_TARGET) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = a_texDesc.Format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvDesc.Texture2D.MipSlice = 0;
		DX::ThrowIfFailed(a_d3d11Device->CreateRenderTargetView(resource11, &rtvDesc, &rtv));
	}
}

DXGISwapChainProxy::DXGISwapChainProxy(IDXGISwapChain4* a_swapChain)
{
	swapChain = a_swapChain;
}

/****IUknown****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(REFIID riid, void** ppvObj)
{
	auto ret = swapChain->QueryInterface(riid, ppvObj);
	if (*ppvObj)
		*ppvObj = this;
	return ret;
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::AddRef()
{
	return swapChain->AddRef();
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::Release()
{
	return swapChain->Release();
}

/****IDXGIObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateData(_In_ REFGUID Name, UINT DataSize, _In_reads_bytes_(DataSize) const void* pData)
{
	return swapChain->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown)
{
	return swapChain->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData)
{
	return swapChain->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent)
{
	return swapChain->GetParent(riid, ppParent);
}

/****IDXGIDeviceSubObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice)
{
	return DX12SwapChain::GetSingleton()->GetDevice(riid, ppDevice);
}

/****IDXGISwapChain****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
	return DX12SwapChain::GetSingleton()->Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(UINT, _In_ REFIID, _COM_Outptr_ void** ppSurface)
{
	return DX12SwapChain::GetSingleton()->GetBuffer(ppSurface);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetFullscreenState(BOOL, _In_opt_ IDXGIOutput*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenState(_Out_opt_ BOOL* pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget)
{
	return swapChain->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc)
{
	return swapChain->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	if (dx12SwapChain->hdrSettings.IsEnabled()) {
		NewFormat = dx12SwapChain->swapChainDesc.Format;
	}
	return swapChain->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeTarget(_In_ const DXGI_MODE_DESC*)
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput)
{
	return swapChain->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats)
{
	return swapChain->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetLastPresentCount(_Out_ UINT* pLastPresentCount)
{
	return swapChain->GetLastPresentCount(pLastPresentCount);
}
