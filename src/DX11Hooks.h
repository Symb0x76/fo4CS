#pragma once

struct ID3D11Device;

namespace DX11Hooks
{
	void Install();
	void NotifyD3D11DeviceCreated(ID3D11Device* a_device);
}
