#include "Core/ShaderCache.h"

#include "Core/CommunityShaders.h"
#include "Core/DebugSwitches.h"
#include "Core/Feature.h"
#include "Core/PreNGEnvironment.h"
#include "Core/ShaderCompiler.h"
#include "Core/State.h"
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSShader.h"
#else
#include "RE/Bethesda/BSShader.h"
#endif
#include <RE/FO4Runtime.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <format>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>

namespace CommunityShaders
{
	void ShaderCache::ObserveShader(ShaderStage a_stage, const void* a_bytecode, SIZE_T a_bytecodeLength)
	{
		if (!a_bytecode || a_bytecodeLength == 0) {
			return;
		}

		m_hookVerifyCounter.fetch_add(1);

		const auto hash = HashShaderBytecode(a_bytecode, a_bytecodeLength);
		{
			std::scoped_lock lock(observedLock);
			if (!observedHashes.insert(hash).second) {
				return;
			}
		}

		logger::trace("[CommunityShaders] Observed {} shader {} ({} bytes)", GetStageName(a_stage), hash, a_bytecodeLength);

		if (tracePipeline)
			TraceShaderCreation(a_stage, a_bytecodeLength, hash);

		if (!dumpAllShaders)
			return;

		const auto bytes = std::span{ static_cast<const std::byte*>(a_bytecode), a_bytecodeLength };
		DumpShader(a_stage, bytes, hash);
	}

	std::optional<std::uint32_t> ShaderCache::GetAsmHashForBytecode(const void* a_bytecode, SIZE_T a_bytecodeLength)
	{
		auto metadata = GetMetadataForBytecode(ShaderStage::Pixel, a_bytecode, a_bytecodeLength);
		if (!metadata) {
			return std::nullopt;
		}

		return metadata->asmHash;
	}

	std::optional<ShaderCache::ShaderMetadata> ShaderCache::GetMetadataForBytecode(ShaderStage a_stage, const void* a_bytecode, SIZE_T a_bytecodeLength)
	{
		if (!a_bytecode || a_bytecodeLength == 0) {
			return std::nullopt;
		}

		const auto bytecodeHash = HashShaderBytecode(a_bytecode, a_bytecodeLength);
		{
			std::scoped_lock lock(observedLock);
			if (auto it = bytecodeToMetadata.find(bytecodeHash); it != bytecodeToMetadata.end()) {
				return it->second;
			}
		}

		auto bytes = std::span{ static_cast<const std::byte*>(a_bytecode), a_bytecodeLength };
		winrt::com_ptr<ID3DBlob> disassembly;
		if (FAILED(D3DDisassemble(bytes.data(), bytes.size_bytes(), 0, nullptr, disassembly.put()))) {
			return std::nullopt;
		}

		auto disasmText = std::string_view{
			static_cast<const char*>(disassembly->GetBufferPointer()),
			disassembly->GetBufferSize()
		};

		auto metadata = BuildMetadata(a_stage, bytes, disasmText);
		{
			std::scoped_lock lock(observedLock);
			bytecodeToAsmHash[bytecodeHash] = metadata.asmHash;
			bytecodeToMetadata[bytecodeHash] = metadata;
		}
		return metadata;
	}

	void ShaderCache::ObserveD3DShaderObject(ShaderStage a_stage, std::uintptr_t a_d3dObject, const ShaderMetadata& a_metadata)
	{
		if (a_d3dObject == 0) {
			return;
		}

		std::scoped_lock lock(observedLock);
		d3dShaderObjectToMetadata.insert_or_assign(D3DShaderObjectKey{ a_stage, a_d3dObject }, a_metadata);
	}

	void ShaderCache::ObserveD3DShaderObjectBytecode(ShaderStage a_stage, std::uintptr_t a_d3dObject, const ShaderMetadata& a_metadata, const void* a_bytecode, SIZE_T a_bytecodeLength)
	{
		if (a_d3dObject == 0 || !a_bytecode || a_bytecodeLength == 0) {
			return;
		}

		const auto bytes = std::span{ static_cast<const std::byte*>(a_bytecode), a_bytecodeLength };
		D3DShaderObjectBytecode observed;
		observed.metadata = a_metadata;
		observed.bytecodeHash = HashShaderBytecode(a_bytecode, a_bytecodeLength);
		observed.bytecode.assign(bytes.begin(), bytes.end());

		std::scoped_lock lock(observedLock);
		const D3DShaderObjectKey key{ a_stage, a_d3dObject };
		d3dShaderObjectToMetadata.insert_or_assign(key, a_metadata);
		d3dShaderObjectToBytecode.insert_or_assign(key, std::move(observed));
	}

	std::optional<ShaderCache::ShaderMetadata> ShaderCache::GetMetadataForD3DShaderObject(ShaderStage a_stage, std::uintptr_t a_d3dObject)
	{
		if (a_d3dObject == 0) {
			return std::nullopt;
		}

		std::scoped_lock lock(observedLock);
		if (auto it = d3dShaderObjectToMetadata.find(D3DShaderObjectKey{ a_stage, a_d3dObject }); it != d3dShaderObjectToMetadata.end()) {
			return it->second;
		}

		return std::nullopt;
	}

