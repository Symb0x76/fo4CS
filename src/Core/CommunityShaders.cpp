#include "Core/CommunityShaders.h"

#include "Core/BSShaderHooks.h"
#include "Core/Deferred.h"
#include "Core/Globals.h"
#include "Core/Hooks.h"
#include "Core/Menu.h"
#include "Core/ShaderCompiler.h"
#include "Core/ShaderDB.h"
#include "Core/State.h"
#include "Diagnostics/LogEvents.h"
#include "Render/RuntimeAdapter.h"

#include <memory>

#ifdef TRACY_ENABLE
#	include "Core/Feature.h"

#	include <Tracy/Tracy.hpp>
#	include <Tracy/TracyD3D11.hpp>

#	include <wrl/client.h>
#endif

using fo4cs::diagnostics::Event;
using fo4cs::diagnostics::LogEvent;

#ifdef TRACY_ENABLE
namespace
{
	// Owned for the process lifetime. F4SE plugins get no unload callback, so there
	// is no TracyD3D11Destroy site; the context dies with the process.
	TracyD3D11Ctx g_tracyCtx = nullptr;
}
#endif

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
		BSShaderHooks::Install();
		if (fo4cs::RuntimeAdapter::Get().GetCapabilities().supportsDeferredPipeline) {
			Deferred::Hooks::Install();
		}

		ShaderCompiler::GetSingleton()->SetSourceRoot("Data\\Shaders");

		// Load user-supplied ShaderDB entries (fallback mechanism)
		auto runtimeName = State::GetSingleton()->GetRuntimeName();
		auto dbPath = std::filesystem::path(
			"Data\\F4SE\\Plugins\\CommunityShaders\\ShaderDB") / std::string(runtimeName);
		ShaderDB::GetSingleton()->Load(runtimeName, dbPath.string());
		ShaderDB::GetSingleton()->Load("Common",
			(std::filesystem::path(
				"Data\\F4SE\\Plugins\\CommunityShaders\\ShaderDB") / "Common").string());

		LoadFeatures();

		loaded = true;
		LogEvent(Event::HookInstall, "[CommunityShaders] Foundation loaded for {}",
			State::GetSingleton()->GetRuntimeName());
	}

	void Runtime::PostPostLoad()
	{
		CommunityShaders::PostPostLoad();
	}

	void Runtime::OnD3D11DeviceCreated(ID3D11Device* a_device)
	{
		d3d11Device = a_device;
		LogEvent(Event::DeviceReady, "[CommunityShaders] D3D11 device created");
#ifdef TRACY_ENABLE
		if (!g_tracyCtx && a_device) {
			Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
			a_device->GetImmediateContext(immediate.GetAddressOf());
			if (immediate) {
				g_tracyCtx = TracyD3D11Context(a_device, immediate.Get());
				Feature::SetTracyCtx(g_tracyCtx);
			}
		}
#endif
		Hooks::OnD3D11DeviceCreated(a_device);
		Deferred::GetSingleton()->SetupResources();
		SetupResources();
		LogEvent(Event::ResourceCreate, "[CommunityShaders] Deferred and feature resources set up");
	}

	void Runtime::OnFrame()
	{
		if (!loaded) {
			return;
		}

		++frameCount;
		Hooks::OnFrame();
		BSShaderHooks::OnFrame();  // drain deferred shader replacements
		ResetFeatures();
		Menu::Reset();
		Menu::Draw();
#ifdef TRACY_ENABLE
		if (g_tracyCtx) {
			TracyD3D11Collect(g_tracyCtx);
		}
		FrameMark;
#endif
	}
}
