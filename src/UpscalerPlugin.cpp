#include "PluginCommon.h"

#include "DX11Hooks.h"
#include "Upscaler.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* message)
	{
		switch (message->type) {
		case F4SE::MessagingInterface::kPostPostLoad:
			Upscaling::GetSingleton()->PostPostLoad();
			break;
		default:
			break;
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

	auto upscaling = Upscaling::GetSingleton();
	upscaling->pluginMode = Upscaling::PluginMode::kUpscaler;
	upscaling->LoadSettings();

	DX11Hooks::Install();

	auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(MessageHandler);

	return true;
}
