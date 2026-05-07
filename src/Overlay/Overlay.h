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
	void ToggleVisible() noexcept;
	[[nodiscard]] bool ShouldRender() const noexcept { return visible || IsShowingIntroMessage(); }

	[[nodiscard]] float GetUIScale() const noexcept { return dpiScale * uiScaleOverride; }
	[[nodiscard]] float GetUIScaleOverride() const noexcept { return uiScaleOverride; }
	void SetUIScaleOverride(float a_scale) noexcept { uiScaleOverride = a_scale; }

	void Render(ID3D12GraphicsCommandList4* a_commandList, ID3D12Resource* a_backBuffer, DXGI_FORMAT a_swapChainFormat);

	[[nodiscard]] ImGuiContext* GetImGuiContext() const noexcept { return imguiContext; }
	[[nodiscard]] ID3D12DescriptorHeap* GetSrvHeap() const noexcept { return srvHeap.get(); }
	[[nodiscard]] HWND GetHwnd() const noexcept { return hwnd; }
	[[nodiscard]] WNDPROC GetPreviousWndProc() const noexcept { return previousWndProc; }

	void SetShaderDumpStats(const ShaderDumpStats& a_stats) { shaderDumpStats = a_stats; }

	[[nodiscard]] int GetHotkey() const noexcept { return hotkey; }
	void StartCapturingHotkey() noexcept { capturingHotkey = true; }
	[[nodiscard]] bool IsCapturingHotkey() const noexcept { return capturingHotkey; }
	void SetHotkey(int a_key) noexcept { hotkey = a_key; capturingHotkey = false; hotkeyWasDown = true; }
	[[nodiscard]] bool HandleKeyDown(int a_key) noexcept;
	void PollHotkeyState() noexcept;
	[[nodiscard]] bool IsIntroEnabled() const noexcept { return showIntroEnabled; }
	void SetIntroEnabled(bool a_enabled) noexcept;

	// Intro notification shown after plugin load until dismissed or gameplay starts
	[[nodiscard]] bool IsShowingIntroMessage() const noexcept;
	void DismissIntroMessage() noexcept { showIntroMessage = false; }
	void RenderIntroMessage();

	int hotkey = VK_END;
	bool capturingHotkey = false;

	HWND hwnd = nullptr;
	WNDPROC previousWndProc = nullptr;

private:
	Overlay() = default;

	[[nodiscard]] const char* GetHotkeyName() const noexcept;

	bool initialized = false;
	bool visible = false;
	bool sharedContext = false;
	float dpiScale = 1.0f;
	float uiScaleOverride = 1.0f;

	winrt::com_ptr<ID3D12DescriptorHeap> srvHeap;
	winrt::com_ptr<ID3D12DescriptorHeap> rtvHeap;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	ImGuiContext* imguiContext = nullptr;

	ShaderDumpStats shaderDumpStats;

	uint64_t initTimestamp = 0;
	bool showIntroEnabled = true;
	bool showIntroMessage = false;

	uint64_t lastToggleTimestamp = 0;
	bool hotkeyWasDown = false;
	static constexpr uint64_t kDebounceMs = 200;

	static constexpr uint64_t kIntroMessageDurationMs = 30000;
};
