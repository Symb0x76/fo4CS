#define FO4CS_OVERLAY_API_EXPORTS
#include "Overlay/Overlay.h"
#include "Overlay/OverlayWndProc.h"

#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

#include <SimpleIni.h>

#include <directx/d3dx12.h>
#include <filesystem>

namespace
{
	winrt::com_ptr<ID3D12DescriptorHeap> g_srvHeap;
	constexpr std::size_t kSrvDescriptorCount = 1;
	std::size_t g_allocatedDescriptors = 0;
	constexpr const char* kSettingsSection = "Settings";
	const std::filesystem::path kOverlaySelfSettingsPath{ "Data\\F4SE\\Plugins\\Overlay\\Overlay.ini" };

	void AllocateImGuiDescriptor(ImGui_ImplDX12_InitInfo* a_initInfo, D3D12_CPU_DESCRIPTOR_HANDLE* a_outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* a_outGpu)
	{
		if (g_allocatedDescriptors >= kSrvDescriptorCount) {
			*a_outCpu = {};
			*a_outGpu = {};
			return;
		}

		const auto heapStart = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
		const auto gpuStart = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
		const auto increment = a_initInfo->Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const auto offset = g_allocatedDescriptors * increment;

		a_outCpu->ptr = heapStart.ptr + offset;
		a_outGpu->ptr = gpuStart.ptr + offset;
		++g_allocatedDescriptors;
	}

	void FreeImGuiDescriptor(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)
	{
		if (g_allocatedDescriptors > 0) {
			--g_allocatedDescriptors;
		}
	}

	void LoadOverlaySelfSettings(bool& a_showIntroEnabled, int& a_hotkey)
	{
		std::error_code ec;
		if (!std::filesystem::exists(kOverlaySelfSettingsPath, ec)) {
			return;
		}

		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(kOverlaySelfSettingsPath.string().c_str()) < 0) {
			return;
		}

		a_showIntroEnabled = ini.GetBoolValue(kSettingsSection, "showIntro",
			ini.GetBoolValue(kSettingsSection, "bShowIntro", a_showIntroEnabled));

		const auto loadedHotkey = static_cast<int>(ini.GetLongValue(kSettingsSection, "iOverlayHotkey",
			ini.GetLongValue(kSettingsSection, "iHotkey", a_hotkey)));
		if (loadedHotkey > 0 && loadedHotkey <= 0xFE) {
			a_hotkey = loadedHotkey;
		} else {
			logger::warn("[Overlay] iOverlayHotkey={} is invalid, using VK_END", loadedHotkey);
			a_hotkey = VK_END;
		}

		auto* self = Overlay::GetSingleton();
		if (self) {
			float scale = static_cast<float>(ini.GetDoubleValue(kSettingsSection, "fUIScale", 1.0));
			if (scale < 0.5f) scale = 0.5f;
			if (scale > 4.0f) scale = 4.0f;
			self->SetUIScaleOverride(scale);
		}
	}

}

Overlay* Overlay::GetSingleton()
{
	static Overlay singleton;
	return &singleton;
}

