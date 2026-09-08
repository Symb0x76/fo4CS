#include "Upscaling/Upscaler.h"

#include "Platform/RE/CameraData.h"
#include "Platform/RE/SingletonAccessors.h"
#include <RE/FO4Runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostics/HangTrace.h"
#include "Render/DX12SwapChain.h"
#include "Render/PresentationMenuPolicy.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Upscaling/UpscalingInternal.h"
#include "Upscaling/UpscalingRenderTargetIDs.h"

using fo4cs::upscaling::TraceRenderBackendStage;

namespace
{
	struct BSGraphics_State_UpdateDynamicResolution
	{
		static void thunk(RE::BSGraphics::RenderTargetManager* This, RE::NiPoint3* a2, RE::NiPoint3* a3, RE::NiPoint3* a4, RE::NiPoint3* a5)
		{
			TraceRenderBackendStage("hook:UpdateDynamicResolution:game");
			fo4cs::diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:game:begin");
			func(This, a2, a3, a4, a5);
			fo4cs::diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:game:end");
			TraceRenderBackendStage("hook:UpdateDynamicResolution:fo4cs");
			fo4cs::diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:fo4cs:begin");
			Upscaling::GetSingleton()->UpdateUpscaling();
			fo4cs::diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:fo4cs:end");
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ImageSpaceEffectTemporalAA_IsActive
	{
		static bool thunk(void* This)
		{
			auto* upscaling = Upscaling::GetSingleton();
			return !upscaling->nativePresentationModeActive &&
			       upscaling->upscaleMethod == Upscaling::UpscaleMethod::kDisabled &&
			       func(This);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_Render_PreUI_DeferredPrePass
	{
		static void thunk(void* This)
		{
			TraceRenderBackendStage("hook:PreUI_DeferredPrePass");
			auto upscaling = Upscaling::GetSingleton();
			upscaling->OverrideSamplerStates();
			func(This);
			upscaling->ResetSamplerStates();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DrawWorld_Render_PreUI_Forward
	{
		static void thunk(void* This)
		{
			TraceRenderBackendStage("hook:PreUI_Forward");
			auto upscaling = Upscaling::GetSingleton();
			upscaling->OverrideSamplerStates();
			func(This);
			upscaling->ResetSamplerStates();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

void InstallUpscalerRenderBackendHooks()
{
	stl::write_vfunc<0x8, ImageSpaceEffectTemporalAA_IsActive>(RE::VTABLE::ImageSpaceEffectTemporalAA[0]);

#if defined(FALLOUT_POST_NG)
	namespace F4Hooks = RE::FO4Runtime::PostNG::Hooks;
	stl::write_thunk_call<BSGraphics_State_UpdateDynamicResolution>(F4Hooks::UPSCALER_RENDER_BACKEND_DYNAMIC_RESOLUTION_CALL.address());
	stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(F4Hooks::UPSCALER_RENDER_BACKEND_DEFERRED_PREPASS_CALL.address());
	stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(F4Hooks::UPSCALER_RENDER_BACKEND_FORWARD_CALL.address());
#else
	namespace F4Hooks = RE::FO4Runtime::PreNG::Hooks;
	stl::write_thunk_call<BSGraphics_State_UpdateDynamicResolution>(F4Hooks::UPSCALER_RENDER_BACKEND_DYNAMIC_RESOLUTION_CALL.address());
	stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(F4Hooks::UPSCALER_RENDER_BACKEND_DEFERRED_PREPASS_CALL.address());
	stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(F4Hooks::UPSCALER_RENDER_BACKEND_FORWARD_CALL.address());
#endif

	logger::debug("[Upscaler] Installed render backend hooks");
}
