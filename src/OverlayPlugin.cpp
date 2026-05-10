#include "PluginCommon.h"

#include "Overlay/Overlay.h"

// Overlay.dll is a standalone F4SE plugin.
// DX12SwapChain resolves Overlay_OnSwapChainCreated / Overlay_OnPresent / Overlay_OnPollHotkey
// via GetProcAddress at swap-chain creation time — no per-DLL callback registration needed.
// Feature DLLs call Overlay_RegisterPanel via GetProcAddress to register their settings panels.

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* message)
	{
		if (message->type == F4SE::MessagingInterface::kPostPostLoad) {
			logger::info("[Overlay] PostPostLoad - {} panels registered",
				Overlay::GetSingleton()->IsInitialized() ? "ready" : "waiting for D3D12 device");
		}
	}
}

#if defined(FALLOUT_POST_NG)
extern "C" DLLEXPORT constinit F4SE::PluginVersionData F4SEPlugin_Version = []() consteval {
	F4SE::PluginVersionData data{};
	fo4cs::PopulateVersionData(data);
	return data;
}();
#else
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface*, F4SE::PluginInfo* a_info)
{
	fo4cs::PopulatePluginInfo(a_info);
	return true;
}
#endif

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
#if defined(FALLOUT_POST_AE)
	F4SE::Init(a_f4se, { .trampoline = true, .trampolineSize = 16 * stl::kThunkCallTrampolineSize });
#else
	F4SE::Init(a_f4se);
	F4SE::AllocTrampoline(16 * stl::kThunkCallTrampolineSize);
#endif
	fo4cs::WaitForDebuggerIfNeeded();
	fo4cs::InitializeLog();

	logger::info("[Overlay] Plugin loaded - exporting callbacks for DX12SwapChain resolution");

	auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(MessageHandler);

	return true;
}
