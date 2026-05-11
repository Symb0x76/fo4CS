#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include <d3d11.h>
#include <winrt/base.h>

namespace RE
{
	class BSBatchRenderer;
	class BSShaderAccumulator;
	class NiAVObject;
}

class Deferred
{
public:
	static Deferred* GetSingleton()
	{
		static Deferred singleton;
		return std::addressof(singleton);
	}

	void SetupResources();
	void ReflectionsPrepasses();
	void EarlyPrepasses();
	void StartDeferred();
	void EndDeferred();
	void PrepassPasses();
	void DeferredPasses();

	void OverrideBlendStates();
	void ResetBlendStates();
	[[nodiscard]] bool IsBlendOverridden() const noexcept { return blendStatesOverridden; }

	// Blend state extension for MRT: intercepts OMSetBlendState during deferred pass,
	// cloning single-RT blend states to cover RTs [0..7] with identical settings.
	[[nodiscard]] ID3D11BlendState* GetOrCreateMRTBlendState(ID3D11BlendState* a_original);

	void ClearShaderCache();

	// --- Hooks into FO4 rendering pipeline ---
	// REL::ID offsets need to be resolved per runtime flavor (PreNG/PostNG/PostAE).
	// These slots correspond to the Skyrim CS counterpart hooks:
	//   Main_RenderShadowMaps    → FO4's shadow map rendering dispatch
	//   Main_RenderWorld         → FO4's main world render entry
	//   Main_RenderWorld_Start   → FO4's opaque geometry batch start
	//   Main_RenderWorld_BlendedDecals → FO4's blend/decals post-pass
	//   Renderer_ResetState      → FO4's render state reset
	struct Hooks
	{
		struct Main_RenderShadowMaps
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_Start
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_BlendedDecals
		{
			static void thunk();
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Renderer_ResetState
		{
			static void thunk(void* a_this);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install();
	};

private:
	Deferred() = default;

	bool deferredPass = false;
	bool blendStatesOverridden = false;
	std::unordered_map<ID3D11BlendState*, winrt::com_ptr<ID3D11BlendState>> blendStateCache;
};
