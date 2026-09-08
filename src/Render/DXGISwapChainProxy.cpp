#include "Render/DX12SwapChain.h"

// COM forwarding shell around the real IDXGISwapChain4. Only QueryInterface,
// GetDevice, Present and GetBuffer carry fo4CS logic; everything else forwards.

DXGISwapChainProxy::DXGISwapChainProxy(IDXGISwapChain4 *a_swapChain)
{
    swapChain = a_swapChain;
}

/****IUnknown****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::QueryInterface(REFIID riid, void **ppvObj)
{
    if (!ppvObj)
        return E_POINTER;
    // Only hand out interfaces this proxy actually implements. Anything else
    // (IDXGISwapChain1..4, etc.) must be served by the inner swap chain, or
    // callers would dispatch through a vtable the proxy does not provide.
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGISwapChain))
    {
        *ppvObj = static_cast<IDXGISwapChain *>(this);
        AddRef();
        return S_OK;
    }
    return swapChain->QueryInterface(riid, ppvObj);
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::AddRef()
{
    return swapChain->AddRef();
}

ULONG STDMETHODCALLTYPE DXGISwapChainProxy::Release()
{
    return swapChain->Release();
}

/****IDXGIObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateData(_In_ REFGUID Name, UINT DataSize,
                                                             _In_reads_bytes_(DataSize) const void *pData)
{
    return swapChain->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetPrivateDataInterface(_In_ REFGUID Name,
                                                                      _In_opt_ const IUnknown *pUnknown)
{
    return swapChain->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetPrivateData(_In_ REFGUID Name, _Inout_ UINT *pDataSize,
                                                             _Out_writes_bytes_(*pDataSize) void *pData)
{
    return swapChain->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetParent(_In_ REFIID riid, _COM_Outptr_ void **ppParent)
{
    return swapChain->GetParent(riid, ppParent);
}

/****IDXGIDeviceSubObject****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDevice(_In_ REFIID riid, _COM_Outptr_ void **ppDevice)
{
    return DX12SwapChain::GetSingleton()->GetDevice(riid, ppDevice);
}

/****IDXGISwapChain****/
HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
    return DX12SwapChain::GetSingleton()->Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetBuffer(UINT, _In_ REFIID, _COM_Outptr_ void **ppSurface)
{
    return DX12SwapChain::GetSingleton()->GetBuffer(ppSurface);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::SetFullscreenState(BOOL, _In_opt_ IDXGIOutput *)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFullscreenState(
    _Out_opt_ BOOL *pFullscreen, _COM_Outptr_opt_result_maybenull_ IDXGIOutput **ppTarget)
{
    return swapChain->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC *pDesc)
{
    return swapChain->GetDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                                            DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    return swapChain->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::ResizeTarget(_In_ const DXGI_MODE_DESC *)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetContainingOutput(_COM_Outptr_ IDXGIOutput **ppOutput)
{
    return swapChain->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS *pStats)
{
    return swapChain->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE DXGISwapChainProxy::GetLastPresentCount(_Out_ UINT *pLastPresentCount)
{
    return swapChain->GetLastPresentCount(pLastPresentCount);
}
