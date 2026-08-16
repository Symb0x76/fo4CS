#pragma once

#include <d3d11.h>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>
#include <winrt/base.h>

#include "Core/Feature.h"

// Light Limit Fix removes the vanilla 6-light forward point-light cap through clustered
// forward lighting: the clustered SRVs t35-t37 carry every forward point light.
//
// FO4 Adaptation Notes (vs Skyrim CS):
//   - BSShader::SetupGeometry is at vfunc index 7 (FO4 added SetupMaterialSecondary)
//   - FO4 uses BSShaderManager::ShaderEnum (kLighting=8, kEffect=0, kWater=0xA)
//   - PreNG SetupGeometry/point-light callsite addresses and layout offsets live
//     behind RE::FO4Runtime typed runtime accessors in CommonLibF4.
//   - Skyrim LLF binds clustered SRVs from Prepass and uses the internal
//     point-light setup call path for strict-light CB data. FO4 PreNG keeps
//     SetupGeometry evidence behind an explicit runtime gate until the integrated
//     shader consumer is ready.
//   - DFLight no-op/additive passes are proof tools only. The implementation
//     direction is a vanilla-equivalent shader consumer in the normal lighting
//     path, not an extra additive DFLight refresh pass.

struct LightLimitFix : Feature
{
	static constexpr std::string_view kModID = "99548";

	[[nodiscard]] std::string GetName() override { return "Light Limit Fix"; }
	[[nodiscard]] std::string GetShortName() override { return "LightLimitFix"; }
	[[nodiscard]] std::string_view GetShaderDefineName() override { return "LIGHT_LIMIT_FIX"; }
	[[nodiscard]] std::string_view GetCategory() const override { return FeatureCategories::kLighting; }
	[[nodiscard]] bool IsCore() const override { return true; }

	[[nodiscard]] std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Removes the vanilla 6-light forward cap using clustered-forward rendering.",
			{
				"GPU cluster building + culling via compute shaders",
				"Clustered SRV light lists (no b3 strict-light buffer)",
				"Unlimited dynamic lights per pixel"
			}
		};
	}

	// --- GPU Data Types (matches Skyrim CS layout for shader compatibility) ---

	enum class LightFlags : std::uint32_t
	{
		PortalStrict  = (1 << 0),
		Shadow        = (1 << 1),
		Simple        = (1 << 2),
		Initialised   = (1 << 8),
		Disabled      = (1 << 9),
		InverseSquare = (1 << 10),
		Linear        = (1 << 11),
	};

	struct PositionOpt
	{
		float3 data;
		std::uint32_t pad;
	};

	#pragma warning(push)
	#pragma warning(disable: 4324)
	struct alignas(16) LightData
	{
		float3 color;
		float fade = 1.0f;
		float radius;
		float invRadius;
		float fadeZone;
		float sizeBias;
		PositionOpt positionWS[2];
		std::uint32_t roomFlags[4]{};
		std::uint32_t lightFlags = 0;
		std::uint32_t shadowLightIndex = 0;
		std::uint32_t pad0;
		std::uint32_t pad1;
	};
	static_assert(sizeof(LightData) == 96);
	static_assert(offsetof(LightData, positionWS) == 32);
	static_assert(offsetof(LightData, roomFlags) == 64);
	static_assert(offsetof(LightData, lightFlags) == 80);
	static_assert(offsetof(LightData, shadowLightIndex) == 84);
	#pragma warning(pop)

	struct ClusterAABB
	{
		float4 minPoint;
		float4 maxPoint;
	};

	struct alignas(16) LightGrid
	{
		std::uint32_t offset;
		std::uint32_t lightCount;
		std::uint32_t pad0[2];
	};

	struct alignas(16) LightBuildingCB
	{
		float LightsNear;
		float LightsFar;
		std::uint32_t pad0[2];              // → 16
		std::uint32_t ClusterSize[4];       // → 32
		float4x4 CameraProjInverse;          // → 96
	};

	// Matches ClusterCullingCS.hlsl cbuffer PerFrame (register b0, 112 bytes)
	struct alignas(16) LightCullingCB
	{
		std::uint32_t LightCount;
		std::uint32_t pad[3];               // → 16
		std::uint32_t ClusterSize[4];       // → 32
		float4x4 CameraView;                 // → 96
		DirectX::XMFLOAT4 CameraPos;         // → 112
	};

	struct alignas(16) PerFrame
	{
		std::uint32_t EnableLightsVisualisation;
		std::uint32_t LightsVisualisationMode;
		float CameraNear;
		float CameraFar;
		std::uint32_t ClusterSize[4];
	};

	PerFrame GetCommonBufferData();

	// --- Runtime per-frame state ---
	static constexpr std::uint32_t NUMTHREAD_X = 16;
	static constexpr std::uint32_t NUMTHREAD_Y = 16;
	static constexpr std::uint32_t NUMTHREAD_Z = 4;

	float CameraNear = 0.1f;
	float CameraFar = 10000.0f;
	std::uint32_t currentLightCount = 0;
