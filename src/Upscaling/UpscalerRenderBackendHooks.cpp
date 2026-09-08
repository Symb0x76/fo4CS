#include "Upscaling/Upscaler.h"

#include "Platform/RE/CameraData.h"
#include "Platform/RE/SingletonAccessors.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostics/HangTrace.h"
#include "Render/DX12SwapChain.h"
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
			fo4cs::Diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:game:begin");
			func(This, a2, a3, a4, a5);
			fo4cs::Diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:game:end");
			TraceRenderBackendStage("hook:UpdateDynamicResolution:fo4cs");
			fo4cs::Diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:fo4cs:begin");
			Upscaling::GetSingleton()->UpdateUpscaling();
			fo4cs::Diagnostics::WriteHangTraceLine("hook:UpdateDynamicResolution:fo4cs:end");
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ImageSpaceEffectTemporalAA_IsActive
	{
		static bool thunk(void* This)
		{
			return Upscaling::GetSingleton()->upscaleMethod == Upscaling::UpscaleMethod::kDisabled && func(This);
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
	stl::write_thunk_call<BSGraphics_State_UpdateDynamicResolution>(REL::ID(2318321).address() + 0x29F);
	stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(REL::ID(2318321).address() + 0x2E3);
	stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(REL::ID(2318321).address() + 0x3A6);
#else
	stl::write_thunk_call<BSGraphics_State_UpdateDynamicResolution>(REL::ID(984743).address() + 0x14B);
	stl::write_thunk_call<DrawWorld_Render_PreUI_DeferredPrePass>(REL::ID(984743).address() + 0x17F);
	stl::write_thunk_call<DrawWorld_Render_PreUI_Forward>(REL::ID(984743).address() + 0x1C9);
#endif

	logger::debug("[Upscaler] Installed render backend hooks");
}
