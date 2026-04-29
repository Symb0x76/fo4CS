#pragma once

#include <d3d11.h>

namespace CommunityShaders
{
	class Runtime
	{
	public:
		static Runtime* GetSingleton();

		void Load();
		void PostPostLoad();
		void OnD3D11DeviceCreated(ID3D11Device* a_device);
		void OnFrame();

	private:
		Runtime() = default;

		bool loaded = false;
		std::uint64_t frameCount = 0;
	};
}