#if defined(FALLOUT_PRE_NG)
	struct ClusterBuildCacheState
	{
		float LightsNear = 0.0f;
		float LightsFar = 0.0f;
		std::uint32_t ClusterSize[4]{};
	};

	bool clusterBuildCacheValid = false;
	ClusterBuildCacheState clusterBuildCache{};

	struct ClusterPayloadCacheState
	{
		float LightsNear = 0.0f;
		float LightsFar = 0.0f;
		std::uint32_t ClusterSize[4]{};
		std::uint32_t LightCount = 0;
		std::uint64_t LightsHash = 0;
		std::uint64_t ViewHash = 0;
	};

	bool clusterPayloadCacheValid = false;
	ClusterPayloadCacheState clusterPayloadCache{};

	struct ShadowSceneFastReuseKey
	{
		std::uintptr_t Node = 0;
		std::uint8_t SelectedIndex = 0;
		std::uint8_t CurrentIndex = 0;
		bool CurrentIndexRead = false;
		bool UsedFallback = false;
		std::uintptr_t ActiveEntries = 0;
		std::uintptr_t ShadowEntries = 0;
		std::uintptr_t ExtraEntries = 0;
		std::uint32_t ActiveCount = 0;
		std::uint32_t ShadowCount = 0;
		std::uint32_t ExtraCount = 0;
		std::uint64_t ActiveHash = 0;
		std::uint64_t ShadowHash = 0;
		std::uint64_t ExtraHash = 0;
	};

	struct ShadowSceneFastReuseState
	{
		ShadowSceneFastReuseKey Key{};
		std::vector<LightData> Lights{};
		std::uint32_t LightCount = 0;
		std::uint64_t LightsHash = 0;
		std::uint32_t StableDecodeCount = 0;
		std::uint32_t ReuseAge = 0;
	};

	bool shadowSceneFastReuseValid = false;
	ShadowSceneFastReuseState shadowSceneFastReuse{};
#endif

	// --- Settings ---
	struct Settings
	{
		bool EnableLightsVisualisation = false;
		std::uint32_t LightsVisualisationMode = 0;
	};

	Settings settings;

	// --- Lifecycle ---
	void SetupResources() override;
	void LoadSettings() override;
	void SaveSettings() override;
	void RestoreDefaultSettings() override;
	void DrawSettings() override;
	[[nodiscard]] bool HasResources() const;
	// The light-payload SRV the current frame uploaded (rotates with the
	// triple-buffered light buffers on PreNG; single SRV elsewhere).
	ID3D11ShaderResourceView* GetCurrentLightsSRV();
	void PostPostLoad() override;
	void DataLoaded() override;
	void Prepass() override;
	void EarlyPrepass() override;
	void Reset() override;
private:
	// Shared clustered-compute submission body invoked by exactly one of
	// Prepass()/EarlyPrepass() per frame (selected by FO4CS_LLF_PRENG_PREPASS_EARLY_HOOK).
	void RunClusterPrepass();
