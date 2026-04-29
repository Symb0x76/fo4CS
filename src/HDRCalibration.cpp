#include "HDRCalibration.h"
#include "Overlay/Overlay.h"
#include "Upscaler.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <utility>

#include <d3dcompiler.h>
#include <directx/d3dx12.h>

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace
{
	HDRCalibrationOverlay* g_calibrationOverlay = nullptr;

	constexpr const char* kCalibrationShader = R"(
cbuffer HDRCalibrationCB : register(b0)
{
    float peakNits;
    float paperWhiteNits;
    float scRGBReferenceNits;
    uint hdrMode;
    uint showClippingWarning;
    uint showColorBars;
    float2 padding;
};

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

float3 EncodeNits(float3 nits)
{
    if (hdrMode == 2) {
        return LinearToPQ(nits, peakNits);
    }
    return nits / max(scRGBReferenceNits, 1.0);
}

float GridLine(float2 uv)
{
    float2 grid = abs(frac(uv * float2(10.0, 8.0)) - 0.5);
    return step(0.485, max(grid.x, grid.y)) * 0.08;
}

float4 PSMain(VSOut input) : SV_Target
{
    float2 uv = saturate(input.uv);
    float3 nits = 0.0;

    if (uv.y < 0.22) {
        float blackNits = pow(saturate(uv.x), 2.2) * 5.0;
        nits = blackNits.xxx;
    } else if (uv.y < 0.48) {
        float rampNits = saturate(uv.x) * peakNits;
        nits = rampNits.xxx;
    } else if (uv.y < 0.72 && showColorBars != 0) {
        float bar = floor(saturate(uv.x) * 7.0);
        float3 bars[7] = {
            float3(1.0, 1.0, 1.0),
            float3(1.0, 0.0, 0.0),
            float3(0.0, 1.0, 0.0),
            float3(0.0, 0.0, 1.0),
            float3(0.0, 1.0, 1.0),
            float3(1.0, 0.0, 1.0),
            float3(1.0, 1.0, 0.0)
        };
        nits = bars[(uint)min(bar, 6.0)] * paperWhiteNits;
    } else {
        float2 center = abs(uv - float2(0.5, 0.86));
        float patch = step(center.x, 0.18) * step(center.y, 0.09);
        float background = lerp(20.0, peakNits * 1.1, saturate(uv.x));
        nits = lerp(background.xxx, paperWhiteNits.xxx, patch);
        if (showClippingWarning != 0 && background > peakNits) {
            nits = lerp(nits, float3(peakNits, 0.0, 0.0), 0.75);
        }
    }

    nits += GridLine(uv) * paperWhiteNits;
    return float4(EncodeNits(nits), 1.0);
}
)";

	struct PatternConstants
	{
		float peakNits;
		float paperWhiteNits;
		float scRGBReferenceNits;
		std::uint32_t hdrMode;
		std::uint32_t showClippingWarning;
		std::uint32_t showColorBars;
		float padding[2]{};
	};

	winrt::com_ptr<ID3DBlob> CompileShader(const char* entryPoint, const char* target)
	{
		winrt::com_ptr<ID3DBlob> shaderBlob;
		winrt::com_ptr<ID3DBlob> errorBlob;
		const auto result = D3DCompile(
			kCalibrationShader,
			std::strlen(kCalibrationShader),
			"HDRCalibration",
			nullptr,
			nullptr,
			entryPoint,
			target,
			D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0,
			shaderBlob.put(),
			errorBlob.put());
		if (FAILED(result)) {
			logger::warn("[HDR] Calibration shader compile failed: {}", errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "unknown");
			DX::ThrowIfFailed(result);
		}
		return shaderBlob;
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

	std::uint16_t NitsToHDRValue(float nits)
	{
		return static_cast<std::uint16_t>(std::clamp(nits, 0.0f, 65535.0f));
	}

	void AllocateImGuiDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
	{
		*outCpu = info->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		*outGpu = info->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	}

	void FreeImGuiDescriptor(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)
	{}
}

HDRCalibrationOverlay::~HDRCalibrationOverlay()
{
	Shutdown();
}

bool HDRCalibrationOverlay::IsActive() const
{
	return active;
}

