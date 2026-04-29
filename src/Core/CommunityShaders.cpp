#include "Core/CommunityShaders.h"

#include "Core/Globals.h"
#include "Core/Hooks.h"
#include "Core/Menu.h"
#include "Core/ShaderCache.h"
#include "Core/State.h"

#include <memory>

namespace CommunityShaders
{
	Runtime* Runtime::GetSingleton()
	{
		static Runtime singleton;
		return std::addressof(singleton);
	}

	void Runtime::Load()
	{
		if (loaded) {
			return;
		}

		State::GetSingleton()->Refresh();
		Hooks::Install();
		LoadFeatures();

		loaded = true;
		logger::info("[CommunityShaders] Foundation loaded for {}", State::GetSingleton()->GetRuntimeName());
	}

	void Runtime::PostPostLoad()
	{
		CommunityShaders::PostPostLoad();
	}

	void Runtime::OnD3D11DeviceCreated(ID3D11Device* a_device)
	{
		Hooks::OnD3D11DeviceCreated(a_device);
		SetupResources();
	}

	void Runtime::OnFrame()
	{
		if (!loaded) {
			return;
		}

		++frameCount;
		ResetFeatures();
		Menu::Reset();
		Menu::Draw();
	}
}
