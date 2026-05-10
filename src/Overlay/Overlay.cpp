#include "Overlay/Overlay.h"
#include "Overlay/OverlaySettings.h"
#include "Overlay/OverlayShaderDump.h"
#include "Overlay/OverlayWndProc.h"

#include "HDR.h"
#include "Upscaler.h"

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
	const std::filesystem::path kOverlaySettingsPath{ "Data\\F4SE\\Plugins\\Upscaler\\Upscaler.ini" };

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

	void LoadOverlaySettings(bool& a_showIntroEnabled, int& a_hotkey)
	{
		std::error_code ec;
		if (!std::filesystem::exists(kOverlaySettingsPath, ec)) {
			return;
		}

		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(kOverlaySettingsPath.string().c_str()) < 0) {
			logger::warn("[Overlay] Failed to load overlay settings from {}", kOverlaySettingsPath.string());
			return;
		}

		a_showIntroEnabled = ini.GetBoolValue(
			kSettingsSection,
			"showIntro",
			ini.GetBoolValue(kSettingsSection, "bShowIntro", a_showIntroEnabled));

		const auto loadedHotkey = static_cast<int>(ini.GetLongValue(
			kSettingsSection,
			"iOverlayHotkey",
			ini.GetLongValue(kSettingsSection, "iHotkey", a_hotkey)));
		if (loadedHotkey > 0 && loadedHotkey <= 0xFE) {
			a_hotkey = loadedHotkey;
		} else {
			logger::warn("[Overlay] iOverlayHotkey={} is invalid, using VK_END", loadedHotkey);
			a_hotkey = VK_END;
		}
			auto* self = Overlay::GetSingleton();
			if (self) {
				float scale = static_cast<float>(ini.GetDoubleValue(
					kSettingsSection, "fUIScale", 1.0));
				if (scale < 0.5f) scale = 0.5f;
				if (scale > 4.0f) scale = 4.0f;
				self->SetUIScaleOverride(scale);
			}
	}


		void WriteINIFile(const std::filesystem::path& a_path, std::initializer_list<std::pair<const char*, const char*>> a_pairs)
	{
		std::error_code ec;
		std::filesystem::create_directories(a_path.parent_path(), ec);
		if (ec) {
			logger::warn("[Overlay] Failed to create directory {}: {}", a_path.parent_path().string(), ec.message());
			return;
		}

		CSimpleIniA ini;
		ini.SetUnicode();
		if (std::filesystem::exists(a_path, ec)) {
			ini.LoadFile(a_path.string().c_str());
		}

		for (const auto& [key, value] : a_pairs) {
			ini.SetValue("Settings", key, value);
		}

		if (ini.SaveFile(a_path.string().c_str()) < 0) {
			logger::warn("[Overlay] Failed to save {}", a_path.string());
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
		return true;
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

	HDC hdc = GetDC(hwnd);
	float dpi = static_cast<float>(GetDeviceCaps(hdc, LOGPIXELSX));
	ReleaseDC(hwnd, hdc);
	dpiScale = dpi / 96.0f;
	if (dpiScale < 1.0f) dpiScale = 1.0f;
	io.FontGlobalScale = dpiScale;
	logger::info("[Overlay] DPI scale: {:.2f} (dpi={})", dpiScale, static_cast<int>(dpi));

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
	LoadOverlaySettings(showIntroEnabled, hotkey);

	g_overlay = this;
	previousWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
	if (!previousWndProc) {
		logger::warn("[Overlay] SetWindowLongPtrW failed to capture previous WndProc");
	}

	initTimestamp = GetTickCount64();
	showIntroMessage = showIntroEnabled;

	sharedContext = true;
	initialized = true;
	logger::info(
		"[Overlay] Initialized (format={}, hwnd=0x{:X}, hotkey={}, intro={})",
		static_cast<std::uint32_t>(format),
		reinterpret_cast<uintptr_t>(hwnd),
		GetHotkeyName(),
		showIntroEnabled);
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
	sharedContext = false;
	initialized = false;

	logger::info("[Overlay] Shutdown complete");
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

const char* Overlay::GetHotkeyName() const noexcept
{
	if (hotkey == VK_END)
		return "END";

	// MAPVK_VK_TO_VSC_EX (3) returns scan code in low byte + extended-key bit in high byte
	const UINT scanCodeEx = MapVirtualKeyW(static_cast<UINT>(hotkey), MAPVK_VK_TO_VSC_EX);
	if (scanCodeEx == 0)
		return "(unknown key)";

	thread_local char nameBuf[32];
	nameBuf[0] = '\0';
	LONG lParam = static_cast<LONG>(scanCodeEx & 0xFF) << 16;
	if (scanCodeEx & 0x100)
		lParam |= 0x01000000;
	if (GetKeyNameTextA(lParam, nameBuf, static_cast<int>(sizeof(nameBuf))) != 0 && nameBuf[0] != '\0')
		return nameBuf;

	return "(unknown key)";
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

void Overlay::Render(ID3D12GraphicsCommandList4* a_commandList, ID3D12Resource* a_backBuffer, DXGI_FORMAT a_swapChainFormat)
{
	if (!initialized || !imguiContext) {
		return;
	}

	static bool loggedFirstRender = false;
	if (!loggedFirstRender) {
		loggedFirstRender = true;
		logger::info("[Overlay] First Render() call - visible={} showIntro={} fmt={}", visible, IsShowingIntroMessage(), static_cast<std::uint32_t>(a_swapChainFormat));
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
		OverlaySettings::RenderPanel();
		OverlayShaderDump::RenderPanel(shaderDumpStats);
	}
	if (showIntro) {
		RenderIntroMessage();
	}

	ImGui::Render();

	ID3D12DescriptorHeap* heaps[] = { srvHeap.get() };
	a_commandList->SetDescriptorHeaps(1, heaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), a_commandList);
}