bool HDRCalibrationOverlay::Render(
	ID3D12Device* device,
	ID3D12CommandQueue* commandQueue,
	IDXGISwapChain4* swapChain,
	ID3D12GraphicsCommandList4* commandList,
	ID3D12Resource* backBuffer,
	DXGI_FORMAT swapChainFormat,
	HDRSettings& settings)
{
	if (!device || !commandQueue || !swapChain || !commandList || !backBuffer) {
		return false;
	}

	if (!ReloadActivation(settings)) {
		return false;
	}

	if (!Initialize(device, commandQueue, swapChain, swapChainFormat, settings)) {
		return false;
	}

	RenderPattern(commandList, backBuffer, swapChainFormat, editableSettings);
	RenderUI(settings);
	if (applyMetadataOnClose) {
		ApplyHDRMetadata(swapChain, settings);
		applyMetadataOnClose = false;
	}

	ID3D12DescriptorHeap* heaps[] = { srvHeap.get() };
	commandList->SetDescriptorHeaps(1, heaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

	auto barrierToPresent = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	commandList->ResourceBarrier(1, &barrierToPresent);
	return true;
}

bool HDRCalibrationOverlay::Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue, IDXGISwapChain4* swapChain, DXGI_FORMAT swapChainFormat, const HDRSettings& settings)
{
	(void)settings;
	if (initialized && initializedFormat == swapChainFormat) {
		return true;
	}

	Shutdown();

	DXGI_SWAP_CHAIN_DESC swapChainDesc{};
	if (FAILED(swapChain->GetDesc(&swapChainDesc)) || !swapChainDesc.OutputWindow) {
		hwnd = GetForegroundWindow();
	} else {
		hwnd = swapChainDesc.OutputWindow;
	}
	if (!hwnd) {
		logger::warn("[HDR] Calibration overlay could not resolve game window");
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	DX::ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(srvHeap.put())));

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	DX::ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.put())));

		auto* overlay = Overlay::GetSingleton();
		sharedContext = overlay && overlay->IsInitialized();

		if (sharedContext) {
			ImGui::SetCurrentContext(overlay->GetImGuiContext());
			win32Initialized = true;
			dx12Initialized = true;
			imguiContextCreated = false;
			logger::debug("[HDR] Calibration overlay using shared ImGui context from Overlay");
		} else {
			ImGui::CreateContext();
			imguiContextCreated = true;
			ImGui::StyleColorsDark();
			ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

			win32Initialized = ImGui_ImplWin32_Init(hwnd);
			if (!win32Initialized) {
				logger::warn("[HDR] ImGui Win32 backend initialization failed");
				return false;
			}

			ImGui_ImplDX12_InitInfo initInfo{};
			initInfo.Device = device;
			initInfo.CommandQueue = commandQueue;
			initInfo.NumFramesInFlight = 2;
			initInfo.RTVFormat = swapChainFormat;
			initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
			initInfo.SrvDescriptorHeap = srvHeap.get();
			initInfo.SrvDescriptorAllocFn = AllocateImGuiDescriptor;
			initInfo.SrvDescriptorFreeFn = FreeImGuiDescriptor;
			dx12Initialized = ImGui_ImplDX12_Init(&initInfo);
			if (!dx12Initialized) {
				logger::warn("[HDR] ImGui D3D12 backend initialization failed");
				return false;
			}

			g_calibrationOverlay = this;
			previousWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
		}

		EnsurePatternResources(device, swapChainFormat);
	initializedFormat = swapChainFormat;
	initialized = true;
	logger::info("[HDR] Calibration overlay initialized");
	return true;
}

void HDRCalibrationOverlay::Shutdown()
{
	if (!sharedContext) {
		if (hwnd && previousWndProc) {
			SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previousWndProc));
		}
		previousWndProc = nullptr;
		if (g_calibrationOverlay == this) {
			g_calibrationOverlay = nullptr;
		}

		if (dx12Initialized) {
			ImGui_ImplDX12_Shutdown();
		}
		if (win32Initialized) {
			ImGui_ImplWin32_Shutdown();
		}
		if (imguiContextCreated) {
			ImGui::DestroyContext();
		}
	}

	initialized = false;
	active = false;
	applyMetadataOnClose = false;
	imguiContextCreated = false;
	win32Initialized = false;
	dx12Initialized = false;
	hwnd = nullptr;
	initializedFormat = DXGI_FORMAT_UNKNOWN;
	mappedConstants = nullptr;
	constantBuffer = nullptr;
	pipelineState = nullptr;
	rootSignature = nullptr;
	rtvHeap = nullptr;
	srvHeap = nullptr;
}

