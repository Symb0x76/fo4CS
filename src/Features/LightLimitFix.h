#pragma once

#include <d3d11.h>
#include <set>
#include <vector>
#include <winrt/base.h>

#include "Core/Feature.h"

// Light Limit Fix removes the vanilla 4-light limit by replacing the per-draw
// strict-light CB with a clustered-forward light grid computed on the GPU.
//
// FO4 Adaptation Notes (vs Skyrim CS):
//   - BSShader::SetupGeometry is at vfunc index 7 (FO4 added SetupMaterialSecondary)
//   - FO4 uses BSShaderManager::ShaderEnum (kLighting=8, kEffect=0, kWater=0xA)

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
			"Removes the vanilla 4-light limit using clustered-forward rendering.",
			{
				"GPU cluster building + culling via compute shaders",
				"Structured light buffer replaces per-draw strict-light CB",
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
		std::uint64_t roomFlags = 0;
		std::uint32_t lightFlags = 0;
		std::uint32_t shadowMaskIndex = 0;
		std::uint32_t pad0;
		std::uint32_t pad1;
		std::uint64_t pad2[2];
	};
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

	// Matches ClusterCullingCS.hlsl cbuffer PerFrame (register b0, 96 bytes)
	struct alignas(16) LightCullingCB
	{
		std::uint32_t LightCount;
		std::uint32_t pad[3];               // → 16
		std::uint32_t ClusterSize[4];       // → 32
		float4x4 CameraView;                 // → 96
	};

	struct alignas(16) PerFrame
	{
		std::uint32_t EnableLightsVisualisation;
		std::uint32_t LightsVisualisationMode;
		float pad0[2];
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
	[[nodiscard]] bool HasResources() const { return clusterBuildingCS && clusterCullingCS; }
	void PostPostLoad() override;
	void DataLoaded() override;
	void Prepass() override;
	void Reset() override;

	// --- SetupGeometry hooks ---
	void SetupGeometryBefore(RE::BSRenderPass* a_pass);
	void SetupGeometryAfter(RE::BSRenderPass* a_pass);

	// Per-frame light accumulation
	void CollectLightsFromPass(RE::BSRenderPass* a_pass);
	void CollectLightsFromScene();
	void CollectLightsFromBSLight();
	void CollectLightCB();
	std::vector<LightData> frameLights;
	std::set<RE::BSLight*> seenLights;
	std::vector<RE::BSLight*> seenThisPass;
	std::set<std::uint64_t> seenCBHashes;
	std::uint32_t diagFrameCounter = 0;

	struct Hooks
	{
		struct BSLightingShader_SetupGeometry
		{
			static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass, std::uint32_t a_renderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSEffectShader_SetupGeometry
		{
			static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass, std::uint32_t a_renderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install();
	};

private:
	// --- GPU Resources (RAII: winrt::com_ptr auto-releases on destruction) ---
	winrt::com_ptr<ID3D11ComputeShader>          clusterBuildingCS;
	winrt::com_ptr<ID3D11ComputeShader>          clusterCullingCS;
	winrt::com_ptr<ID3D11Buffer>                 lightBuildingCB;
	winrt::com_ptr<ID3D11Buffer>                 lightCullingCB;
	winrt::com_ptr<ID3D11Buffer>                 lightsBuffer;
	winrt::com_ptr<ID3D11ShaderResourceView>     lightsSRV;
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

	std::uint32_t clusterSize[3] = { 16, 16, 32 };
};
