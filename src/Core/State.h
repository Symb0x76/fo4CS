#pragma once

#include <string>

namespace CommunityShaders
{
	enum class RuntimeFlavor
	{
		PreNG,
		PostNG,
		PostAE
	};

	class State
	{
	public:
		static State* GetSingleton();

		void Refresh();

		[[nodiscard]] RuntimeFlavor GetRuntimeFlavor() const noexcept { return runtimeFlavor; }
		[[nodiscard]] bool IsVR() const noexcept { return vr; }
		[[nodiscard]] bool IsENBLoaded() const noexcept { return enbLoaded; }
		[[nodiscard]] const std::string& GetRuntimeName() const noexcept { return runtimeName; }

		void SetENBLoaded(bool a_loaded) noexcept { enbLoaded = a_loaded; }

	private:
		State() = default;

		RuntimeFlavor runtimeFlavor = RuntimeFlavor::PreNG;
		bool vr = false;
		bool enbLoaded = false;
		std::string runtimeName = "PreNG";
	};
}
