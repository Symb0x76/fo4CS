#pragma once

#include "HDR.h"

#include <cstdint>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

class HDRCalibrationOverlay
{
public:
	~HDRCalibrationOverlay();

	bool IsActive() const;
	bool Render(
		ID3D12Device* device,
		ID3D12CommandQueue* commandQueue,
		IDXGISwapChain4* swapChain,
		ID3D12GraphicsCommandList4* commandList,
		ID3D12Resource* backBuffer,
		DXGI_FORMAT swapChainFormat,
		HDRSettings& settings);

private:
	bool Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue, IDXGISwapChain4* swapChain, DXGI_FORMAT swapChainFormat, const HDRSettings& settings);
	void Shutdown();
	bool ReloadActivation(HDRSettings& settings);
	void SaveAndClose(HDRSettings& settings);
	void Cancel(HDRSettings& settings);
	void RenderPattern(ID3D12GraphicsCommandList4* commandList, ID3D12Resource* backBuffer, DXGI_FORMAT swapChainFormat, const HDRSettings& settings);
	void RenderUI(HDRSettings& settings);
	void EnsurePatternResources(ID3D12Device* device, DXGI_FORMAT swapChainFormat);
	void UpdatePatternConstants(ID3D12GraphicsCommandList4* commandList, const HDRSettings& settings);
	void ApplyHDRMetadata(IDXGISwapChain4* swapChain, const HDRSettings& settings);

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	bool initialized = false;
	bool active = false;
	bool showClippingWarning = true;
	bool showColorBars = true;
	bool imguiContextCreated = false;
	bool win32Initialized = false;
	bool dx12Initialized = false;
	bool applyMetadataOnClose = false;
	HWND hwnd = nullptr;
	WNDPROC previousWndProc = nullptr;
	DXGI_FORMAT initializedFormat = DXGI_FORMAT_UNKNOWN;
	HDRSettings editableSettings;

	winrt::com_ptr<ID3D12DescriptorHeap> srvHeap;
	winrt::com_ptr<ID3D12DescriptorHeap> rtvHeap;
	winrt::com_ptr<ID3D12RootSignature> rootSignature;
	winrt::com_ptr<ID3D12PipelineState> pipelineState;
	winrt::com_ptr<ID3D12Resource> constantBuffer;
	std::uint8_t* mappedConstants = nullptr;
};
