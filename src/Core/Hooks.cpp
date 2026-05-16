#include "Core/Hooks.h"

#include "Core/ShaderCache.h"
#include "Core/ShaderDB.h"
#include "Core/ShaderCompiler.h"
#include "Core/State.h"

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

		std::string HashShaderBytecode(const void* a_bytecode, SIZE_T a_bytecodeLength)
		{
			if (!a_bytecode || a_bytecodeLength == 0) {
				return "0000000000000000";
			}

			const auto* bytes = static_cast<const std::uint8_t*>(a_bytecode);
			std::uint64_t hash = 14695981039346656037ull;
			for (SIZE_T i = 0; i < a_bytecodeLength; ++i) {
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}

			return std::format("{:016X}", hash);
		}

		std::uint32_t ReadBytecodeMagic(const void* a_bytecode, SIZE_T a_bytecodeLength)
		{
			if (!a_bytecode || a_bytecodeLength < sizeof(std::uint32_t)) {
				return 0;
			}

			std::uint32_t magic = 0;
			std::memcpy(&magic, a_bytecode, sizeof(magic));
			return magic;
		}

		void LogCreateComputeShaderFailure(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, HRESULT a_result)
		{
			const auto removedReason = a_device ? a_device->GetDeviceRemovedReason() : E_POINTER;
			logger::error(
				"[CommunityShaders] CreateComputeShader failed (hr=0x{:08X}, deviceRemoved=0x{:08X}, len={}, magic=0x{:08X}, hash={})",
				static_cast<std::uint32_t>(a_result),
				static_cast<std::uint32_t>(removedReason),
				a_bytecodeLength,
				ReadBytecodeMagic(a_bytecode, a_bytecodeLength),
				HashShaderBytecode(a_bytecode, a_bytecodeLength));
		}

		HRESULT STDMETHODCALLTYPE CreateVertexShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11VertexShader** a_vertexShader)
		{
			ShaderCache::GetSingleton()->ObserveShader(ShaderStage::Vertex, a_bytecode, a_bytecodeLength);

			auto* db = ShaderDB::GetSingleton();
			if (auto asmHash = ShaderCache::GetSingleton()->GetAsmHashForBytecode(a_bytecode, a_bytecodeLength)) {
				if (auto* def = db->FindByHash(State::GetSingleton()->GetRuntimeName(), "VS", *asmHash)) {
					if (!def->shader.empty()) {
						auto compiled = ShaderCompiler::GetSingleton()->CompileFromFile(def->shader);
						if (!compiled.empty())
							return createVertexShader(a_device, compiled.data(), compiled.size(), a_classLinkage, a_vertexShader);
					}
				}
			}

			return createVertexShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_vertexShader);
		}

		HRESULT STDMETHODCALLTYPE CreatePixelShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11PixelShader** a_pixelShader)
		{
			ShaderCache::GetSingleton()->ObserveShader(ShaderStage::Pixel, a_bytecode, a_bytecodeLength);

			auto* db = ShaderDB::GetSingleton();
			if (auto asmHash = ShaderCache::GetSingleton()->GetAsmHashForBytecode(a_bytecode, a_bytecodeLength)) {
				if (auto* def = db->FindByHash(State::GetSingleton()->GetRuntimeName(), "PS", *asmHash)) {
					if (!def->shader.empty()) {
						auto compiled = ShaderCompiler::GetSingleton()->CompileFromFile(def->shader);
						if (!compiled.empty())
							return createPixelShader(a_device, compiled.data(), compiled.size(), a_classLinkage, a_pixelShader);
					}
				}
			}

			return createPixelShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_pixelShader);
		}

		HRESULT STDMETHODCALLTYPE CreateComputeShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11ComputeShader** a_computeShader)
		{
			ShaderCache::GetSingleton()->ObserveShader(ShaderStage::Compute, a_bytecode, a_bytecodeLength);

			auto* db = ShaderDB::GetSingleton();
			if (auto asmHash = ShaderCache::GetSingleton()->GetAsmHashForBytecode(a_bytecode, a_bytecodeLength)) {
				if (auto* def = db->FindByHash(State::GetSingleton()->GetRuntimeName(), "CS", *asmHash)) {
					if (!def->shader.empty()) {
						auto compiled = ShaderCompiler::GetSingleton()->CompileFromFile(def->shader);
						if (!compiled.empty())
							return createComputeShader(a_device, compiled.data(), compiled.size(), a_classLinkage, a_computeShader);
					}
				}
			}

			const auto result = createComputeShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_computeShader);
			if (FAILED(result)) {
				LogCreateComputeShaderFailure(a_device, a_bytecode, a_bytecodeLength, result);
			}
			return result;
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
