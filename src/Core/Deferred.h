#pragma once

namespace CommunityShaders
{
	class Deferred
	{
	public:
		static Deferred* GetSingleton();

		void SetupResources();
		void ReflectionsPrepasses();
		void EarlyPrepasses();
		void StartDeferred();
		void OverrideBlendStates();
		void ResetBlendStates();
		void DeferredPasses();
		void EndDeferred();
		void PrepassPasses();
		void ClearShaderCache();

		[[nodiscard]] bool IsDeferredPass() const noexcept { return deferredPass; }

	private:
		Deferred() = default;

		bool deferredPass = false;
	};
}
