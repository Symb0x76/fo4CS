#pragma once

// Wrapper: abstracts version-specific singleton REL::ID lookups.
// Each game version has different REL::IDs for the same logical singletons.
// All fo4CS code calls these functions instead of querying REL::ID(X).address().

#include <d3d11.h>

namespace RE::BSGraphics {}
namespace fo4cs::RE
{
	[[nodiscard]] inline ::RE::BSGraphics::State* GetGraphicsState()
	{
#if defined(FALLOUT_POST_NG)
		static REL::Relocation<::RE::BSGraphics::State**> ptr{ REL::ID(2704621) };
#else
		static REL::Relocation<::RE::BSGraphics::State**> ptr{ REL::ID(600795) };
#endif
		return *ptr;
	}

	[[nodiscard]] inline ::RE::BSGraphics::RenderTargetManager* GetRenderTargetManager()
	{
#if defined(FALLOUT_POST_NG)
		static REL::Relocation<::RE::BSGraphics::RenderTargetManager**> ptr{ REL::ID(2666735) };
#else
		static REL::Relocation<::RE::BSGraphics::RenderTargetManager**> ptr{ REL::ID(1508457) };
#endif
		return *ptr;
	}

	[[nodiscard]] inline ID3D11SamplerState** GetSamplerStateArray()
	{
#if defined(FALLOUT_POST_NG)
		static REL::Relocation<ID3D11SamplerState**> ptr{ REL::ID(2704455) };
#else
		static REL::Relocation<ID3D11SamplerState**> ptr{ REL::ID(44312) };
#endif
		return ptr.get();
	}

	[[nodiscard]] inline bool* GetTAAEnableFlag()
	{
#if defined(FALLOUT_POST_NG)
		static REL::Relocation<bool*> ptr{ REL::ID(2704658) };
#else
		static REL::Relocation<bool*> ptr{ REL::ID(460417) };
#endif
		return ptr.get();
	}
}