bool Overlay::Initialize(ID3D12Device* a_device, ID3D12CommandQueue* a_commandQueue, IDXGISwapChain4* /*a_swapChain*/, DXGI_FORMAT a_swapChainFormat, HWND a_hwnd)
{
	if (initialized) {
		if (format == a_swapChainFormat) {
			return true;
		}
		Shutdown();
	}

	hwnd = a_hwnd;
	if (!hwnd) {
		hwnd = GetForegroundWindow();
		if (hwnd) {
			logger::warn("[Overlay] Using foreground window (fallback)");
		} else {
			logger::error("[Overlay] Failed to locate game window");
			return false;
		}
	}

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.NumDescriptors = static_cast<UINT>(kSrvDescriptorCount);
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	if (FAILED(a_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(srvHeap.put())))) {
		logger::error("[Overlay] Failed to create ImGui descriptor heap");
		return false;
	}
	g_srvHeap = srvHeap;

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(a_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.put())))) {
		logger::error("[Overlay] Failed to create RTV descriptor heap");
		return false;
	}

	IMGUI_CHECKVERSION();
	imguiContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(imguiContext);
	ImGui::StyleColorsDark();

	auto& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
	io.IniFilename = nullptr;

	HDC hdcTmp = GetDC(hwnd);
	dpiScale = static_cast<float>(GetDeviceCaps(hdcTmp, LOGPIXELSX)) / 96.0f;
	ReleaseDC(hwnd, hdcTmp);
	if (dpiScale < 1.0f) dpiScale = 1.0f;
	io.FontGlobalScale = dpiScale;
	logger::info("[Overlay] DPI scale: {:.2f}", dpiScale);

	if (!ImGui_ImplWin32_Init(hwnd)) {
		logger::error("[Overlay] ImGui_ImplWin32_Init failed");
		return false;
	}

	ImGui_ImplDX12_InitInfo initInfo{};
	initInfo.Device = a_device;
	initInfo.CommandQueue = a_commandQueue;
	initInfo.NumFramesInFlight = 2;
	initInfo.RTVFormat = a_swapChainFormat;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	initInfo.SrvDescriptorHeap = srvHeap.get();
	initInfo.SrvDescriptorAllocFn = AllocateImGuiDescriptor;
	initInfo.SrvDescriptorFreeFn = FreeImGuiDescriptor;

	if (!ImGui_ImplDX12_Init(&initInfo)) {
		logger::error("[Overlay] ImGui_ImplDX12_Init failed");
		return false;
	}

	format = a_swapChainFormat;
	LoadOverlaySelfSettings(showIntroEnabled, hotkey);

	g_overlay = this;
	previousWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
	if (!previousWndProc) {
		logger::warn("[Overlay] SetWindowLongPtrW failed to capture previous WndProc");
	}

	initTimestamp = GetTickCount64();
	showIntroMessage = showIntroEnabled;

	initialized = true;
	logger::info("[Overlay] Initialized (format={}, hwnd=0x{:X}, hotkey={}, intro={}, panels={})",
		static_cast<std::uint32_t>(format),
		reinterpret_cast<uintptr_t>(hwnd),
		GetHotkeyName(),
		showIntroEnabled,
		panels.size());
	return true;
}

void Overlay::Shutdown()
{
	if (!initialized) {
		return;
	}

	if (previousWndProc) {
		SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previousWndProc));
		previousWndProc = nullptr;
	}
	g_overlay = nullptr;

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();

	if (imguiContext) {
		ImGui::DestroyContext(imguiContext);
		imguiContext = nullptr;
	}

	srvHeap = nullptr;
	rtvHeap = nullptr;
	g_srvHeap = nullptr;
	g_allocatedDescriptors = 0;
	visible = false;
	capturingHotkey = false;
	showIntroMessage = false;
	showIntroEnabled = true;
	initTimestamp = 0;
	lastToggleTimestamp = 0;
	hotkeyWasDown = false;
	initialized = false;

	logger::info("[Overlay] Shutdown complete");
}

int Overlay::RegisterPanel(const char* a_name, int a_category, const OverlayPanelCallbacks* a_callbacks)
{
	std::lock_guard lock(panelMutex);

	for (const auto& panel : panels) {
		if (panel.name == a_name) {
			logger::warn("[Overlay] Panel '{}' already registered", a_name);
			return -1;
		}
	}

	const int id = nextPanelId++;
	panels.push_back({ a_name, a_category, *a_callbacks, id });
	logger::info("[Overlay] Registered panel '{}' (id={}, category={})", a_name, id, a_category);
	return id;
}

void Overlay::UnregisterPanel(int a_panelId)
{
	std::lock_guard lock(panelMutex);
	std::erase_if(panels, [a_panelId](const RegisteredPanel& p) { return p.id == a_panelId; });
}

void Overlay::UpdateStats(const char* a_key, const char* a_value)
{
	std::lock_guard lock(statsMutex);
	stats[a_key] = a_value;
}

void Overlay::ToggleVisible() noexcept
{
	const auto now = GetTickCount64();
	if (now - lastToggleTimestamp < kDebounceMs)
		return;
	lastToggleTimestamp = now;

	visible = !visible;
	if (visible) {
		showIntroMessage = false;
	}
	logger::debug("[Overlay] Toggled visible={}", visible);
}

bool Overlay::HandleKeyDown(int a_key) noexcept
{
	if (capturingHotkey) {
		SetHotkey(a_key);
		return true;
	}

	if (a_key == hotkey) {
		if (!hotkeyWasDown) {
			ToggleVisible();
		}
		hotkeyWasDown = true;
		return true;
	}

	return false;
}

void Overlay::PollHotkeyState() noexcept
{
	if (!initialized) {
		return;
	}

	const bool hotkeyIsDown = (GetAsyncKeyState(hotkey) & 0x8000) != 0;
	if (!hotkeyIsDown) {
		hotkeyWasDown = false;
		return;
	}

	if (!hotkeyWasDown && !capturingHotkey) {
		ToggleVisible();
	}
	hotkeyWasDown = true;
}