	bool ShaderCache::DumpObservedD3DShaderObject(
		ShaderStage a_stage,
		std::uintptr_t a_d3dObject,
		std::string_view a_label,
		std::string_view a_familySubdir)
	{
		if (a_d3dObject == 0) {
			return false;
		}

		D3DShaderObjectBytecode observed;
		const std::string label{ a_label };
		{
			std::scoped_lock lock(observedLock);
			const D3DShaderObjectKey objectKey{ a_stage, a_d3dObject };
			const auto it = d3dShaderObjectToBytecode.find(objectKey);
			if (it == d3dShaderObjectToBytecode.end()) {
				return false;
			}

			const auto dumpKey = std::format("{}:{:X}:{}", GetStageName(a_stage), a_d3dObject, label.empty() ? it->second.metadata.uid : label);
			if (!dumpedD3DShaderObjectKeys.insert(dumpKey).second) {
				return true;
			}

			observed = it->second;
		}

		const auto familySubdir = a_familySubdir.empty() ? std::string_view{ "Unknown" } : a_familySubdir;
		const auto dumpDirectory = GetDumpDirectory() / "LightLimitFix" / familySubdir / GetStageName(a_stage);
		std::error_code ec;
		std::filesystem::create_directories(dumpDirectory, ec);
		if (ec) {
			logger::warn("[CommunityShaders] Failed to create targeted shader dump directory {}: {}", dumpDirectory.string(), ec.message());
			return false;
		}

		const auto stem = label.empty() ?
			observed.metadata.uid :
			std::format("{}_{}", label, observed.metadata.uid);
		const auto binPath = dumpDirectory / std::format("{}.bin", stem);
		std::ofstream bin{ binPath, std::ios::binary };
		bin.write(reinterpret_cast<const char*>(observed.bytecode.data()), static_cast<std::streamsize>(observed.bytecode.size()));

		winrt::com_ptr<ID3DBlob> disassembly;
		if (SUCCEEDED(D3DDisassemble(observed.bytecode.data(), observed.bytecode.size(), 0, nullptr, disassembly.put()))) {
			const auto disassemblyText = std::string_view{
				static_cast<const char*>(disassembly->GetBufferPointer()),
				disassembly->GetBufferSize()
			};
			const auto asmPath = dumpDirectory / std::format("{}.asm", stem);
			std::ofstream asmFile{ asmPath, std::ios::binary };
			asmFile.write(disassemblyText.data(), static_cast<std::streamsize>(disassemblyText.size()));
		} else {
			logger::warn("[CommunityShaders] Failed to disassemble targeted {} shader {}", GetStageName(a_stage), observed.bytecodeHash);
		}

		WriteMetadataFiles(dumpDirectory, a_stage, observed.metadata);

		const auto capturePath = dumpDirectory / std::format("{}.capture.txt", stem);
		std::ofstream captureFile{ capturePath };
		captureFile << "[targetedShaderDump]\n";
		captureFile << "label=" << stem << "\n";
		captureFile << std::format("d3dObject=0x{:X}\n", a_d3dObject);
		captureFile << "stage=" << GetStageName(a_stage) << "\n";
		captureFile << "bytecodeHash=" << observed.bytecodeHash << "\n";
		captureFile << "shaderUID=" << observed.metadata.uid << "\n";
		captureFile << std::format("hash=0x{:08X}\n", observed.metadata.hash);
		captureFile << std::format("asmHash=0x{:08X}\n", observed.metadata.asmHash);
		captureFile << "bytecodeSize=" << observed.bytecode.size() << "\n";
		captureFile << "[/targetedShaderDump]\n";

		logger::info(
			"[CommunityShaders] Targeted observed {} shader dumped label={} d3dObject=0x{:X} uid={} hash={} asm=0x{:08X} size={} path={}",
			GetStageName(a_stage),
			stem,
			a_d3dObject,
			observed.metadata.uid,
			observed.bytecodeHash,
			observed.metadata.asmHash,
			observed.bytecode.size(),
			dumpDirectory.string());
		return true;
	}

	void ShaderCache::TraceShaderCreation(ShaderStage a_stage, SIZE_T a_len, std::string_view a_hash)
	{
		void* stack[64];
		USHORT frames = RtlCaptureStackBackTrace(0, 64, stack, nullptr);
		if (frames == 0)
			return;

		uint64_t stackHash = 14695981039346656037ull;
		for (USHORT i = 0; i < frames; i++) {
			stackHash ^= reinterpret_cast<uint64_t>(stack[i]);
			stackHash *= 1099511628211ull;
		}
		auto stackHashStr = std::format("{:016X}", stackHash);
		{
			std::scoped_lock lock(observedLock);
			if (!traceStackHashes.insert(stackHashStr).second)
				return;
		}

		auto moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("Fallout4.exe"));
		auto traceDir = GetDumpDirectory().parent_path() / "PipelineTrace" / State::GetSingleton()->GetRuntimeName();
		std::error_code ec;
		std::filesystem::create_directories(traceDir, ec);

		auto traceFile = traceDir / "pipeline_trace.txt";
		std::ofstream out(traceFile, std::ios::app);
		out << std::format("[{}] {} (hash={}, {} bytes)\n", GetStageName(a_stage), a_hash, a_hash, a_len);
		out << std::format("  Frames: {}\n", frames);
		for (USHORT i = 0; i < frames && i < 20; i++) {
			auto offset = reinterpret_cast<uintptr_t>(stack[i]) - moduleBase;
			out << std::format("    [{}] Fallout4.exe+0x{:X}\n", i, offset);
		}
		out << "\n";
	}


	std::string ShaderCache::HashShaderBytecode(const void* a_bytecode, SIZE_T a_bytecodeLength) const
	{
		const auto bytes = std::span{ static_cast<const std::byte*>(a_bytecode), a_bytecodeLength };
		uint64_t hash = 14695981039346656037ull;
		for (const auto byte : bytes) {
			hash ^= static_cast<uint8_t>(byte);
			hash *= 1099511628211ull;
		}

		return std::format("{:016X}", hash);
	}
}
