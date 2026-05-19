#pragma once

#include <d3d11.h>

// D3D11 Create*Shader vtable hooks — bytecode observation only.
// No shader replacement here — that's handled by BSShaderHooks (engine level).
// These hooks feed ShaderCache::ObserveShader for ShaderDump RE tooling.

namespace CommunityShaders::Hooks
{
	void Install();
	void OnD3D11DeviceCreated(ID3D11Device* a_device);
}
