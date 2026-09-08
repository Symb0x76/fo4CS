#include "Upscaling/Upscaler.h"

#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"

void InstallUpscalerRenderBackendHooks();

void Upscaling::PostPostLoad()
{
	highFPSPhysicsFixLoaded = GetModuleHandleA("Data\\F4SE\\Plugins\\HighFPSPhysicsFix.dll") != nullptr;

	logger::debug("[FrameGen] HighFPSPhysicsFix.dll loaded: {}", highFPSPhysicsFixLoaded);

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
	stl::detour_thunk<WindowSizeChanged>(REL::ID(2276824));
	stl::write_thunk_call<SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(2318322).address() + 0xC5);
	stl::detour_thunk<DrawWorld_Forward>(REL::ID(2318315));
	stl::write_thunk_call<DrawWorld_Reticle>(REL::ID(2318315).address() + 0x53D);
#else
	// Fix game initialising twice
	stl::detour_thunk<WindowSizeChanged>(REL::ID(212827));

	// Watch frame presentation
	stl::write_thunk_call<SetUseDynamicResolutionViewportAsDefaultViewport>(REL::ID(587723).address() + 0xE1);

	// Fix reticles on motion vectors and depth
	stl::detour_thunk<DrawWorld_Forward>(REL::ID(656535));
	stl::write_thunk_call<DrawWorld_Reticle>(REL::ID(338205).address() + 0x253);
#endif

	logger::debug("[Upscaler] Installed hooks");
}
