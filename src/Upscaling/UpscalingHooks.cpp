#include "Upscaling/Upscaler.h"

#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"

// InstallHooks names RE::FO4Runtime::{PreNG,PostNG}::Hooks directly. PreNG got
// this header transitively, which is why the omission only ever broke PostNG.
#include <RE/FO4Runtime.h>

void InstallUpscalerRenderBackendHooks();

void Upscaling::PostPostLoad()
{
	highFPSPhysicsFixLoaded = GetModuleHandleW(L"HighFPSPhysicsFix.dll") != nullptr;
	highFPSPhysicsFixIniReadable = false;
	highFPSPhysicsFixUntiesSpeed = false;
	highFPSPhysicsFixOwnLimiterActive = false;
	highFPSPhysicsFixInGameLimit = 0.0f;
	highFPSPhysicsFixExteriorLimit = 0.0f;
	highFPSPhysicsFixInteriorLimit = 0.0f;

	if (highFPSPhysicsFixLoaded) {
		constexpr auto iniPath = "Data\\F4SE\\Plugins\\HighFPSPhysicsFix.ini";
		CSimpleIniA ini;
		ini.SetUnicode();

		std::error_code ec;
		if (!std::filesystem::exists(iniPath, ec)) {
			logger::warn("[FrameGen] High FPS Physics Fix INI is not VFS-visible at {}; using conservative limiter policy", iniPath);
		} else if (ini.LoadFile(iniPath) < 0) {
			logger::warn("[FrameGen] Failed to read {}; using conservative limiter policy", iniPath);
		} else {
			const bool hasRequiredSettings =
				ini.GetValue("Main", "UntieSpeedFromFPS") &&
				ini.GetValue("Limiter", "InGameFPS") &&
				ini.GetValue("Limiter", "ExteriorFPS") &&
				ini.GetValue("Limiter", "InteriorFPS");
			if (!hasRequiredSettings) {
				logger::warn("[FrameGen] {} is missing required physics/limiter keys; using conservative limiter policy", iniPath);
			} else {
				highFPSPhysicsFixUntiesSpeed = ini.GetBoolValue("Main", "UntieSpeedFromFPS", false);
				highFPSPhysicsFixInGameLimit = static_cast<float>(ini.GetDoubleValue("Limiter", "InGameFPS", 0.0));
				highFPSPhysicsFixExteriorLimit = static_cast<float>(ini.GetDoubleValue("Limiter", "ExteriorFPS", 0.0));
				highFPSPhysicsFixInteriorLimit = static_cast<float>(ini.GetDoubleValue("Limiter", "InteriorFPS", 0.0));

				highFPSPhysicsFixIniReadable =
					std::isfinite(highFPSPhysicsFixInGameLimit) &&
					std::isfinite(highFPSPhysicsFixExteriorLimit) &&
					std::isfinite(highFPSPhysicsFixInteriorLimit);
				if (!highFPSPhysicsFixIniReadable) {
					logger::warn("[FrameGen] {} contains non-finite limiter values; using conservative limiter policy", iniPath);
					highFPSPhysicsFixUntiesSpeed = false;
					highFPSPhysicsFixInGameLimit = 0.0f;
					highFPSPhysicsFixExteriorLimit = 0.0f;
					highFPSPhysicsFixInteriorLimit = 0.0f;
				}
			}
		}
	}

	highFPSPhysicsFixOwnLimiterActive =
		highFPSPhysicsFixLoaded && highFPSPhysicsFixIniReadable &&
		(highFPSPhysicsFixInGameLimit != 0.0f ||
		 highFPSPhysicsFixExteriorLimit != 0.0f ||
		 highFPSPhysicsFixInteriorLimit != 0.0f);
	ResolveFrameLimiterPolicy(true);

	renderBackendEnabled = pluginMode == PluginMode::kUpscaler &&
		((UsesDLSSUpscaling() && Streamline::GetSingleton()->featureDLSS) ||
		 (UsesFSRUpscaling() && d3d12Interop && FidelityFX::GetSingleton()->featureFSR));
	InstallHooks();
}

