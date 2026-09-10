#include "Render/DX12SwapChain.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <dx12/ffx_api_dx12.hpp>
#include <dxgi1_6.h>
#include <optional>
#include <string_view>
#include <vector>

#include <directx/d3dx12.h>

#include "Core/DebugSwitches.h"
#include "Upscaling/FidelityFX.h"
#include "Render/PresentationMenuPolicy.h"
#include "Upscaling/Streamline.h"
#include "Upscaling/Upscaler.h"
#include "Render/DX12SwapChainInternal.h"

extern bool enbLoaded;

using fo4cs::render::ResolveOverlayCallbacks;
using fo4cs::render::s_overlayInitCb;

// Overlay callbacks resolved from Overlay.dll at init time.
// Using file-scope statics avoids the OBJECT-library multiple-singleton problem:
// whichever DLL creates the DX12SwapChain resolves these once.
namespace fo4cs::render
{
OverlayInitCallback s_overlayInitCb = nullptr;
OverlayPresentCallback s_overlayPresentCb = nullptr;
OverlayPollCallback s_overlayPollCb = nullptr;
bool s_overlayCallbacksResolved = false;
bool s_overlayCallbacksMissingLogged = false;

bool ResolveOverlayCallbacks()
{
    if (s_overlayCallbacksResolved)
    {
        return true;
    }

    auto tryResolve = [](HMODULE a_module, const char *a_source) -> bool {
        if (!a_module)
        {
            return false;
        }

        auto initCb = reinterpret_cast<OverlayInitCallback>(GetProcAddress(a_module, "Overlay_OnSwapChainCreated"));
        auto presentCb = reinterpret_cast<OverlayPresentCallback>(GetProcAddress(a_module, "Overlay_OnPresent"));
        auto pollCb = reinterpret_cast<OverlayPollCallback>(GetProcAddress(a_module, "Overlay_OnPollHotkey"));
        if (!initCb || !presentCb || !pollCb)
        {
            return false;
        }

        s_overlayInitCb = initCb;
        s_overlayPresentCb = presentCb;
        s_overlayPollCb = pollCb;
        s_overlayCallbacksResolved = true;
        logger::info("[DX12SwapChain] Overlay callbacks resolved from {}", a_source);
        return true;
    };

    HMODULE currentModule = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&s_overlayCallbacksResolved), &currentModule) &&
        tryResolve(currentModule, "current module"))
    {
        return true;
    }

    if (tryResolve(GetModuleHandleW(L"NuclearGFX.dll"), "NuclearGFX.dll"))
    {
        return true;
    }

    if (tryResolve(GetModuleHandleW(L"Overlay.dll"), "Overlay.dll"))
    {
        return true;
    }

    if (!s_overlayCallbacksMissingLogged)
    {
        s_overlayCallbacksMissingLogged = true;
        logger::info("[DX12SwapChain] Overlay callbacks not found; retrying until Overlay is loaded");
    }
    return false;
}
} // namespace

namespace fo4cs::render
{
namespace
{
bool s_d3d12DebugLayerEnabled = false;
winrt::com_ptr<ID3D12InfoQueue> s_d3d12InfoQueue;
} // namespace

void EnableD3D12Diagnostics()
{
    if (!CommunityShaders::DebugSwitches::ReadSwitchEnabled("FO4CS_D3D12_DEBUG_LAYER"))
    {
        return;
    }

    winrt::com_ptr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.put()))) || !debug)
    {
        logger::warn("[DX12SwapChain] FO4CS_D3D12_DEBUG_LAYER is set but D3D12GetDebugInterface failed; "
                     "enable the Windows 'Graphics Tools' optional feature to use it");
        return;
    }

    debug->EnableDebugLayer();
    s_d3d12DebugLayerEnabled = true;
    logger::info("[DX12SwapChain] D3D12 debug layer enabled; validation messages will be logged as [D3D12]");
}

void ConfigureD3D12InfoQueue(ID3D12Device *a_device)
{
    if (!s_d3d12DebugLayerEnabled || !a_device)
    {
        return;
    }

    if (FAILED(a_device->QueryInterface(IID_PPV_ARGS(s_d3d12InfoQueue.put()))) || !s_d3d12InfoQueue)
    {
        logger::warn("[DX12SwapChain] D3D12 debug layer is on but ID3D12InfoQueue is unavailable");
        s_d3d12InfoQueue = nullptr;
        return;
    }

    // Keep every message; the queue is drained into the log each Present. Left
    // unbounded the ring buffer would silently drop the first offending message,
    // which is precisely the one worth having.
    s_d3d12InfoQueue->SetMuteDebugOutput(FALSE);
    s_d3d12InfoQueue->ClearStorageFilter();
    DX::ThrowIfFailed(s_d3d12InfoQueue->SetMessageCountLimit(0));
}

