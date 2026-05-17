#include "Core/Menu.h"

#include "Core/Globals.h"

#if defined(FALLOUT_PRE_NG)
#include "Overlay/Overlay.h"
#include "Overlay/OverlayWndProc.h"
#include "Upscaler.h"

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>
#include <SimpleIni.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <cstdint>
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#endif

namespace CommunityShaders::Menu
{
	namespace
	{
#if defined(FALLOUT_PRE_NG)
		constexpr const char* kSettingsSection = "Settings";
		const std::filesystem::path kOverlaySelfSettingsPath{ "Data\\F4SE\\Plugins\\Overlay\\Overlay.ini" };

		bool initialized = false;
		bool menuOpen = false;
		bool loggedFirstRender = false;
		bool hotkeyWasDown = false;
		uint64_t initTimestamp = 0;
		uint64_t lastToggleTimestamp = 0;
		HWND hwnd = nullptr;
		WNDPROC previousWndProc = nullptr;
		ImGuiContext* imguiContext = nullptr;
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		float d3d11DpiScale = 1.0f;
		int lastHDRModeApplied = -1;
		bool loggedHDRCalibrationActive = false;
		ID3D11VertexShader* calibrationVS = nullptr;
		ID3D11PixelShader* calibrationPS = nullptr;
		ID3D11Buffer* calibrationCB = nullptr;
		ID3D11VertexShader* colorSpaceVS = nullptr;
		ID3D11PixelShader* colorSpacePS = nullptr;
		ID3D11Buffer* colorSpaceCB = nullptr;
		ID3D11SamplerState* colorSpaceSampler = nullptr;
		ID3D11Texture2D* colorSpaceSource = nullptr;
		ID3D11ShaderResourceView* colorSpaceSRV = nullptr;
		UINT colorSpaceWidth = 0;
		UINT colorSpaceHeight = 0;
		DXGI_FORMAT colorSpaceFormat = DXGI_FORMAT_UNKNOWN;
		bool loggedColorSpacePassActive = false;
		std::mutex renderMutex;

		void UpdateD3D11MouseCaptureState(bool a_overlayVisible)
		{
			if (!a_overlayVisible) {
				return;
			}

			ReleaseCapture();
			ClipCursor(nullptr);
		}

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

float3 LinearToPQ(float3 linearNits)
{
    float3 y = saturate(linearNits / 10000.0);
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
        return LinearToPQ(nits);
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

float3 LinearToPQ(float3 linearNits)
{
    float3 y = saturate(linearNits / 10000.0);
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
    float3 pq = LinearToPQ(rec2020 * paperWhiteNits);
    return float4(pq, source.a);
}
)";

		struct CalibrationConstants
		{
			float peakNits;
			float paperWhiteNits;
			float scRGBReferenceNits;
			std::uint32_t hdrMode;
			std::uint32_t showClippingWarning;
			std::uint32_t showColorBars;
			float padding[2]{};
		};

		struct ColorSpaceConstants
		{
			std::uint32_t dimensions[2];
			float peakNits;
			float paperWhiteNits;
			float scRGBReferenceNits;
			std::uint32_t hdrMode;
			float padding[2]{};
		};

		float GetD3D11UIScale()
		{
			return d3d11DpiScale * Overlay::GetSingleton()->GetUIScaleOverride();
		}

		void LoadOverlaySelfSettings()
		{
			std::error_code ec;
			if (!std::filesystem::exists(kOverlaySelfSettingsPath, ec)) {
				return;
			}

			CSimpleIniA ini;
			ini.SetUnicode();
			if (ini.LoadFile(kOverlaySelfSettingsPath.string().c_str()) < 0) {
				return;
			}

			auto* overlay = Overlay::GetSingleton();
			auto hotkey = static_cast<int>(ini.GetLongValue(kSettingsSection, "iOverlayHotkey",
				ini.GetLongValue(kSettingsSection, "iHotkey", overlay->GetHotkey())));
			if (hotkey > 0 && hotkey <= 0xFE) {
				overlay->SetHotkey(hotkey);
			}

			auto scale = static_cast<float>(ini.GetDoubleValue(kSettingsSection, "fUIScale", 1.0));
			if (scale < 0.5f) {
				scale = 0.5f;
			}
			if (scale > 4.0f) {
				scale = 4.0f;
			}
			overlay->SetUIScaleOverride(scale);
			overlay->SetIntroEnabled(ini.GetBoolValue(kSettingsSection, "showIntro",
				ini.GetBoolValue(kSettingsSection, "bShowIntro", overlay->IsIntroEnabled())));
		}

