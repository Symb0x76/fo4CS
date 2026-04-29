#include "Core/Deferred.h"

#include <memory>

namespace CommunityShaders
{
	Deferred* Deferred::GetSingleton()
	{
		static Deferred singleton;
		return std::addressof(singleton);
	}

	void Deferred::SetupResources() {}
	void Deferred::ReflectionsPrepasses() {}
	void Deferred::EarlyPrepasses() {}
	void Deferred::StartDeferred() { deferredPass = true; }
	void Deferred::OverrideBlendStates() {}
	void Deferred::ResetBlendStates() {}
	void Deferred::DeferredPasses() {}
	void Deferred::EndDeferred() { deferredPass = false; }
	void Deferred::PrepassPasses() {}
	void Deferred::ClearShaderCache() {}
}