void DrainD3D12InfoQueue(const char *a_when)
{
    if (!s_d3d12InfoQueue)
    {
        return;
    }

    const auto stored = s_d3d12InfoQueue->GetNumStoredMessages();
    std::vector<std::byte> storage;
    for (UINT64 index = 0; index < stored; ++index)
    {
        SIZE_T length = 0;
        if (FAILED(s_d3d12InfoQueue->GetMessage(index, nullptr, &length)) || length == 0)
        {
            continue;
        }

        storage.resize(length);
        auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
        if (FAILED(s_d3d12InfoQueue->GetMessage(index, message, &length)))
        {
            continue;
        }

        // CORRUPTION(0) < ERROR(1) < WARNING(2) < INFO(3) < MESSAGE(4). Info and
        // below are pure noise at 4K.
        if (message->Severity > D3D12_MESSAGE_SEVERITY_WARNING)
        {
            continue;
        }

        const std::string_view description(
            message->pDescription, message->DescriptionByteLength > 0 ? message->DescriptionByteLength - 1 : 0);
        logger::critical("[D3D12] {} severity={} id={} {}", a_when, static_cast<int>(message->Severity),
                         static_cast<int>(message->ID), description);
    }

    s_d3d12InfoQueue->ClearStoredMessages();
}
} // namespace fo4cs::render

void DX12SwapChain::CreateD3D12Device(IDXGIAdapter *a_adapter)
{
    fo4cs::render::EnableD3D12Diagnostics();

    // Feature-level ladder rather than a bare 12_0 create.
    //
    // With FO4CS_D3D12_DEBUG_LAYER set, D3D12CreateDevice at 12_0 returns
    // E_INVALIDARG on this machine and the whole proxy falls back to plain D3D11 --
    // no upscaling, no frame generation. EnableDebugLayer() cannot be undone once
    // called, so there is no way to retry without it; the only recovery is to ask
    // for less. The game itself creates its D3D11 device at FEATURE_LEVEL_11_1, so
    // nothing here actually requires 12_0.
    //
    // 12_0 is still tried first, so a machine that accepts it behaves exactly as
    // before. Each attempt logs its HRESULT, which is the evidence needed to tell a
    // debug-layer rejection apart from a genuine capability gap.
    static constexpr D3D_FEATURE_LEVEL kDeviceFeatureLevels[]{
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    HRESULT deviceResult = E_FAIL;
    for (const auto featureLevel : kDeviceFeatureLevels)
    {
        deviceResult = D3D12CreateDevice(a_adapter, featureLevel, IID_PPV_ARGS(&d3d12Device));
        if (SUCCEEDED(deviceResult))
        {
            logger::info("[DX12SwapChain] D3D12 device created at feature level 0x{:X}",
                         static_cast<std::uint32_t>(featureLevel));
            break;
        }
        logger::warn("[DX12SwapChain] D3D12CreateDevice failed at feature level 0x{:X}: 0x{:08X}",
                     static_cast<std::uint32_t>(featureLevel), static_cast<std::uint32_t>(deviceResult));
    }
    DX::ThrowIfFailed(deviceResult);
    fo4cs::render::ConfigureD3D12InfoQueue(d3d12Device.get());
    if (ID3D12Device *upgradedDevice = d3d12Device.get();
        Streamline::GetSingleton()->UpgradeD3D12DeviceForDLSSG(&upgradedDevice) && upgradedDevice != d3d12Device.get())
    {
        d3d12Device.attach(upgradedDevice);
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.NodeMask = 0;

    DX::ThrowIfFailed(d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
    Streamline::GetSingleton()->LogD3D12CommandQueueProxyState(commandQueue.get());

    for (int i = 0; i < 2; i++)
    {
        DX::ThrowIfFailed(
            d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators[i])));
        DX::ThrowIfFailed(d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[i].get(),
                                                         nullptr, IID_PPV_ARGS(&commandLists[i])));
        commandLists[i]->Close();
    }
}

