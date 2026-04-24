#include "PluginCommon.h"

#include "DX11Hooks.h"
#include "Upscaling.h"

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
extern "C" DLLEXPORT auto F4SEPlugin_Version = []() noexcept {
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
	F4SE::Init(a_f4se);
	fo4cs::WaitForDebuggerIfNeeded();
	fo4cs::InitializeLog();

	DX11Hooks::Install();
	Upscaling::GetSingleton()->LoadSettings();

	auto messaging = F4SE::GetMessagingInterface();
	messaging->RegisterListener(MessageHandler);

	return true;
}
