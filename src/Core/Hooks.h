#pragma once

#include <d3d11.h>

namespace CommunityShaders::Hooks
{
	void Install();
	void OnD3D11DeviceCreated(ID3D11Device* a_device);
}