		bool InitializeD3D11(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
		{
			if (initialized) {
				return true;
			}
			if (!a_device || !a_context) {
				return false;
			}

			hwnd = GetForegroundWindow();
			if (!hwnd) {
				logger::warn("[CommunityShaders::Menu] Could not locate foreground window for D3D11 overlay");
				return false;
			}

			IMGUI_CHECKVERSION();
			imguiContext = ImGui::CreateContext();
			ImGui::SetCurrentContext(imguiContext);
			ImGui::StyleColorsDark();

			auto& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
			io.IniFilename = nullptr;

			if (auto hdc = GetDC(hwnd)) {
				d3d11DpiScale = static_cast<float>(GetDeviceCaps(hdc, LOGPIXELSX)) / 96.0f;
				ReleaseDC(hwnd, hdc);
				if (d3d11DpiScale < 1.0f) {
					d3d11DpiScale = 1.0f;
				}
				ImGui::GetStyle().ScaleAllSizes(d3d11DpiScale);
				io.FontGlobalScale = d3d11DpiScale;
				logger::info("[CommunityShaders::Menu] D3D11 DPI scale: {:.2f}", d3d11DpiScale);
			}

			if (!ImGui_ImplWin32_Init(hwnd)) {
				logger::error("[CommunityShaders::Menu] ImGui_ImplWin32_Init failed");
				return false;
			}
			if (!ImGui_ImplDX11_Init(a_device, a_context)) {
				logger::error("[CommunityShaders::Menu] ImGui_ImplDX11_Init failed");
				ImGui_ImplWin32_Shutdown();
				return false;
			}

			device = a_device;
			context = a_context;
			LoadOverlaySelfSettings();

			auto* overlay = Overlay::GetSingleton();
			overlay->hwnd = hwnd;
			previousWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
			overlay->previousWndProc = previousWndProc;
			overlay->SetVisible(false);
			g_overlay = overlay;

			menuOpen = false;
			initTimestamp = GetTickCount64();
			initialized = true;
			logger::info("[CommunityShaders::Menu] D3D11 overlay initialized (hwnd=0x{:X}, hotkey={})",
				reinterpret_cast<uintptr_t>(hwnd),
				overlay->GetHotkey());
			return true;
		}

		bool IsShowingIntroMessage()
		{
			const auto* overlay = Overlay::GetSingleton();
			return overlay->IsIntroEnabled() && (GetTickCount64() - initTimestamp) < 30000;
		}

		void PollHotkeyState()
		{
			auto* overlay = Overlay::GetSingleton();
			const bool hotkeyIsDown = (GetAsyncKeyState(overlay->GetHotkey()) & 0x8000) != 0;
			if (!hotkeyIsDown) {
				hotkeyWasDown = false;
				return;
			}
			if (!hotkeyWasDown && !overlay->IsCapturingHotkey()) {
				const auto now = GetTickCount64();
				if (now - lastToggleTimestamp >= 200) {
					lastToggleTimestamp = now;
					menuOpen = !menuOpen;
					overlay->SetVisible(menuOpen);
					logger::debug("[CommunityShaders::Menu] Toggled visible={}", menuOpen);
				}
			}
			hotkeyWasDown = true;
		}

		void DrawRegisteredPanels()
		{
			auto* overlay = Overlay::GetSingleton();
			if (menuOpen) {
				const float uiScale = GetD3D11UIScale();
				ImGui::SetNextWindowSize(ImVec2(520.0f * uiScale, 480.0f * uiScale), ImGuiCond_FirstUseEver);
				if (ImGui::Begin("Community Shaders", &menuOpen)) {
					overlay->DrawOverlaySettings();
					for (auto* feature : Feature::GetFeatureList()) {
						if (feature->loaded && feature->IsInMenu()) {
							ImGui::PushID(feature);
							feature->DrawSettings();
							ImGui::PopID();
						}
					}
					ImGui::Separator();
					if (ImGui::Button("Save All Settings")) {
						overlay->SaveOverlaySelfSettings();
						for (auto* feature : Feature::GetFeatureList()) {
							if (feature->loaded) {
								feature->SaveSettings();
							}
						}
					}
				}
				ImGui::End();
			}

			if (!menuOpen && IsShowingIntroMessage()) {
				const float padding = 20.0f * GetD3D11UIScale();
				ImGui::SetNextWindowBgAlpha(0.65f);
				ImGui::SetNextWindowPos(ImVec2(padding, padding), ImGuiCond_Always);
				constexpr int flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
					ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
					ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
					ImGuiWindowFlags_AlwaysAutoResize;
				if (ImGui::Begin("##CommunityShadersIntro", nullptr, flags)) {
					ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "CommunityShaders loaded.");
					ImGui::SameLine();
					ImGui::Text("Press End to open in-game menu.");
				}
				ImGui::End();
			}
		}