void Overlay::SetIntroEnabled(bool a_enabled) noexcept
{
	showIntroEnabled = a_enabled;
	if (!a_enabled) {
		showIntroMessage = false;
		return;
	}

	initTimestamp = GetTickCount64();
	showIntroMessage = true;
}

bool Overlay::IsShowingIntroMessage() const noexcept
{
	if (!showIntroEnabled || !showIntroMessage)
		return false;
	if (!initialized)
		return false;
	return (GetTickCount64() - initTimestamp) < kIntroMessageDurationMs;
}

static const char* VKCodeToName(int a_vkCode)
	{
	switch (a_vkCode) {
	case VK_END:     return "End";
	case VK_HOME:    return "Home";
	case VK_INSERT:  return "Insert";
	case VK_DELETE:  return "Delete";
	case VK_PRIOR:   return "Page Up";
	case VK_NEXT:    return "Page Down";
	case VK_LEFT:    return "Left";
	case VK_UP:      return "Up";
	case VK_RIGHT:   return "Right";
	case VK_DOWN:    return "Down";
	case VK_SPACE:   return "Space";
	case VK_RETURN:  return "Enter";
	case VK_TAB:     return "Tab";
	case VK_BACK:    return "Backspace";
	case VK_ESCAPE:  return "Escape";
	case VK_SNAPSHOT: return "Print Screen";
	case VK_PAUSE:   return "Pause";
	case VK_CAPITAL: return "Caps Lock";
	case VK_SCROLL:  return "Scroll Lock";
	case VK_NUMLOCK: return "Num Lock";
	case VK_LSHIFT:  return "Left Shift";
	case VK_RSHIFT:  return "Right Shift";
	case VK_LCONTROL: return "Left Ctrl";
	case VK_RCONTROL: return "Right Ctrl";
	case VK_LMENU:   return "Left Alt";
	case VK_RMENU:   return "Right Alt";
	case VK_LWIN:    return "Left Win";
	case VK_RWIN:    return "Right Win";
	case VK_APPS:    return "Menu";
	case VK_NUMPAD0: return "Numpad 0";
	case VK_NUMPAD1: return "Numpad 1";
	case VK_NUMPAD2: return "Numpad 2";
	case VK_NUMPAD3: return "Numpad 3";
	case VK_NUMPAD4: return "Numpad 4";
	case VK_NUMPAD5: return "Numpad 5";
	case VK_NUMPAD6: return "Numpad 6";
	case VK_NUMPAD7: return "Numpad 7";
	case VK_NUMPAD8: return "Numpad 8";
	case VK_NUMPAD9: return "Numpad 9";
	case VK_MULTIPLY:  return "Numpad *";
	case VK_ADD:       return "Numpad +";
	case VK_SUBTRACT:  return "Numpad -";
	case VK_DECIMAL:   return "Numpad .";
	case VK_DIVIDE:    return "Numpad /";
	case VK_CLEAR:     return "Clear";
	}
	if (a_vkCode >= VK_F1 && a_vkCode <= VK_F24) {
		thread_local char fBuf[8];
		snprintf(fBuf, sizeof(fBuf), "F%d", a_vkCode - VK_F1 + 1);
		return fBuf;
	}
	if (a_vkCode >= '0' && a_vkCode <= '9') {
		thread_local char dBuf[2];
		dBuf[0] = static_cast<char>(a_vkCode);
		dBuf[1] = '\0';
		return dBuf;
	}
	if (a_vkCode >= 'A' && a_vkCode <= 'Z') {
		thread_local char cBuf[2];
		cBuf[0] = static_cast<char>(a_vkCode);
		cBuf[1] = '\0';
		return cBuf;
	}

	const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(a_vkCode), MAPVK_VK_TO_VSC);
	if (scanCode == 0)
		return "(unknown key)";

	thread_local char nameBuf[32];
	LONG lParam = static_cast<LONG>(scanCode) << 16;
	if (GetKeyNameTextA(lParam, nameBuf, sizeof(nameBuf)) != 0 && nameBuf[0] != '\0')
		return nameBuf;

	return "(unknown key)";
	}

	const char* Overlay::GetHotkeyName() const noexcept
	{
		return VKCodeToName(hotkey);
	}

