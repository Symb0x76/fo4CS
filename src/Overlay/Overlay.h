#pragma once

#include <cstdint>
#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

struct ImGuiContext;

struct ShaderDumpStats
{
	bool dumpingEnabled = false;
	std::uint32_t uniqueShaders = 0;
	std::uint32_t dumpedShaders = 0;
	std::uint32_t vsCount = 0;
	std::uint32_t psCount = 0;
	std::uint32_t csCount = 0;
	std::string runtimeName;
};

class Overlay
{
public:
	static Overlay* GetSingleton();

	bool Initialize(ID3D12Device* a_device, ID3D12CommandQueue* a_commandQueue, IDXGISwapChain4* a_swapChain, DXGI_FORMAT a_swapChainFormat, HWND a_hwnd);
	void Shutdown();
	[[nodiscard]] bool IsInitialized() const noexcept { return initialized; }
	[[nodiscard]] bool IsVisible() const noexcept { return visible; }
	void ToggleVisible() noexcept { visible = !visible; }

	void Render(ID3D12GraphicsCommandList4* a_commandList, ID3D12Resource* a_backBuffer, DXGI_FORMAT a_swapChainFormat);

	[[nodiscard]] ImGuiContext* GetImGuiContext() const noexcept { return imguiContext; }
	[[nodiscard]] ID3D12DescriptorHeap* GetSrvHeap() const noexcept { return srvHeap.get(); }
	[[nodiscard]] HWND GetHwnd() const noexcept { return hwnd; }
	[[nodiscard]] WNDPROC GetPreviousWndProc() const noexcept { return previousWndProc; }

	void SetShaderDumpStats(const ShaderDumpStats& a_stats) { shaderDumpStats = a_stats; }

	HWND hwnd = nullptr;
	WNDPROC previousWndProc = nullptr;

private:
	Overlay() = default;


	bool initialized = false;
	bool visible = false;
	bool sharedContext = false;

	winrt::com_ptr<ID3D12DescriptorHeap> srvHeap;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	ImGuiContext* imguiContext = nullptr;

	ShaderDumpStats shaderDumpStats;
};