void DX12SwapChain::CreateSwapChain(IDXGIFactory4 *a_dxgiFactory, DXGI_SWAP_CHAIN_DESC a_swapChainDesc)
{
    swapChainDesc = {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Width = a_swapChainDesc.BufferDesc.Width;
    swapChainDesc.Height = a_swapChainDesc.BufferDesc.Height;
    swapChainDesc.Format = a_swapChainDesc.BufferDesc.Format;
    isHDR = (swapChainDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) ||
            (swapChainDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
    if (isHDR)
    {
        logger::info("[DX12SwapChain] HDR detected: fmt={} {}", static_cast<uint32_t>(swapChainDesc.Format),
                     swapChainDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM ? "HDR10" : "scRGB");
    }
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    BOOL allowTearing = FALSE;
    if (winrt::com_ptr<IDXGIFactory5> dxgiFactory5;
        SUCCEEDED(a_dxgiFactory->QueryInterface(IID_PPV_ARGS(dxgiFactory5.put()))))
    {
        DX::ThrowIfFailed(
            dxgiFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)));
    }

    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    if (allowTearing)
    {
        swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    auto upscaling = Upscaling::GetSingleton();
    auto fidelityFX = FidelityFX::GetSingleton();
    const bool useFidelityFXSwapChain = upscaling->UsesFSRFrameGeneration() && fidelityFX->module;
    IDXGIFactory4 *dxgiFactory = a_dxgiFactory;
    logger::info("[DX12SwapChain] Creating D3D12 proxy swap chain {}x{} fmt={} flags=0x{:X} backend={}",
                 swapChainDesc.Width, swapChainDesc.Height, static_cast<uint32_t>(swapChainDesc.Format),
                 swapChainDesc.Flags, useFidelityFXSwapChain ? "FidelityFX" : "native");

    const auto createNativeSwapChain = [&]() {
        winrt::com_ptr<IDXGISwapChain1> nativeSwapChain;
        DX::ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(commandQueue.get(), a_swapChainDesc.OutputWindow,
                                                              &swapChainDesc, nullptr, nullptr, nativeSwapChain.put()));
        DX::ThrowIfFailed(nativeSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain)));
    };

    if (useFidelityFXSwapChain)
    {
        ffx::CreateContextDescFrameGenerationSwapChainForHwndDX12 ffxSwapChainDesc{};

        ffxSwapChainDesc.desc = &swapChainDesc;
        ffxSwapChainDesc.dxgiFactory = a_dxgiFactory;
        ffxSwapChainDesc.fullscreenDesc = nullptr;
        ffxSwapChainDesc.gameQueue = commandQueue.get();
        ffxSwapChainDesc.hwnd = a_swapChainDesc.OutputWindow;
        ffxSwapChainDesc.swapchain = &swapChain;

        if (ffx::CreateContext(fidelityFX->swapChainContext, nullptr, ffxSwapChainDesc) != ffx::ReturnCode::Ok ||
            !swapChain)
        {
            logger::error("[FidelityFX] Failed to create swap chain context, using native D3D12 swap chain");
            swapChain = nullptr;
            fidelityFX->swapChainContext = nullptr;
            createNativeSwapChain();
        }
    }
    else
    {
        createNativeSwapChain();
    }

    DX::ThrowIfFailed(swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainBuffers[0])));
    DX::ThrowIfFailed(swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainBuffers[1])));

    frameIndex = swapChain->GetCurrentBackBufferIndex();
    logger::info("[DX12SwapChain] Swap chain ready (frameIndex={}, buffers={})", frameIndex, swapChainDesc.BufferCount);

    if (useFidelityFXSwapChain && fidelityFX->swapChainContext != nullptr)
        fidelityFX->SetupFrameGeneration();

    swapChainProxy = std::make_unique<DXGISwapChainProxy>(swapChain);

    ResolveOverlayCallbacks();
    if (auto initCb = s_overlayInitCb ? s_overlayInitCb : overlayInitCallback)
    {
        initCb(d3d12Device.get(), commandQueue.get(), swapChain, swapChainDesc.Format, a_swapChainDesc.OutputWindow);
    }
}

