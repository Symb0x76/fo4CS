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
		[[nodiscard]] bool IsLoaded() const noexcept { return loaded; }

		[[nodiscard]] std::uint64_t GetFrameCount() const noexcept { return frameCount; }
		[[nodiscard]] ID3D11Device* GetDevice() const noexcept { return d3d11Device; }

	private:
		Runtime() = default;

		bool loaded = false;
		std::uint64_t frameCount = 0;
		ID3D11Device* d3d11Device = nullptr;
	};
}