public:

	// --- SetupGeometry hooks ---
	void SetupGeometryBefore(RE::BSRenderPass* a_pass);
	void SetupGeometryAfter(RE::BSRenderPass* a_pass);

	// Per-frame light accumulation
	void CollectLightsFromPass(RE::BSRenderPass* a_pass);
#if defined(FALLOUT_PRE_NG)
	std::uint32_t CollectLightsFromPreNGSceneLights(
		RE::BSRenderPass* a_pass,
		std::uint32_t a_requestedLightCount = 0xFFFFFFFFu,
		std::uint32_t a_shadowArg = 0);
	std::uint32_t CollectLightsFromPreNGShadowScene();
	bool BindPreNGClusterSRVsToPixelShader(
		RE::BSRenderPass* a_pass,
		std::uint32_t a_requestedLightCount,
		bool a_strictCBBound);
	struct PreNGDFLightResourceBindingState
	{
		bool strictCBBound = false;
		bool clusterSRVsBound = false;
		std::uint32_t lightCount = 0;
		std::uint32_t strictLightCount = 0;
		std::uint32_t shadowBitMask = 0;
	};
	PreNGDFLightResourceBindingState BindPreNGDFLightDrawStateStrictLightCB(ID3D11DeviceContext* a_context);
	PreNGDFLightResourceBindingState BindPreNGDFLightDrawStateClusterSRVs(
		ID3D11DeviceContext* a_context,
		bool a_strictCBBound);
	PreNGDFLightResourceBindingState BindPreNGDFLightResourceNoOpPass(ID3D11DeviceContext* a_context);
	PreNGDFLightResourceBindingState BindPreNGDFLightFullContractNoOpPass(ID3D11DeviceContext* a_context);
	PreNGDFLightResourceBindingState BindPreNGDFLightLLFAdditivePass(ID3D11DeviceContext* a_context);
	PreNGDFLightResourceBindingState BindPreNGDFLightDescriptorResourcesToPixelShader();
	PreNGDFLightResourceBindingState BindPreNGDFCompositeDescriptorResourcesToPixelShader();
	PreNGDFLightResourceBindingState BindPreNGBSLightingDescriptorResourcesToPixelShader();
	PreNGDFLightResourceBindingState BindPreNGBSLightingSetupGeometryResources(RE::BSRenderPass* a_pass);
	void TryBindPreNGBSLightingVisibleConsumerFromSetupGeometry(
		RE::BSShader* a_shader,
		const char* a_sourceName = "SetupGeometry");
	void InstallPreNGBSLightingBatchHook();
	void UpdatePreNGDFLightForwardCameraCB(
		const DirectX::XMFLOAT4X4& a_viewMatrix,
		float a_cameraNear,
		float a_cameraFar);
	void HandlePreNGDFLightForwardBatchPostCall(RE::BSShader* a_shader);
	[[nodiscard]] bool ShouldProcessPreNGBSLightingSetupGeometryProof() const;
	[[nodiscard]] bool HasPreNGDFLightDescriptorConsumerData() const;
	[[nodiscard]] bool HasPreNGDFCompositeDescriptorConsumerData() const;
	[[nodiscard]] bool HasPreNGBSLightingDescriptorConsumerData() const;
	[[nodiscard]] bool ShouldSuppressPreNGBSLightingVisibleConsumerForMenu() const;
	void NotifyPreNGBSLightingVisibleConsumerResumeComplete();
	void NotifyPreNGDFLightLLFConsumerDescriptorObserved(
		std::uint32_t a_vertexDescriptor,
		std::uint32_t a_pixelDescriptor,
		bool a_found,
		std::uintptr_t a_pixelShader);
	void NotifyPreNGDFCompositeLLFConsumerDescriptorObserved(
		std::uint32_t a_vertexDescriptor,
		std::uint32_t a_pixelDescriptor,
		bool a_found,
		std::uintptr_t a_vanillaPixelShader,
		std::uintptr_t a_ownedPixelShader);
	void NotifyPreNGBSLightingLLFConsumerDescriptorObserved(
		std::uint32_t a_vertexDescriptor,
		std::uint32_t a_pixelDescriptor,
		bool a_found,
		std::uintptr_t a_vanillaPixelShader);
	[[nodiscard]] bool HasPreNGDFLightLLFConsumerDescriptorObserved() const;
	[[nodiscard]] bool HasPreNGDFCompositeLLFConsumerDescriptorObserved() const;
	[[nodiscard]] bool HasPreNGBSLightingLLFConsumerDescriptorObserved() const;
	bool TracePreNGActiveLightingBindings(
		const char* a_source,
		std::int32_t a_shaderType,
		std::uint32_t a_vertexDescriptor,
		std::uint32_t a_pixelDescriptor,
		bool a_found,
		std::uintptr_t a_lookupPixelShader,
		ID3D11DeviceContext* a_contextOverride = nullptr);