		ID3DBlob* CompileCalibrationShader(const char* a_entryPoint, const char* a_target)
		{
			ID3DBlob* shaderBlob = nullptr;
			ID3DBlob* errorBlob = nullptr;
			const auto result = D3DCompile(
				kCalibrationShader,
				std::strlen(kCalibrationShader),
				"HDRCalibrationD3D11",
				nullptr,
				nullptr,
				a_entryPoint,
				a_target,
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0,
				&shaderBlob,
				&errorBlob);
			if (FAILED(result)) {
				logger::warn("[HDR] D3D11 calibration shader compile failed: {}",
					errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "unknown");
				if (errorBlob) {
					errorBlob->Release();
				}
				return nullptr;
			}
			if (errorBlob) {
				errorBlob->Release();
			}
			return shaderBlob;
		}

		ID3DBlob* CompileColorSpaceShader(const char* a_entryPoint, const char* a_target)
		{
			ID3DBlob* shaderBlob = nullptr;
			ID3DBlob* errorBlob = nullptr;
			const auto result = D3DCompile(
				kColorSpaceShader,
				std::strlen(kColorSpaceShader),
				"HDRColorSpaceD3D11",
				nullptr,
				nullptr,
				a_entryPoint,
				a_target,
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0,
				&shaderBlob,
				&errorBlob);
			if (FAILED(result)) {
				logger::warn("[HDR] D3D11 color-space shader compile failed: {}",
					errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "unknown");
				if (errorBlob) {
					errorBlob->Release();
				}
				return nullptr;
			}
			if (errorBlob) {
				errorBlob->Release();
			}
			return shaderBlob;
		}

		bool EnsureCalibrationResources()
		{
			if (calibrationVS && calibrationPS && calibrationCB) {
				return true;
			}
			if (!device) {
				return false;
			}

			ID3DBlob* vsBlob = CompileCalibrationShader("VSMain", "vs_5_0");
			ID3DBlob* psBlob = CompileCalibrationShader("PSMain", "ps_5_0");
			if (!vsBlob || !psBlob) {
				if (vsBlob) {
					vsBlob->Release();
				}
				if (psBlob) {
					psBlob->Release();
				}
				return false;
			}

			auto result = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &calibrationVS);
			vsBlob->Release();
			if (FAILED(result)) {
				psBlob->Release();
				logger::warn("[HDR] Failed to create D3D11 calibration vertex shader (hr=0x{:08X})", static_cast<uint32_t>(result));
				return false;
			}

			result = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &calibrationPS);
			psBlob->Release();
			if (FAILED(result)) {
				logger::warn("[HDR] Failed to create D3D11 calibration pixel shader (hr=0x{:08X})", static_cast<uint32_t>(result));
				return false;
			}

