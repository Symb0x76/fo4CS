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

	IMGUI_CHECKVERSION();
	imguiContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(imguiContext);
	ImGui::StyleColorsDark();

	auto& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr;

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

	g_overlay = this;
	previousWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
	if (!previousWndProc) {
		logger::warn("[Overlay] SetWindowLongPtrW failed to capture previous WndProc");
	}

	sharedContext = true;
	initialized = true;
	logger::info("[Overlay] Initialized (format={})", static_cast<std::uint32_t>(format));
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
	g_srvHeap = nullptr;
	g_allocatedDescriptors = 0;
	sharedContext = false;
	initialized = false;

	logger::info("[Overlay] Shutdown complete");
}

void Overlay::Render(ID3D12GraphicsCommandList4* a_commandList, ID3D12Resource* /*a_backBuffer*/, DXGI_FORMAT a_swapChainFormat)
{
	if (!initialized || !imguiContext) {
		return;
	}

	ImGui::SetCurrentContext(imguiContext);

	if (format != a_swapChainFormat) {
		logger::debug("[Overlay] Swap chain format changed ({} -> {}), ignoring", static_cast<std::uint32_t>(format), static_cast<std::uint32_t>(a_swapChainFormat));
	}

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	OverlaySettings::RenderPanel();
	OverlayShaderDump::RenderPanel(shaderDumpStats);

	ImGui::Render();

	ID3D12DescriptorHeap* heaps[] = { srvHeap.get() };
	a_commandList->SetDescriptorHeaps(1, heaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), a_commandList);
}
