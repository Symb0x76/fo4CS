#pragma once

// Wrapper: abstracts game-version-specific REL::ID lookups for camera
// near/far plane global variables. All fo4CS code calls GetCameraNear() /
// GetCameraFar() instead of *(float*)REL::ID(X).address().
//
// CommonLibFO4 remains pure RE — the #if guards stay here, in fo4CS.

namespace fo4cs::RE
{
	[[nodiscard]] inline float GetCameraNear()
	{
#if defined(FALLOUT_POST_NG)
		static auto ptr = (float*)REL::ID(2712882).address();
#else
		static auto ptr = (float*)REL::ID(57985).address();
#endif
		return *ptr;
	}

	[[nodiscard]] inline float GetCameraFar()
	{
#if defined(FALLOUT_POST_NG)
		static auto ptr = (float*)REL::ID(2712883).address();
#else
		static auto ptr = (float*)REL::ID(958877).address();
#endif
		return *ptr;
	}
}
