#pragma once

#include "Render/Buffer.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "SimpleIni.h"

class Upscaling
{
public:
	static Upscaling* GetSingleton()
	{
		static Upscaling singleton;
		return &singleton;
	}

	struct Settings
	{
		bool frameGenerationMode = 1;
		bool frameLimitMode = 1;
		int frameGenerationBackend = 1;
		int reflexMode = 1;
		bool reflexSleepMode = true;
		int upscaleMethodPreference = 2;
		int qualityMode = 1;
		int dlssPreset = 0;
		bool debugLogging = false;
		int streamlineLogLevel = 0;
		int debugFrameLogCount = 240;
	};

	enum class PluginMode
	{
		kFrameGen,
		kUpscaler,
		kReflex
	};

	enum class UpscaleMethod
	{
		kDisabled = 0,
		kFSR = 1,
		kDLSS = 2
	};

	static constexpr int kFrameGenerationBackendDLSS = 1;
	static constexpr int kFrameGenerationBackendFSR = 2;

	Settings settings;

	PluginMode pluginMode = PluginMode::kFrameGen;
	bool renderBackendEnabled = false;
	bool highFPSPhysicsFixLoaded = false;
	bool debugTraceCurrentPresent = false;

	bool d3d12Interop = false;
	double refreshRate = 0.0f;

	Texture2D* HUDLessBufferShared[2];
	Texture2D* uiColorAndAlphaBufferShared[2]{};
	Texture2D* depthBufferShared[2];
	Texture2D* motionVectorBufferShared[2];
	Texture2D* upscalerInputShared[2]{};
	Texture2D* upscalerOutputShared[2]{};
	
	winrt::com_ptr<ID3D12Resource> HUDLessBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> uiColorAndAlphaBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> depthBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> motionVectorBufferShared12[2];
	winrt::com_ptr<ID3D12Resource> upscalerInputShared12[2];
	winrt::com_ptr<ID3D12Resource> upscalerOutputShared12[2];

	ID3D11ComputeShader* copyDepthToSharedBufferCS;
	ID3D11ComputeShader* generateSharedBuffersCS;
	ID3D11ComputeShader* compositeUIOverBackbufferCS;

	// While the UI render target is redirected, kFrameBuffer's RTV points at
	// uiColorAndAlphaBufferShared instead of the proxy swap chain buffer. This holds the
	// engine's own pointer so it can be put back at Present.
	// void*, not ID3D11RenderTargetView*: PostNG and PostAE type this field as
	// REX::W32::ID3D11RenderTargetView* while PreNG uses the real D3D11 type, so the only
	// declaration that compiles on all three is an untyped one cast at each end.
	void* savedFrameBufferRTV = nullptr;
	bool uiRedirectActive = false;
	uint32_t uiRedirectFrameIndex = 0;

	bool setupBuffers = false;
	bool postLoadingSkipUpscale = false;
	std::array<bool, 2> hudLessFrameValid{};
	std::array<std::uint64_t, 2> hudLessFrameIDs{};

	void LoadSettings();
	void LoadFrameGenerationSettings();
	void LoadReflexSettings();
	void ApplyRuntimeFallbacks();
	[[nodiscard]] const char* GetDLSSUnavailableReason() const;
	[[nodiscard]] static bool IsStreamlineRuntimeAvailable();

	void PostPostLoad();
	void OnD3D11DeviceCreated(ID3D11Device* a_device, IDXGIAdapter* a_adapter);

	void CreateFrameGenerationResources();
	void PreAlpha();
	bool CaptureHUDLessFrame();
	void PostAlpha();
	void CopyBuffersToSharedResources();
	void RedirectUIRenderTarget();
	bool RestoreUIRenderTarget();
	bool CompositeUIOntoPresentedFrame(ID3D11UnorderedAccessView* a_presentedFrameUAV);
	void ReportUILayerCoverageOnce(uint32_t a_frameIndex);

	static void TimerSleepQPC(int64_t targetQPC);

	void FrameLimiter(bool a_useFrameGeneration);

	void GameFrameLimiter();

	static double GetRefreshRate(HWND a_window);

	void PostDisplay();

	void Reset();

	UpscaleMethod GetPreferredUpscaleMethod() const;
	bool UsesDLSSUpscaling() const;
	bool UsesFSRUpscaling() const;
	bool UsesDLSSFrameGeneration() const;
	bool UsesFSRFrameGeneration() const;
	bool UsesReflex() const;

	UpscaleMethod GetUpscaleMethod(bool a_checkMenu) const;
	void UpdateUpscaling();
	bool Upscale();
	void CheckResources();

	void UpdateRenderTargets(float a_currentWidthRatio, float a_currentHeightRatio);
	void UpdateRenderTarget(int a_index, float a_currentWidthRatio, float a_currentHeightRatio);

	void UpdateDepth(float a_currentWidthRatio, float a_currentHeightRatio);
	void CopyDepth();

	void UpdateSamplerStates(float a_currentMipBias);
	void OverrideSamplerStates();
	void ResetSamplerStates();
	void UpdateGameSettings();
	bool CreateUpscalingResources();
	void DestroyUpscalingResources();

	ID3D11VertexShader* GetCopyDepthVS();
	ID3D11PixelShader* GetCopyDepthPS();
	ID3D11DepthStencilState* GetCopyDepthStencilState();
	ID3D11BlendState* GetCopyBlendState();
	ID3D11RasterizerState* GetCopyRasterizerState();
	ID3D11SamplerState* GetCopySamplerState();

	struct UpscalingCB
	{
		uint ScreenSize[2];
		uint RenderSize[2];
		float4 CameraData;
	};

	ConstantBuffer* GetUpscalingCB();
	void UpdateAndBindUpscalingCB(ID3D11DeviceContext* a_context, float2 a_screenSize, float2 a_renderSize);

	float2 jitter = { 0.0f, 0.0f };
	UpscaleMethod upscaleMethodNoMenu = UpscaleMethod::kDisabled;
	UpscaleMethod upscaleMethod = UpscaleMethod::kDisabled;

	RE::BSGraphics::RenderTarget originalRenderTargets[101]{};
	RE::BSGraphics::RenderTarget proxyRenderTargets[101]{};
	RE::BSGraphics::DepthStencilTarget originalDepthStencilTarget{};
	RE::BSGraphics::DepthStencilTarget depthOverrideTarget{};

	std::array<ID3D11SamplerState*, 320> originalSamplerStates{};
	std::array<ID3D11SamplerState*, 320> biasedSamplerStates{};

	std::unique_ptr<Texture2D> upscalingTexture;
	std::unique_ptr<Texture2D> dilatedMotionVectorTexture;

	winrt::com_ptr<ID3D11VertexShader> copyDepthVS;
	winrt::com_ptr<ID3D11PixelShader> copyDepthPS;
	winrt::com_ptr<ID3D11DepthStencilState> copyDepthStencilState;
	winrt::com_ptr<ID3D11BlendState> copyBlendState;
	winrt::com_ptr<ID3D11RasterizerState> copyRasterizerState;
	winrt::com_ptr<ID3D11SamplerState> copySamplerState;

	static void InstallHooks();
};
