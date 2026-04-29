#include "Core/State.h"

#include <memory>

namespace CommunityShaders
{
	State* State::GetSingleton()
	{
		static State singleton;
		return std::addressof(singleton);
	}

	void State::Refresh()
	{
#if defined(FALLOUT_POST_AE)
		runtimeFlavor = RuntimeFlavor::PostAE;
		runtimeName = "PostAE";
#elif defined(FALLOUT_POST_NG)
		runtimeFlavor = RuntimeFlavor::PostNG;
		runtimeName = "PostNG";
#else
		runtimeFlavor = RuntimeFlavor::PreNG;
		runtimeName = "PreNG";
#endif

		vr = false;
	}
}