bool HDRCalibrationOverlay::ReloadActivation(HDRSettings& settings)
{
	auto latestSettings = ReloadCalibrationSettings();
	if (!latestSettings.calibrationActive) {
		if (active) {
			active = false;
			settings.calibrationActive = false;
			logger::info("[HDR] Calibration overlay closed by INI state");
		}
		return false;
	}

	if (!latestSettings.IsEnabled()) {
		latestSettings.hdrMode = 2;
		if (latestSettings.peakLuminance < 80.0f)
			latestSettings.peakLuminance = 1000.0f;
		if (latestSettings.paperWhiteLuminance < 20.0f)
			latestSettings.paperWhiteLuminance = 200.0f;
		SaveHDRSettingsToINI(latestSettings);
		logger::info("[HDR] Auto-enabled HDR10 for calibration (peak={:.0f} paperWhite={:.0f})",
			latestSettings.peakLuminance, latestSettings.paperWhiteLuminance);
	}

	if (!active) {
		editableSettings = latestSettings;
		logger::info("[HDR] Calibration overlay activated");
	}
	settings = latestSettings;
	active = true;
	return true;
}

void HDRCalibrationOverlay::SaveAndClose(HDRSettings& settings)
{
	editableSettings.calibrationActive = false;
	SaveHDRSettingsToINI(editableSettings);
	settings = editableSettings;
	applyMetadataOnClose = true;
	active = false;

	auto upscaling = Upscaling::GetSingleton();
	upscaling->settings.hdrMode = editableSettings.hdrMode;
	upscaling->settings.peakLuminance = editableSettings.peakLuminance;
	upscaling->settings.paperWhiteLuminance = editableSettings.paperWhiteLuminance;
	upscaling->settings.scRGBReferenceLuminance = editableSettings.scRGBReferenceLuminance;
	upscaling->settings.hdrCalibrationActive = false;
}

void HDRCalibrationOverlay::Cancel(HDRSettings& settings)
{
	settings.calibrationActive = false;
	SaveHDRSettingsToINI(settings);
	active = false;
}

void HDRCalibrationOverlay::RenderPattern(ID3D12GraphicsCommandList4* commandList, ID3D12Resource* backBuffer, DXGI_FORMAT swapChainFormat, const HDRSettings& settings)
{
	UpdatePatternConstants(commandList, settings);

	auto barrierToRT = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	commandList->ResourceBarrier(1, &barrierToRT);

	auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = swapChainFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	winrt::com_ptr<ID3D12Device> device;
	DX::ThrowIfFailed(backBuffer->GetDevice(IID_PPV_ARGS(device.put())));
	device->CreateRenderTargetView(backBuffer, &rtvDesc, rtv);
	commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

	D3D12_RESOURCE_DESC backBufferDesc = backBuffer->GetDesc();
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(backBufferDesc.Width), static_cast<float>(backBufferDesc.Height), 0.0f, 1.0f };
	D3D12_RECT scissor{ 0, 0, static_cast<LONG>(backBufferDesc.Width), static_cast<LONG>(backBufferDesc.Height) };
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);
	commandList->SetGraphicsRootSignature(rootSignature.get());
	commandList->SetPipelineState(pipelineState.get());
	commandList->SetGraphicsRootConstantBufferView(0, constantBuffer->GetGPUVirtualAddress());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}