#endif
#if !defined(FALLOUT_PRE_NG)
	// PostNG / PostAE forward clustered consumer (no b3 strict-light buffer).
	// The cluster SRVs (t35/t36/t37) are bound from RunClusterPrepass every frame;
	// BindPostNGBSLightingClusterResourcesToPixelShader re-asserts them inside the
	// per-draw consumer bind so a high-register clobber cannot empty the lights.
	struct PostNGClusterResourceBindingState
	{
		bool clusterSRVsBound = false;
		std::uint32_t lightCount = 0;
	};
	[[nodiscard]] bool HasPostNGBSLightingDescriptorConsumerData() const;
	PostNGClusterResourceBindingState BindPostNGBSLightingClusterResourcesToPixelShader();
	void NotifyPostNGBSLightingLLFConsumerDescriptorObserved(
		std::uint32_t a_vertexDescriptor,
		std::uint32_t a_pixelDescriptor,
		bool a_found,
		std::uintptr_t a_vanillaPixelShader);
	[[nodiscard]] bool HasPostNGBSLightingLLFConsumerDescriptorObserved() const;
#endif
	void CollectLightsFromScene();
	void CollectLightsFromBSLight();
	void CollectLightCB();
	std::vector<LightData> frameLights;
#if defined(FALLOUT_PRE_NG)
	// Camera state used to fill positionWS[1]; the culling block reuses it so
	// the light grid and the light payload always share one camera state.
	DirectX::XMFLOAT4 preNGDFLightLastSnapshotCameraPos{};
	DirectX::XMFLOAT4X4 preNGDFLightLastSnapshotViewRows{};
	bool preNGDFLightLastSnapshotViewValid = false;