void Overlay::RenderIntroMessage()
{
	const float uiScale = GetUIScale();
	const float padding = 20.0f * uiScale;

	ImGui::SetNextWindowBgAlpha(0.65f);
	ImGui::SetNextWindowPos(ImVec2(padding, padding), ImGuiCond_Always);

	constexpr int flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_AlwaysAutoResize;

	if (ImGui::Begin("##NuclearGFXIntro", nullptr, flags)) {
		ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "NuclearGFX loaded.");
		ImGui::SameLine();
		ImGui::Text("Press %s to open in-game menu.", GetHotkeyName());
	}
	ImGui::End();
}

void Overlay::DrawOverlaySettings()
{
	// Hotkey
	int currentHotkey = GetHotkey();
	char hotkeyName[32];
	if (IsCapturingHotkey()) {
		snprintf(hotkeyName, sizeof(hotkeyName), "Press any key...");
	} else {
		snprintf(hotkeyName, sizeof(hotkeyName), "%s", VKCodeToName(currentHotkey));
	}
	ImGui::Text("Toggle Hotkey:");
	ImGui::SameLine();
	if (ImGui::Button(hotkeyName)) {
		StartCapturingHotkey();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(click then press key)");

	// Intro
	bool showIntro = IsIntroEnabled();
	if (ImGui::Checkbox("Show Intro Hint", &showIntro)) {
		SetIntroEnabled(showIntro);
	}

	// UI Scale
	static const float kUIScales[] = { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 4.0f };
	static const char* kUIScaleNames[] = { "0.75x", "1.0x (auto)", "1.25x", "1.5x", "1.75x", "2.0x", "2.5x", "3.0x", "4.0x" };
	float curScale = GetUIScaleOverride();
	int scaleIdx = 1;
	float best = 999.0f;
	for (int i = 0; i < IM_ARRAYSIZE(kUIScales); ++i) {
		float d = (curScale - kUIScales[i]) * (curScale - kUIScales[i]);
		if (d < best) { best = d; scaleIdx = i; }
	}
	if (ImGui::Combo("UI Scale", &scaleIdx, kUIScaleNames, IM_ARRAYSIZE(kUIScaleNames)))
		SetUIScaleOverride(kUIScales[scaleIdx]);

	ImGui::Separator();
	ImGui::TextDisabled("Press %s to toggle overlay", GetHotkeyName());
}

void Overlay::SaveOverlaySelfSettings()
{
	std::error_code ec;
	std::filesystem::create_directories(kOverlaySelfSettingsPath.parent_path(), ec);

	CSimpleIniA ini;
	ini.SetUnicode();
	if (std::filesystem::exists(kOverlaySelfSettingsPath, ec)) {
		ini.LoadFile(kOverlaySelfSettingsPath.string().c_str());
	}

	ini.SetValue(kSettingsSection, "showIntro", IsIntroEnabled() ? "true" : "false");
	ini.SetValue(kSettingsSection, "iOverlayHotkey", std::to_string(GetHotkey()).c_str());
	ini.SetValue(kSettingsSection, "fUIScale", std::to_string(GetUIScaleOverride()).c_str());

	if (ini.SaveFile(kOverlaySelfSettingsPath.string().c_str()) < 0) {
		logger::warn("[Overlay] Failed to save Overlay.ini");
	}
}

void Overlay::Render(ID3D12GraphicsCommandList4* a_commandList, ID3D12Resource* a_backBuffer, DXGI_FORMAT a_swapChainFormat)
{
	if (!initialized || !imguiContext) {
		return;
	}

	static bool loggedFirstRender = false;
	if (!loggedFirstRender) {
		loggedFirstRender = true;
		logger::info("[Overlay] First Render() call - visible={} showIntro={} panels={}", visible, IsShowingIntroMessage(), panels.size());
	}

	const bool showIntro = IsShowingIntroMessage();
	if (!visible && !showIntro) {
		return;
	}

	ImGui::SetCurrentContext(imguiContext);

	if (format != a_swapChainFormat) {
		logger::debug("[Overlay] Swap chain format changed ({} -> {}), ignoring", static_cast<std::uint32_t>(format), static_cast<std::uint32_t>(a_swapChainFormat));
	}

	if (!a_backBuffer || !rtvHeap) {
		logger::warn("[Overlay] Missing back buffer or RTV heap; skipping overlay render");
		return;
	}

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = a_swapChainFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	winrt::com_ptr<ID3D12Device> device;
	DX::ThrowIfFailed(a_backBuffer->GetDevice(IID_PPV_ARGS(device.put())));
	device->CreateRenderTargetView(a_backBuffer, &rtvDesc, rtv);
	a_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	ImGui::GetIO().MouseDrawCursor = visible;
	ImGui::GetIO().FontGlobalScale = GetUIScale();

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	if (visible) {
		// Render registered panels (thread-safe copy)
		{
			std::lock_guard lock(panelMutex);
			for (auto& panel : panels) {
				if (panel.callbacks.render) {
					ImGui::SetNextWindowSize(ImVec2(400.0f * GetUIScale(), 300.0f * GetUIScale()), ImGuiCond_FirstUseEver);
					char title[128];
					snprintf(title, sizeof(title), "%s", panel.name.c_str());
					if (ImGui::Begin(title, nullptr)) {
						panel.callbacks.render(panel.callbacks.userData);
					}
					ImGui::End();
				}
			}
		}
	}
	if (showIntro) {
		RenderIntroMessage();
	}

	ImGui::Render();

	ID3D12DescriptorHeap* heaps[] = { srvHeap.get() };
	a_commandList->SetDescriptorHeaps(1, heaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), a_commandList);
}