struct WindowSizeChanged
{
	static void thunk(RE::BSGraphics::Renderer*, unsigned int)
	{
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct SetUseDynamicResolutionViewportAsDefaultViewport
{
	static void thunk(RE::BSGraphics::RenderTargetManager* This, bool a_true)
	{
		func(This, a_true);
		if (!a_true) {
			auto* upscaling = Upscaling::GetSingleton();
			upscaling->Upscale();
			upscaling->PostDisplay();
		}
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

bool reticleFix = false;

struct DrawWorld_Forward
{
	static void thunk(void* a1)
	{		
		func(a1);

		if (!reticleFix)
			Upscaling::GetSingleton()->CopyBuffersToSharedResources();

		reticleFix = false;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

// NOTE ON THE NAME: this hook and its REL::ID are called "Reticle", but the call
// they patch has nothing to do with the crosshair. Verified from the 1.10.163
// binary: UPSCALER_DRAW_WORLD_RETICLE_CALL resolves to sub_1428568B0 + 0x253,
// whose bytes are `E8 D8 87 FD FF` -> a call to sub_14282F2E0, a whole-view
// BSShaderAccumulator flush (sort the pending pass list by depth, distribute into
// per-pass buckets, clear). The same callee is invoked twice more in that function
// with different accumulators.
//
// Fallout 4 has no native crosshair draw at all -- the crosshair is the Scaleform
// MovieClip CenterGroup_mc.HUDCrosshair_mc inside HUDMenu, rendered by the GFx HAL
// in a completely separate code region. A "reticle" extraction pass used to hang
// off this hook; it produced only artifacts and has been removed.
//
// The hook itself stays, and must: PreAlpha()/PostAlpha() are what produce the
// shared motion-vector and depth buffers that frame generation consumes. It is a
// convenient per-frame bracket around the scene flush, nothing more. Renaming it
// would mean renaming the REL::ID in the CommonLibF4 submodule, so the name is
// left alone and documented here instead.
struct DrawWorld_Reticle
{
	static void thunk(void* a1)
	{
		auto upscaling = Upscaling::GetSingleton();
		upscaling->PreAlpha();
		func(a1);
		reticleFix = true;
		upscaling->PostAlpha();
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void Upscaling::InstallHooks()
{
	if (GetSingleton()->pluginMode == PluginMode::kUpscaler)
		InstallUpscalerRenderBackendHooks();

#if defined(FALLOUT_POST_NG)
	namespace F4Hooks = RE::FO4Runtime::PostNG::Hooks;
	stl::detour_thunk<WindowSizeChanged>(F4Hooks::UPSCALER_WINDOW_SIZE_CHANGED);
	stl::write_thunk_call<SetUseDynamicResolutionViewportAsDefaultViewport>(F4Hooks::UPSCALER_SET_DEFAULT_VIEWPORT_CALL.address());
	stl::detour_thunk<DrawWorld_Forward>(F4Hooks::UPSCALER_DRAW_WORLD_FORWARD);
	stl::write_thunk_call<DrawWorld_Reticle>(F4Hooks::UPSCALER_DRAW_WORLD_RETICLE_CALL.address());
#else
	namespace F4Hooks = RE::FO4Runtime::PreNG::Hooks;
	// Fix game initialising twice
	stl::detour_thunk<WindowSizeChanged>(F4Hooks::UPSCALER_WINDOW_SIZE_CHANGED);

	// Watch frame presentation
	stl::write_thunk_call<SetUseDynamicResolutionViewportAsDefaultViewport>(F4Hooks::UPSCALER_SET_DEFAULT_VIEWPORT_CALL.address());

	// Per-frame bracket for the shared motion-vector and depth buffers
	// (see the note on DrawWorld_Reticle above -- the name is a misnomer)
	stl::detour_thunk<DrawWorld_Forward>(F4Hooks::UPSCALER_DRAW_WORLD_FORWARD);
	stl::write_thunk_call<DrawWorld_Reticle>(F4Hooks::UPSCALER_DRAW_WORLD_RETICLE_CALL.address());
#endif

	logger::debug("[Upscaler] Installed hooks");
}