void HDRCalibrationOverlay::RenderUI(HDRSettings& settings)
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowSize(ImVec2(520.0f, 380.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("HDR Calibration Preview", nullptr, ImGuiWindowFlags_NoCollapse);
	ImGui::TextWrapped("Adjust peak luminance and paper white against the HDR test pattern. Save writes Data/MCM/Settings/HDR.ini and applies metadata immediately.");
	ImGui::Separator();
	ImGui::SliderFloat("Peak Luminance", &editableSettings.peakLuminance, 80.0f, 10000.0f, "%.0f nits", ImGuiSliderFlags_Logarithmic);
	ImGui::SliderFloat("Paper White", &editableSettings.paperWhiteLuminance, 20.0f, 1000.0f, "%.0f nits", ImGuiSliderFlags_Logarithmic);
	ImGui::Separator();
	{
		const char* hdrModeNames[] = { "Disabled", "scRGB (HDR)", "HDR10 (HDR)" };
		int currentHDRMode = editableSettings.hdrMode;
		if (ImGui::Combo("HDR Mode", &currentHDRMode, hdrModeNames, IM_ARRAYSIZE(hdrModeNames))) {
			editableSettings.hdrMode = currentHDRMode;
		}
		ImGui::TextWrapped("HDR mode changes take effect after saving and restarting the game.");
	}
	ImGui::Separator();
	ImGui::Checkbox("Show clipping warning", &showClippingWarning);
	ImGui::Checkbox("Show Rec.2020 color bars", &showColorBars);
	ImGui::Separator();
	if (ImGui::Button("Save & Close")) {
		SaveAndClose(settings);
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		Cancel(settings);
	}
	ImGui::End();

	ImGui::Render();
}

void HDRCalibrationOverlay::EnsurePatternResources(ID3D12Device* device, DXGI_FORMAT swapChainFormat)
{
	if (rootSignature && pipelineState && constantBuffer) {
		return;
	}

	CD3DX12_ROOT_PARAMETER rootParameter{};
	rootParameter.InitAsConstantBufferView(0);
	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Init(1, &rootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	winrt::com_ptr<ID3DBlob> signatureBlob;
	winrt::com_ptr<ID3DBlob> errorBlob;
	DX::ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.put(), errorBlob.put()));
	DX::ThrowIfFailed(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(rootSignature.put())));

	const auto vertexShader = CompileShader("VSMain", "vs_5_0");
	const auto pixelShader = CompileShader("PSMain", "ps_5_0");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
	psoDesc.pRootSignature = rootSignature.get();
	psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
	psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = swapChainFormat;
	psoDesc.SampleDesc.Count = 1;
	DX::ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState.put())));

	const auto constantBufferSize = static_cast<UINT64>((sizeof(PatternConstants) + 255) & ~255);
	const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);
	DX::ThrowIfFailed(device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(constantBuffer.put())));
	DX::ThrowIfFailed(constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedConstants)));
}

void HDRCalibrationOverlay::UpdatePatternConstants(ID3D12GraphicsCommandList4*, const HDRSettings& settings)
{
	PatternConstants constants{
		.peakNits = settings.peakLuminance,
		.paperWhiteNits = settings.paperWhiteLuminance,
		.scRGBReferenceNits = settings.scRGBReferenceLuminance,
		.hdrMode = static_cast<std::uint32_t>(settings.hdrMode),
		.showClippingWarning = showClippingWarning ? 1u : 0u,
		.showColorBars = showColorBars ? 1u : 0u
	};
	std::memcpy(mappedConstants, &constants, sizeof(constants));
}

void HDRCalibrationOverlay::ApplyHDRMetadata(IDXGISwapChain4* swapChain, const HDRSettings& settings)
{
	if (!swapChain || !settings.IsEnabled()) {
		return;
	}

	std::ignore = swapChain->SetColorSpace1(GetHDRColorSpace(settings));
	if (settings.GetMode() != HDRMode::kHDR10) {
		return;
	}

	DXGI_HDR_METADATA_HDR10 metadata{};
	metadata.RedPrimary[0] = 34000;
	metadata.RedPrimary[1] = 16000;
	metadata.GreenPrimary[0] = 13250;
	metadata.GreenPrimary[1] = 34500;
	metadata.BluePrimary[0] = 7500;
	metadata.BluePrimary[1] = 3000;
	metadata.WhitePoint[0] = 15635;
	metadata.WhitePoint[1] = 16450;
	metadata.MaxMasteringLuminance = static_cast<UINT>(std::clamp(settings.peakLuminance, 80.0f, 10000.0f) * 10000.0f);
	metadata.MinMasteringLuminance = 0;
	metadata.MaxContentLightLevel = NitsToHDRValue(settings.peakLuminance);
	metadata.MaxFrameAverageLightLevel = NitsToHDRValue(settings.paperWhiteLuminance);
	std::ignore = swapChain->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata);
}

LRESULT CALLBACK HDRCalibrationOverlay::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (g_calibrationOverlay && g_calibrationOverlay->active && ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam)) {
		return true;
	}
	return g_calibrationOverlay && g_calibrationOverlay->previousWndProc ? CallWindowProcW(g_calibrationOverlay->previousWndProc, hwnd, message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}