void DX12SwapChain::CreateInterop()
{
    HANDLE sharedFenceHandle;
    DX::ThrowIfFailed(d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence)));
    DX::ThrowIfFailed(
        d3d12Device->CreateSharedHandle(d3d12Fence.get(), nullptr, GENERIC_ALL, nullptr, &sharedFenceHandle));
    DX::ThrowIfFailed(d3d11Device->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence)));
    CloseHandle(sharedFenceHandle);
    d3d12FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!d3d12FenceEvent)
    {
        DX::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    D3D11_TEXTURE2D_DESC texDesc11{};
    texDesc11.Width = swapChainDesc.Width;
    texDesc11.Height = swapChainDesc.Height;
    texDesc11.MipLevels = 1;
    texDesc11.ArraySize = 1;
    texDesc11.Format = swapChainDesc.Format;
    texDesc11.SampleDesc.Count = 1;
    texDesc11.SampleDesc.Quality = 0;
    texDesc11.Usage = D3D11_USAGE_DEFAULT;
    texDesc11.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    texDesc11.CPUAccessFlags = 0;
    texDesc11.MiscFlags = 0;

    if (enbLoaded)
        swapChainBufferProxyENB = std::make_unique<WrappedResource>(texDesc11, d3d11Device.get(), d3d12Device.get());
    else
        swapChainBufferProxy = std::make_unique<Texture2D>(texDesc11);

    swapChainBufferWrapped[0] = std::make_unique<WrappedResource>(texDesc11, d3d11Device.get(), d3d12Device.get());
    swapChainBufferWrapped[1] = std::make_unique<WrappedResource>(texDesc11, d3d11Device.get(), d3d12Device.get());
}

DXGISwapChainProxy *DX12SwapChain::GetSwapChainProxy()
{
    return swapChainProxy.get();
}

void DX12SwapChain::SetD3D11Device(ID3D11Device *a_d3d11Device)
{
    DX::ThrowIfFailed(a_d3d11Device->QueryInterface(IID_PPV_ARGS(&d3d11Device)));
}

void DX12SwapChain::SetD3D11DeviceContext(ID3D11DeviceContext *a_d3d11Context)
{
    DX::ThrowIfFailed(a_d3d11Context->QueryInterface(IID_PPV_ARGS(&d3d11Context)));
}

HRESULT DX12SwapChain::GetBuffer(void **ppSurface)
{
    if (enbLoaded)
        *ppSurface = swapChainBufferProxyENB->resource11.get();
    else
        *ppSurface = swapChainBufferProxy->resource.get();
    return S_OK;
}

HRESULT DX12SwapChain::GetDevice(REFIID uuid, void **ppDevice)
{
    if (uuid == __uuidof(ID3D11Device) || uuid == __uuidof(ID3D11Device1) || uuid == __uuidof(ID3D11Device2) ||
        uuid == __uuidof(ID3D11Device3) || uuid == __uuidof(ID3D11Device4) || uuid == __uuidof(ID3D11Device5))
    {
        *ppDevice = d3d11Device.get();
        return S_OK;
    }

    return swapChain->GetDevice(uuid, ppDevice);
}

void DX12SwapChain::WaitForCommandAllocator(UINT a_index)
{
    const auto waitFenceValue = commandAllocatorFenceValues[a_index];
    if (waitFenceValue == 0 || d3d12Fence->GetCompletedValue() >= waitFenceValue)
    {
        return;
    }

    DX::ThrowIfFailed(d3d12Fence->SetEventOnCompletion(waitFenceValue, d3d12FenceEvent));
    const auto waitResult = WaitForSingleObject(d3d12FenceEvent, 1000);
    if (waitResult == WAIT_OBJECT_0)
    {
        return;
    }

    logger::warn("[DX12SwapChain] Timed out waiting for command allocator {} fence={} completed={}", a_index,
                 waitFenceValue, d3d12Fence->GetCompletedValue());
    DX::ThrowIfFailed(HRESULT_FROM_WIN32(waitResult == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError()));
}

ID3D12GraphicsCommandList4 *DX12SwapChain::BeginInteropCommandList()
{
    DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
    DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
    fenceValue++;

    WaitForCommandAllocator(frameIndex);
    DX::ThrowIfFailed(commandAllocators[frameIndex]->Reset());
    DX::ThrowIfFailed(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr));
    return commandLists[frameIndex].get();
}

void DX12SwapChain::ExecuteInteropCommandListAndWait()
{
    DX::ThrowIfFailed(commandLists[frameIndex]->Close());

    ID3D12CommandList *lists[] = {commandLists[frameIndex].get()};
    commandQueue->ExecuteCommandLists(1, lists);

    DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));
    commandAllocatorFenceValues[frameIndex] = fenceValue;
    DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
    fenceValue++;
}