// Static callbacks for DX12SwapChain registration
void Overlay::OnSwapChainCreated(ID3D12Device* a_device, ID3D12CommandQueue* a_queue, IDXGISwapChain4* a_swapChain, DXGI_FORMAT a_format, HWND a_hwnd)
{
	auto* overlay = GetSingleton();
	if (!overlay->Initialize(a_device, a_queue, a_swapChain, a_format, a_hwnd)) {
		logger::warn("[Overlay] Initialization via DX12SwapChain callback failed");
	}
}

void Overlay::OnPresent(ID3D12GraphicsCommandList4* a_cmdList, ID3D12Resource* a_backBuffer, DXGI_FORMAT a_format)
{
	auto* overlay = GetSingleton();
	if (overlay->ShouldRender()) {
		overlay->Render(a_cmdList, a_backBuffer, a_format);
	}
}

void Overlay::OnPollHotkey()
{
	GetSingleton()->PollHotkeyState();
}

// --- C-API exports ---

extern "C" {

FO4CS_OVERLAY_API int Overlay_RegisterPanel(const char* a_name, int a_category, const OverlayPanelCallbacks* a_callbacks)
{
	return Overlay::GetSingleton()->RegisterPanel(a_name, a_category, a_callbacks);
}

FO4CS_OVERLAY_API void Overlay_UnregisterPanel(int a_panelId)
{
	Overlay::GetSingleton()->UnregisterPanel(a_panelId);
}

FO4CS_OVERLAY_API int Overlay_GetHotkey(void)
{
	return Overlay::GetSingleton()->GetHotkey();
}

FO4CS_OVERLAY_API void Overlay_SetHotkey(int a_vkCode)
{
	Overlay::GetSingleton()->SetHotkey(a_vkCode);
}

FO4CS_OVERLAY_API float Overlay_GetUIScale(void)
{
	return Overlay::GetSingleton()->GetUIScaleOverride();
}

FO4CS_OVERLAY_API void Overlay_SetUIScale(float a_scale)
{
	Overlay::GetSingleton()->SetUIScaleOverride(a_scale);
}

FO4CS_OVERLAY_API int Overlay_GetVisible(void)
{
	return Overlay::GetSingleton()->IsVisible() ? 1 : 0;
}

FO4CS_OVERLAY_API void Overlay_SetVisible(int a_visible)
{
	Overlay::GetSingleton()->SetVisible(a_visible != 0);
}

FO4CS_OVERLAY_API void Overlay_UpdateStats(const char* a_statKey, const char* a_statValue)
{
	Overlay::GetSingleton()->UpdateStats(a_statKey, a_statValue);
}

// Callbacks resolved by DX12SwapChain via GetProcAddress.
// These are called from whichever DLL creates the D3D12 swap chain.
FO4CS_OVERLAY_API void Overlay_OnSwapChainCreated(ID3D12Device* a_device, ID3D12CommandQueue* a_queue, IDXGISwapChain4* a_swapChain, DXGI_FORMAT a_format, HWND a_hwnd)
{
	Overlay::OnSwapChainCreated(a_device, a_queue, a_swapChain, a_format, a_hwnd);
}

FO4CS_OVERLAY_API void Overlay_OnPresent(ID3D12GraphicsCommandList4* a_cmdList, ID3D12Resource* a_backBuffer, DXGI_FORMAT a_format)
{
	Overlay::OnPresent(a_cmdList, a_backBuffer, a_format);
}

FO4CS_OVERLAY_API void Overlay_OnPollHotkey(void)
{
	Overlay::OnPollHotkey();
}

} // extern "C"
