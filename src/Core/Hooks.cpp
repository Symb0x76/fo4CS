#include "Core/Hooks.h"

#include "Core/ShaderCache.h"

namespace CommunityShaders::Hooks
{
	namespace
	{
		using CreateVertexShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**);
		using CreatePixelShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
		using CreateComputeShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**);

		CreateVertexShaderFn createVertexShader = nullptr;
		CreatePixelShaderFn createPixelShader = nullptr;
		CreateComputeShaderFn createComputeShader = nullptr;
		bool installedDeviceHooks = false;

		HRESULT STDMETHODCALLTYPE CreateVertexShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11VertexShader** a_vertexShader)
		{
			ShaderCache::GetSingleton()->ObserveShader(ShaderStage::Vertex, a_bytecode, a_bytecodeLength);
			return createVertexShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_vertexShader);
		}

		HRESULT STDMETHODCALLTYPE CreatePixelShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11PixelShader** a_pixelShader)
		{
			ShaderCache::GetSingleton()->ObserveShader(ShaderStage::Pixel, a_bytecode, a_bytecodeLength);
			return createPixelShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_pixelShader);
		}

		HRESULT STDMETHODCALLTYPE CreateComputeShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11ComputeShader** a_computeShader)
		{
			ShaderCache::GetSingleton()->ObserveShader(ShaderStage::Compute, a_bytecode, a_bytecodeLength);
			return createComputeShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_computeShader);
		}
	}

	void Install()
	{
		logger::info("[CommunityShaders] Core hooks armed");
	}

	void OnD3D11DeviceCreated(ID3D11Device* a_device)
	{
		if (!a_device || installedDeviceHooks) {
			return;
		}

		*(uintptr_t*)&createVertexShader = Detours::X64::DetourClassVTable(*(uintptr_t*)a_device, &CreateVertexShaderHook, 12);
		*(uintptr_t*)&createPixelShader = Detours::X64::DetourClassVTable(*(uintptr_t*)a_device, &CreatePixelShaderHook, 15);
		*(uintptr_t*)&createComputeShader = Detours::X64::DetourClassVTable(*(uintptr_t*)a_device, &CreateComputeShaderHook, 18);
		installedDeviceHooks = true;

		logger::info("[CommunityShaders] D3D11 shader creation hooks installed");
	}
}
