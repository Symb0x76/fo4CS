#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include <OverlayAPI.h>

struct ImGuiContext;

struct RegisteredPanel {
	std::string name;
	int category;
	OverlayPanelCallbacks callbacks;
	int id;
};

class Overlay
{
public:
	static Overlay* GetSingleton();

	bool Initialize(ID3D12Device* a_device, ID3D12CommandQueue* a_commandQueue, IDXGISwapChain4* a_swapChain, DXGI_FORMAT a_swapChainFormat, HWND a_hwnd);
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const noexcept { return initialized; }
	[[nodiscard]] ImGuiContext* GetImGuiContext() const noexcept { return imguiContext; }
	[[nodiscard]] bool IsVisible() const noexcept { return visible; }
	void SetVisible(bool a_visible) noexcept { visible = a_visible; }
	void ToggleVisible() noexcept;
	[[nodiscard]] bool ShouldRender() const noexcept { return visible || IsShowingIntroMessage(); }

	[[nodiscard]] float GetUIScale() const noexcept { return dpiScale * uiScaleOverride; }
	[[nodiscard]] float GetUIScaleOverride() const noexcept { return uiScaleOverride; }
	void SetUIScaleOverride(float a_scale) noexcept { uiScaleOverride = a_scale; }

	void Render(ID3D12GraphicsCommandList4* a_commandList, ID3D12Resource* a_backBuffer, DXGI_FORMAT a_swapChainFormat);

	[[nodiscard]] HWND GetHwnd() const noexcept { return hwnd; }
	[[nodiscard]] WNDPROC GetPreviousWndProc() const noexcept { return previousWndProc; }

	// Merged overlay self-settings (rendered inline in the unified panel)
	void DrawOverlaySettings();
	void SaveOverlaySelfSettings();

	// Panel registry (thread-safe, may be called before Initialize)
	int RegisterPanel(const char* a_name, int a_category, const OverlayPanelCallbacks* a_callbacks);
	void UnregisterPanel(int a_panelId);

	// Stats (thread-safe key-value store for debug panels)
	void UpdateStats(const char* a_key, const char* a_value);
	[[nodiscard]] const std::unordered_map<std::string, std::string>& GetStats() const { return stats; }

	// Hotkey
	[[nodiscard]] int GetHotkey() const noexcept { return hotkey; }
	void StartCapturingHotkey() noexcept { capturingHotkey = true; }
	[[nodiscard]] bool IsCapturingHotkey() const noexcept { return capturingHotkey; }
	void SetHotkey(int a_key) noexcept { hotkey = a_key; capturingHotkey = false; hotkeyWasDown = true; }
	[[nodiscard]] bool HandleKeyDown(int a_key) noexcept;
	void PollHotkeyState() noexcept;

	// Intro notification
	[[nodiscard]] bool IsIntroEnabled() const noexcept { return showIntroEnabled; }
	void SetIntroEnabled(bool a_enabled) noexcept;
	[[nodiscard]] bool IsShowingIntroMessage() const noexcept;
	void DismissIntroMessage() noexcept { showIntroMessage = false; }
	void RenderIntroMessage();

	// Static callbacks for DX12SwapChain
	static void OnSwapChainCreated(ID3D12Device* a_device, ID3D12CommandQueue* a_queue, IDXGISwapChain4* a_swapChain, DXGI_FORMAT a_format, HWND a_hwnd);
	static void OnPresent(ID3D12GraphicsCommandList4* a_cmdList, ID3D12Resource* a_backBuffer, DXGI_FORMAT a_format);
	static void OnPollHotkey();

	int hotkey = VK_END;
	bool capturingHotkey = false;

	HWND hwnd = nullptr;
	WNDPROC previousWndProc = nullptr;

private:
	Overlay() = default;

	[[nodiscard]] const char* GetHotkeyName() const noexcept;

	bool initialized = false;
	bool visible = false;
	float dpiScale = 1.0f;
	float uiScaleOverride = 1.0f;

	winrt::com_ptr<ID3D12DescriptorHeap> srvHeap;
	winrt::com_ptr<ID3D12DescriptorHeap> rtvHeap;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	ImGuiContext* imguiContext = nullptr;

	uint64_t initTimestamp = 0;
	bool showIntroEnabled = true;
	bool showIntroMessage = false;

	uint64_t lastToggleTimestamp = 0;
	bool hotkeyWasDown = false;
	static constexpr uint64_t kDebounceMs = 200;

	static constexpr uint64_t kIntroMessageDurationMs = 30000;

	// Panel registry
	std::vector<RegisteredPanel> panels;
	int nextPanelId = 1;
	std::mutex panelMutex;

	// Stats
	std::unordered_map<std::string, std::string> stats;
	std::mutex statsMutex;
};