#endif
	std::set<RE::BSLight*> seenLights;
	std::vector<RE::BSLight*> seenThisPass;
	std::set<std::uint64_t> seenCBHashes;
	std::uint32_t diagFrameCounter = 0;

	struct Hooks
	{
		struct BSLightingShader_SetupGeometry
		{
			static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSDFLightShader_SetupGeometry
		{
			static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSEffectShader_SetupGeometry
		{
			static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass);
			static inline REL::Relocation<decltype(thunk)> func;
		};

#if defined(FALLOUT_PRE_NG)
		struct PreNGBSLightingBatchSetup
		{
			// sub_1428C37A0: per-item batched BSLighting setup in the normal
			// world renderer (shader lookup + CB1/CB2 bind). a_batchData is an
			// opaque __m128* in the vanilla signature; we only need a_shader.
			static std::uint8_t thunk(
				RE::BSShader* a_shader,
				void* a_batchData,
				std::uint32_t a_flags,
				std::uint8_t a_flag);
			static inline std::uint8_t (*func)(
				RE::BSShader*,
				void*,
				std::uint32_t,
				std::uint8_t) = nullptr;
		};
#endif

		static void Install(bool a_includeEffectShader = true);
	};

private:
#if defined(FALLOUT_PRE_NG)
	PreNGDFLightResourceBindingState BindPreNGDescriptorResourcesToPixelShader(const char* a_sourceName);
	PreNGDFLightResourceBindingState BindPreNGDFLightNoOpPassResources(
		ID3D11DeviceContext* a_context,
		const char* a_passName);
	// GPU-timing helpers for the clustered compute block. BeginPreNGClusterGpuTimer
	// returns the timer slot index to End/Resolve, or UINT32_MAX if timing is off
	// or query creation failed. ResolvePreNGClusterGpuTimer reads the prior frame's
	// disjoint+timestamp pair and logs the elapsed GPU milliseconds.
	std::uint32_t BeginPreNGClusterGpuTimer(ID3D11DeviceContext* a_context, ID3D11Device* a_device);
	void EndPreNGClusterGpuTimer(ID3D11DeviceContext* a_context, std::uint32_t a_slot);
	void ResolvePreNGClusterGpuTimer(ID3D11DeviceContext* a_context, std::uint32_t a_slot, std::uint32_t a_frameNumber, std::uint32_t a_lightCount);
#endif
	// --- GPU Resources (RAII: winrt::com_ptr auto-releases on destruction) ---
	winrt::com_ptr<ID3D11ComputeShader>          clusterBuildingCS;
	winrt::com_ptr<ID3D11ComputeShader>          clusterCullingCS;
	winrt::com_ptr<ID3D11Buffer>                 lightBuildingCB;
	winrt::com_ptr<ID3D11Buffer>                 lightCullingCB;
#if defined(FALLOUT_PRE_NG)
	// Triple-buffered light payload: the consumer draw of frame N still reads
	// the buffer uploaded in frame N, so a single dynamic buffer Map stalls
	// ~12ms waiting for that draw. Rotating 3 buffers/SRVs removes the stall.
	static constexpr std::uint32_t kPreNGLightsBufferFrames = 8;
	winrt::com_ptr<ID3D11Buffer>                 lightsBuffers[kPreNGLightsBufferFrames];
	winrt::com_ptr<ID3D11ShaderResourceView>     lightsSRVs[kPreNGLightsBufferFrames];
	std::uint32_t                               currentLightsBufferIndex = 0;
#else
	winrt::com_ptr<ID3D11Buffer>                 lightsBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView>     lightsSRV;
#endif
	winrt::com_ptr<ID3D11Buffer>                 dflightForwardCB;
	winrt::com_ptr<ID3D11Buffer>                 clustersBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView>     clustersSRV;
	winrt::com_ptr<ID3D11UnorderedAccessView>    clustersUAV;
	winrt::com_ptr<ID3D11Buffer>                 lightIndexCounterBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView>     lightIndexCounterSRV;
	winrt::com_ptr<ID3D11UnorderedAccessView>    lightIndexCounterUAV;
	winrt::com_ptr<ID3D11Buffer>                 lightIndexListBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView>     lightIndexListSRV;
	winrt::com_ptr<ID3D11UnorderedAccessView>    lightIndexListUAV;
	winrt::com_ptr<ID3D11Buffer>                 lightGridBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView>     lightGridSRV;
	winrt::com_ptr<ID3D11UnorderedAccessView>    lightGridUAV;

	std::uint32_t clusterSize[3] = { 8, 8, 16 };

#if defined(FALLOUT_PRE_NG)
	// Optional GPU timing for the clustered compute block (building + culling
	// dispatch), gated by FO4CS_LLF_PRENG_GPU_TIMING. Double-buffered timestamp
	// queries so GetData reads the previous frame's result without stalling the
	// pipeline. Diagnostic only; created lazily in Prepass on first timed frame.
	static constexpr std::uint32_t kPreNGGpuTimerFrames = 2;
	struct PreNGGpuTimer
	{
		winrt::com_ptr<ID3D11Query> disjoint;
		winrt::com_ptr<ID3D11Query> begin;
		winrt::com_ptr<ID3D11Query> end;
		bool pending = false;
	};
	PreNGGpuTimer preNGClusterGpuTimers[kPreNGGpuTimerFrames];
	bool preNGClusterGpuTimersReady = false;
	std::uint32_t preNGClusterGpuTimerIndex = 0;
#endif
};
