#include "Upscaling/Upscaler.h"

#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"

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

	// Fix reticles on motion vectors and depth
	stl::detour_thunk<DrawWorld_Forward>(F4Hooks::UPSCALER_DRAW_WORLD_FORWARD);
	stl::write_thunk_call<DrawWorld_Reticle>(F4Hooks::UPSCALER_DRAW_WORLD_RETICLE_CALL.address());
#endif

	logger::debug("[Upscaler] Installed hooks");
}