			D3D11_BUFFER_DESC bufferDesc{};
			bufferDesc.ByteWidth = sizeof(CalibrationConstants);
			bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
			bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			result = device->CreateBuffer(&bufferDesc, nullptr, &calibrationCB);
			if (FAILED(result)) {
				logger::warn("[HDR] Failed to create D3D11 calibration constant buffer (hr=0x{:08X})", static_cast<uint32_t>(result));
				return false;
			}

			return true;
		}

		void ReleaseColorSpaceBackBufferResources()
		{
			if (colorSpaceSRV) {
				colorSpaceSRV->Release();
				colorSpaceSRV = nullptr;
			}
			if (colorSpaceSource) {
				colorSpaceSource->Release();
				colorSpaceSource = nullptr;
			}
			colorSpaceWidth = 0;
			colorSpaceHeight = 0;
			colorSpaceFormat = DXGI_FORMAT_UNKNOWN;
		}

		bool EnsureColorSpaceResources(const D3D11_TEXTURE2D_DESC& a_backBufferDesc)
		{
			if (!device) {
				return false;
			}

			if (!colorSpaceVS || !colorSpacePS || !colorSpaceCB || !colorSpaceSampler) {
				ID3DBlob* vsBlob = CompileColorSpaceShader("VSMain", "vs_5_0");
				ID3DBlob* psBlob = CompileColorSpaceShader("PSMain", "ps_5_0");
				if (!vsBlob || !psBlob) {
					if (vsBlob) {
						vsBlob->Release();
					}
					if (psBlob) {
						psBlob->Release();
					}
					return false;
				}

				auto result = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &colorSpaceVS);
				vsBlob->Release();
				if (FAILED(result)) {
					psBlob->Release();
					logger::warn("[HDR] Failed to create D3D11 color-space vertex shader (hr=0x{:08X})", static_cast<uint32_t>(result));
					return false;
				}

				result = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &colorSpacePS);
				psBlob->Release();
				if (FAILED(result)) {
					logger::warn("[HDR] Failed to create D3D11 color-space pixel shader (hr=0x{:08X})", static_cast<uint32_t>(result));
					return false;
				}

				D3D11_BUFFER_DESC bufferDesc{};
				bufferDesc.ByteWidth = sizeof(ColorSpaceConstants);
				bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
				bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				result = device->CreateBuffer(&bufferDesc, nullptr, &colorSpaceCB);
				if (FAILED(result)) {
					colorSpaceVS->Release();
					colorSpaceVS = nullptr;
					colorSpacePS->Release();
					colorSpacePS = nullptr;
					logger::warn("[HDR] Failed to create D3D11 color-space constant buffer (hr=0x{:08X}, byteWidth={})", static_cast<uint32_t>(result), bufferDesc.ByteWidth);
					return false;
				}

				D3D11_SAMPLER_DESC samplerDesc{};
				samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
				samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
				samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
				samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
				samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
				result = device->CreateSamplerState(&samplerDesc, &colorSpaceSampler);
				if (FAILED(result)) {
					colorSpaceVS->Release();
					colorSpaceVS = nullptr;
					colorSpacePS->Release();
					colorSpacePS = nullptr;
					colorSpaceCB->Release();
					colorSpaceCB = nullptr;
					logger::warn("[HDR] Failed to create D3D11 color-space sampler (hr=0x{:08X})", static_cast<uint32_t>(result));
					return false;
				}
			}

			if (colorSpaceSource &&
				colorSpaceWidth == a_backBufferDesc.Width &&
				colorSpaceHeight == a_backBufferDesc.Height &&
				colorSpaceFormat == a_backBufferDesc.Format) {
				return true;
			}

			ReleaseColorSpaceBackBufferResources();

			D3D11_TEXTURE2D_DESC sourceDesc = a_backBufferDesc;
			sourceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			sourceDesc.CPUAccessFlags = 0;
			sourceDesc.MiscFlags = 0;
			sourceDesc.Usage = D3D11_USAGE_DEFAULT;
			auto result = device->CreateTexture2D(&sourceDesc, nullptr, &colorSpaceSource);
			if (FAILED(result)) {
				logger::warn("[HDR] Failed to create D3D11 color-space source texture (hr=0x{:08X})", static_cast<uint32_t>(result));
				return false;
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = sourceDesc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			result = device->CreateShaderResourceView(colorSpaceSource, &srvDesc, &colorSpaceSRV);
			if (FAILED(result)) {
				logger::warn("[HDR] Failed to create D3D11 color-space SRV (hr=0x{:08X})", static_cast<uint32_t>(result));
				ReleaseColorSpaceBackBufferResources();
				return false;
			}

			colorSpaceWidth = sourceDesc.Width;
			colorSpaceHeight = sourceDesc.Height;
			colorSpaceFormat = sourceDesc.Format;
			return true;
		}

		void DrawHDRCalibrationPattern(IDXGISwapChain* a_swapChain)
		{
			auto* upscaling = Upscaling::GetSingleton();
			if (!upscaling->settings.hdrCalibrationActive) {
				loggedHDRCalibrationActive = false;
				return;
			}
			if (!a_swapChain || !context || !EnsureCalibrationResources()) {
				return;
			}

			if (!loggedHDRCalibrationActive) {
				loggedHDRCalibrationActive = true;
				logger::info("[HDR] D3D11 calibration shader pattern active (history mapping: PQ nits/10000)");
			}

			ID3D11Texture2D* backBuffer = nullptr;
			auto result = a_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
			if (FAILED(result) || !backBuffer) {
				return;
			}

			ID3D11RenderTargetView* rtv = nullptr;
			result = device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
			D3D11_TEXTURE2D_DESC backBufferDesc{};
			backBuffer->GetDesc(&backBufferDesc);
			backBuffer->Release();
			if (FAILED(result) || !rtv) {
				return;
			}

			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (SUCCEEDED(context->Map(calibrationCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				CalibrationConstants constants{
					.peakNits = std::clamp(upscaling->settings.peakLuminance, 80.0f, 10000.0f),
					.paperWhiteNits = std::clamp(upscaling->settings.paperWhiteLuminance, 20.0f, 2000.0f),
					.scRGBReferenceNits = std::clamp(upscaling->settings.scRGBReferenceLuminance, 20.0f, 1000.0f),
					.hdrMode = static_cast<std::uint32_t>(upscaling->settings.hdrMode),
					.showClippingWarning = 1u,
					.showColorBars = 1u
				};
				std::memcpy(mapped.pData, &constants, sizeof(constants));
				context->Unmap(calibrationCB, 0);
			}

			ID3D11RenderTargetView* oldRTV = nullptr;
			ID3D11DepthStencilView* oldDSV = nullptr;
			context->OMGetRenderTargets(1, &oldRTV, &oldDSV);
			UINT oldViewportCount = 1;
			D3D11_VIEWPORT oldViewport{};
			context->RSGetViewports(&oldViewportCount, &oldViewport);

			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(backBufferDesc.Width);
			viewport.Height = static_cast<float>(backBufferDesc.Height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;

			context->OMSetRenderTargets(1, &rtv, nullptr);
			context->RSSetViewports(1, &viewport);
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(calibrationVS, nullptr, 0);
			context->PSSetShader(calibrationPS, nullptr, 0);
			context->PSSetConstantBuffers(0, 1, &calibrationCB);
			context->Draw(3, 0);

			context->OMSetRenderTargets(1, &oldRTV, oldDSV);
			if (oldViewportCount > 0) {
				context->RSSetViewports(oldViewportCount, &oldViewport);
			}
			if (oldRTV) {
				oldRTV->Release();
			}
			if (oldDSV) {
				oldDSV->Release();
			}
			rtv->Release();
		}

		void ApplyD3D11HDRColorMapping(IDXGISwapChain* a_swapChain)
		{
			auto* upscaling = Upscaling::GetSingleton();
			if (!a_swapChain || !context || upscaling->settings.hdrMode <= 0 ||
				upscaling->settings.hdrCalibrationActive) {
				return;
			}

			ID3D11Texture2D* backBuffer = nullptr;
			auto result = a_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
			if (FAILED(result) || !backBuffer) {
				return;
			}

			D3D11_TEXTURE2D_DESC backBufferDesc{};
			backBuffer->GetDesc(&backBufferDesc);
			if (!EnsureColorSpaceResources(backBufferDesc)) {
				backBuffer->Release();
				return;
			}

			if (!loggedColorSpacePassActive) {
				loggedColorSpacePassActive = true;
				logger::info("[HDR] D3D11 color mapping active (main mapping: sRGB -> linear -> Rec.2020/PQ)");
			}

			context->CopyResource(colorSpaceSource, backBuffer);

			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (SUCCEEDED(context->Map(colorSpaceCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				ColorSpaceConstants constants{};
				constants.dimensions[0] = backBufferDesc.Width;
				constants.dimensions[1] = backBufferDesc.Height;
				constants.peakNits = std::clamp(upscaling->settings.peakLuminance, 80.0f, 10000.0f);
				constants.paperWhiteNits = std::clamp(upscaling->settings.paperWhiteLuminance, 20.0f, 2000.0f);
				constants.scRGBReferenceNits = std::clamp(upscaling->settings.scRGBReferenceLuminance, 20.0f, 1000.0f);
				constants.hdrMode = static_cast<std::uint32_t>(upscaling->settings.hdrMode);
				std::memcpy(mapped.pData, &constants, sizeof(constants));
				context->Unmap(colorSpaceCB, 0);
			}

			ID3D11RenderTargetView* rtv = nullptr;
			result = device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
			backBuffer->Release();
			if (FAILED(result) || !rtv) {
				return;
			}

			ID3D11RenderTargetView* oldRTV = nullptr;
			ID3D11DepthStencilView* oldDSV = nullptr;
			ID3D11ShaderResourceView* oldSRV = nullptr;
			ID3D11SamplerState* oldSampler = nullptr;
			ID3D11VertexShader* oldVS = nullptr;
			ID3D11PixelShader* oldPS = nullptr;
			ID3D11Buffer* oldPSCB = nullptr;
			D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
			context->OMGetRenderTargets(1, &oldRTV, &oldDSV);
			context->PSGetShaderResources(0, 1, &oldSRV);
			context->PSGetSamplers(0, 1, &oldSampler);
			context->VSGetShader(&oldVS, nullptr, nullptr);
			context->PSGetShader(&oldPS, nullptr, nullptr);
			context->PSGetConstantBuffers(0, 1, &oldPSCB);
			context->IAGetPrimitiveTopology(&oldTopology);
			UINT oldViewportCount = 1;
			D3D11_VIEWPORT oldViewport{};
			context->RSGetViewports(&oldViewportCount, &oldViewport);

			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(backBufferDesc.Width);
			viewport.Height = static_cast<float>(backBufferDesc.Height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;

			context->OMSetRenderTargets(1, &rtv, nullptr);
			context->RSSetViewports(1, &viewport);
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(colorSpaceVS, nullptr, 0);
			context->PSSetShader(colorSpacePS, nullptr, 0);
			context->PSSetConstantBuffers(0, 1, &colorSpaceCB);
			context->PSSetShaderResources(0, 1, &colorSpaceSRV);
			context->PSSetSamplers(0, 1, &colorSpaceSampler);
			context->Draw(3, 0);

			ID3D11ShaderResourceView* nullSRV = nullptr;
			context->PSSetShaderResources(0, 1, &nullSRV);
			context->OMSetRenderTargets(1, &oldRTV, oldDSV);
			if (oldViewportCount > 0) {
				context->RSSetViewports(oldViewportCount, &oldViewport);
			}
			context->IASetPrimitiveTopology(oldTopology);
			context->VSSetShader(oldVS, nullptr, 0);
			context->PSSetShader(oldPS, nullptr, 0);
			context->PSSetConstantBuffers(0, 1, &oldPSCB);
			context->PSSetShaderResources(0, 1, &oldSRV);
			context->PSSetSamplers(0, 1, &oldSampler);

			if (oldRTV) {
				oldRTV->Release();
			}
			if (oldDSV) {
				oldDSV->Release();
			}
			if (oldSRV) {
				oldSRV->Release();
			}
			if (oldSampler) {
				oldSampler->Release();
			}
			if (oldVS) {
				oldVS->Release();
			}
			if (oldPS) {
				oldPS->Release();
			}
			if (oldPSCB) {
				oldPSCB->Release();
			}
			rtv->Release();
		}

		void ApplyD3D11HDRState(IDXGISwapChain* a_swapChain)
		{
			if (!a_swapChain) {
				return;
			}

			auto* upscaling = Upscaling::GetSingleton();
			const int hdrMode = upscaling->settings.hdrMode;
			if (hdrMode == lastHDRModeApplied) {
				return;
			}

			IDXGISwapChain3* swapChain3 = nullptr;
			if (FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) || !swapChain3) {
				lastHDRModeApplied = hdrMode;
				logger::warn("[HDR] D3D11 swap chain does not support IDXGISwapChain3 color-space control");
				return;
			}

			const DXGI_COLOR_SPACE_TYPE colorSpace = hdrMode == 2 ?
				DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 :
				(hdrMode == 1 ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
			UINT support = 0;
			const auto supportResult = swapChain3->CheckColorSpaceSupport(colorSpace, &support);
			if (FAILED(supportResult) || !(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
				swapChain3->Release();
				lastHDRModeApplied = hdrMode;
				logger::warn("[HDR] D3D11 swap chain color space {} unsupported (hr=0x{:08X}, support=0x{:X})",
					static_cast<uint32_t>(colorSpace),
					static_cast<uint32_t>(supportResult),
					support);
				return;
			}

			const auto result = swapChain3->SetColorSpace1(colorSpace);
			if (SUCCEEDED(result)) {
				logger::info("[HDR] D3D11 swap chain color space set to {} for mode {}", static_cast<uint32_t>(colorSpace), hdrMode);
			} else {
				logger::warn("[HDR] Failed to set D3D11 swap chain color space {} (hr=0x{:08X})",
					static_cast<uint32_t>(colorSpace),
					static_cast<uint32_t>(result));
			}
			swapChain3->Release();
			lastHDRModeApplied = hdrMode;
		}
#endif
	}

	void Setup() {}
	void Draw()
	{
#if defined(FALLOUT_PRE_NG)
		auto* overlay = Overlay::GetSingleton();
		PollHotkeyState();
		menuOpen = overlay->IsVisible();
#endif
	}
	void Render(ID3D11Device* a_device, ID3D11DeviceContext* a_context, IDXGISwapChain* a_swapChain)
	{
		(void)a_swapChain;
#if defined(FALLOUT_PRE_NG)
		std::lock_guard lock(renderMutex);
		if (!InitializeD3D11(a_device, a_context)) {
			return;
		}

		ApplyD3D11HDRState(a_swapChain);
		const bool calibrationActive = Upscaling::GetSingleton()->settings.hdrCalibrationActive;
		ApplyD3D11HDRColorMapping(a_swapChain);
		const bool showIntro = IsShowingIntroMessage();
		if (!menuOpen && !showIntro && !calibrationActive) {
			return;
		}

		if (!loggedFirstRender) {
			loggedFirstRender = true;
			logger::info("[CommunityShaders::Menu] First D3D11 render (visible={}, intro={})", menuOpen, showIntro);
		}

		ImGui::SetCurrentContext(imguiContext);
		UpdateD3D11MouseCaptureState(calibrationActive);
		ImGui::GetIO().MouseDrawCursor = menuOpen;
		ImGui::GetIO().FontGlobalScale = GetD3D11UIScale();
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		DrawHDRCalibrationPattern(a_swapChain);
		DrawRegisteredPanels();

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#else
		(void)a_device;
		(void)a_context;
#endif
	}
	void Reset() {}
	bool IsOpen() noexcept
	{
#if defined(FALLOUT_PRE_NG)
		return menuOpen;
#else
		return false;
#endif
	}
	void SetOpen(bool a_open) noexcept
	{
#if defined(FALLOUT_PRE_NG)
		menuOpen = a_open;
		Overlay::GetSingleton()->SetVisible(a_open);
#else
		(void)a_open;
#endif
	}
	void SetHwnd(HWND a_hwnd) noexcept
	{
#if defined(FALLOUT_PRE_NG)
		hwnd = a_hwnd;
#else
		(void)a_hwnd;
#endif
	}
}
