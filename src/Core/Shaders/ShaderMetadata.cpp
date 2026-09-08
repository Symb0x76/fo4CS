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
	void ShaderCache::DumpShader(ShaderStage a_stage, std::span<const std::byte> a_bytecode, std::string_view a_hash)
	{
		const auto dumpDirectory = GetDumpDirectory() / GetStageName(a_stage);
		std::error_code ec;
		std::filesystem::create_directories(dumpDirectory, ec);
		if (ec) {
			logger::warn("[CommunityShaders] Failed to create shader dump directory {}: {}", dumpDirectory.string(), ec.message());
			return;
		}

		winrt::com_ptr<ID3DBlob> disassembly;
		if (SUCCEEDED(D3DDisassemble(a_bytecode.data(), a_bytecode.size_bytes(), 0, nullptr, disassembly.put()))) {
			const auto disassemblyText = std::string_view{
				static_cast<const char*>(disassembly->GetBufferPointer()),
				disassembly->GetBufferSize()
			};
			const auto metadata = BuildMetadata(a_stage, a_bytecode, disassemblyText);
			{
				std::scoped_lock lock(observedLock);
				bytecodeToAsmHash[std::string{ a_hash }] = metadata.asmHash;
				bytecodeToMetadata[std::string{ a_hash }] = metadata;
			}
			const auto binPath = dumpDirectory / std::format("{}.bin", metadata.uid);
			std::ofstream bin{ binPath, std::ios::binary };
			bin.write(reinterpret_cast<const char*>(a_bytecode.data()), static_cast<std::streamsize>(a_bytecode.size_bytes()));

			const auto asmPath = dumpDirectory / std::format("{}.asm", metadata.uid);
			std::ofstream asmFile{ asmPath, std::ios::binary };
			asmFile.write(disassemblyText.data(), static_cast<std::streamsize>(disassemblyText.size()));
			WriteMetadataFiles(dumpDirectory, a_stage, metadata);
		} else {
			const auto binPath = dumpDirectory / std::format("{}.bin", a_hash);
			std::ofstream bin{ binPath, std::ios::binary };
			bin.write(reinterpret_cast<const char*>(a_bytecode.data()), static_cast<std::streamsize>(a_bytecode.size_bytes()));
			logger::warn("[CommunityShaders] Failed to disassemble {} shader {}", GetStageName(a_stage), a_hash);
		}
	}

	ShaderCache::ShaderMetadata ShaderCache::BuildMetadata(ShaderStage a_stage, std::span<const std::byte> a_bytecode, std::string_view a_disassembly) const
	{
		ShaderMetadata metadata;
		metadata.hash = HashText({ reinterpret_cast<const char*>(a_bytecode.data()), a_bytecode.size() });
		metadata.size = static_cast<std::uint32_t>(a_bytecode.size_bytes());

		static const auto regexFlags = std::regex_constants::optimize | std::regex_constants::icase;
		static const std::regex instructionRegex{ "^\\s*(add|sub|mul|div|mad|max|min|dp2|dp3|dp4|rsq|sqrt|and|or|xor|not|lt|gt|le|ge|eq|ne|mov(?:c|_sat)?|sample(?:_indexable)?|loop|endloop|if|else|endif|break(?:c)?|ret)\\b", regexFlags };
		static const std::regex sampleInstructionRegex{ "^\\s*(sample(?:_\\w+)?|ld(?:_\\w+)?)\\b", regexFlags };
		static const std::regex textureSampleSlotRegex{ "\\bt(\\d+)(?:\\.|\\b)", regexFlags };
		static const std::regex textureRegex{ "dcl_resource_(\\w+)\\s*(?:\\([^)]*\\))?\\s*(?:\\([^)]+\\))?\\s+t(\\d+)", regexFlags };
		static const std::regex inputRegex{ "^\\s*dcl_input[^\\r\\n]*\\bv(\\d+)(?:\\.|\\b)", regexFlags };
		static const std::regex constantBufferRegex{ "dcl_constantbuffer\\s+cb(\\d+)\\[(\\d+)\\]", regexFlags };
		static const std::regex outputRegex{ "^\\s*dcl_output[^\\r\\n]*\\bo(\\d+)(?:\\.|\\b)", regexFlags };
		static const std::regex discardRegex{ "^\\s*discard(?:_\\w+)?\\b", regexFlags };
		static const std::regex immediateConstantBufferRegex{ "^\\s*dcl_immediateConstantBuffer\\b", regexFlags };

		std::string opcodeStream;
		std::istringstream stream{ std::string{ a_disassembly } };
		std::string line;
		bool inImmediateConstantBuffer = false;
		int immediateConstantBufferDepth = 0;
		while (std::getline(stream, line)) {
			std::smatch match;
			if (std::regex_search(line, match, instructionRegex)) {
				++metadata.instructionCount;
				opcodeStream += match[1].str();
				opcodeStream += ';';
			}

			if (std::regex_search(line, match, sampleInstructionRegex)) {
				++metadata.sampleInstructionCount;
				for (auto it = std::sregex_iterator(line.begin(), line.end(), textureSampleSlotRegex); it != std::sregex_iterator(); ++it) {
					const auto slot = static_cast<std::uint32_t>(std::stoul((*it)[1].str()));
					if (slot < metadata.textureSampleCounts.size()) {
						++metadata.textureSampleCounts[slot];
					}
				}
			}

			const bool startsImmediateConstantBuffer = std::regex_search(line, immediateConstantBufferRegex);
			if (startsImmediateConstantBuffer) {
				metadata.hasImmediateConstantBuffer = true;
				inImmediateConstantBuffer = true;
				immediateConstantBufferDepth = 0;
			}

			if (inImmediateConstantBuffer) {
				const auto openBraceCount = static_cast<int>(std::count(line.begin(), line.end(), '{'));
				const auto closeBraceCount = static_cast<int>(std::count(line.begin(), line.end(), '}'));
				const auto declarationBraceCount = startsImmediateConstantBuffer ? 1 : 0;
				if (openBraceCount > declarationBraceCount) {
					metadata.immediateConstantBufferRows += static_cast<std::uint32_t>(openBraceCount - declarationBraceCount);
				}
				immediateConstantBufferDepth += openBraceCount - closeBraceCount;
				if (immediateConstantBufferDepth <= 0) {
					inImmediateConstantBuffer = false;
					immediateConstantBufferDepth = 0;
				}
			}

			if (std::regex_search(line, discardRegex)) {
				metadata.hasDiscard = true;
			}

			if (std::regex_search(line, match, textureRegex)) {
				const auto slot = static_cast<std::uint32_t>(std::stoul(match[2].str()));
				const auto dimension = GetResourceDimension(match[1].str());
				metadata.textureSlots.push_back(slot);
				metadata.textureDimensions.emplace_back(dimension, slot);
				if (slot < 32) {
					metadata.textureSlotMask |= (1u << slot);
				}
				if (dimension < 32) {
					metadata.textureDimensionMask |= (1u << dimension);
				}
				++metadata.inputTextureCount;
				continue;
			}

			if (std::regex_search(line, match, inputRegex)) {
				const auto inputIndex = static_cast<std::uint32_t>(std::stoul(match[1].str()));
				if (inputIndex < 32) {
					metadata.inputMask |= (1u << inputIndex);
				}
				++metadata.inputCount;
				continue;
			}

			if (std::regex_search(line, match, constantBufferRegex)) {
				const auto slot = static_cast<std::uint32_t>(std::stoul(match[1].str()));
				const auto sizeInDwords = static_cast<std::uint32_t>(std::stoul(match[2].str()));
				if (slot < metadata.constantBufferSizes.size()) {
					metadata.constantBufferSizes[slot] = sizeInDwords * 16;
				}
				continue;
			}

			if (std::regex_search(line, match, outputRegex)) {
				const auto outputIndex = static_cast<std::uint32_t>(std::stoul(match[1].str()));
				if (outputIndex < 32) {
					metadata.outputMask |= (1u << outputIndex);
				}
				++metadata.outputCount;
			}
		}

		metadata.asmHash = HashText(opcodeStream);
		metadata.uid = std::format("{}{:08X}I{}O{}", GetStageName(a_stage), metadata.asmHash, metadata.inputCount, metadata.outputCount);
		return metadata;
	}

	void ShaderCache::WriteMetadataFiles(const std::filesystem::path& a_dumpDirectory, ShaderStage a_stage, const ShaderMetadata& a_metadata) const
	{
		const auto logPath = a_dumpDirectory / std::format("{}.txt", a_metadata.uid);
		std::ofstream logFile{ logPath };
		logFile << "[shaderDump]\n";
		logFile << "active=true\n";
		logFile << "priority=0\n";
		logFile << "type=" << GetStageTypeName(a_stage) << "\n";
		logFile << "shaderUID=" << a_metadata.uid << "\n";
		logFile << std::format("hash=0x{:08X}\n", a_metadata.hash);
		logFile << std::format("asmHash=0x{:08X}\n", a_metadata.asmHash);
		logFile << "size=(" << a_metadata.size << ")\n";

		logFile << "buffersize=";
		bool first = true;
		for (std::size_t slot = 0; slot < a_metadata.constantBufferSizes.size(); ++slot) {
			if (a_metadata.constantBufferSizes[slot] == 0) {
				continue;
			}
			if (!first) {
				logFile << ",";
			}
			logFile << a_metadata.constantBufferSizes[slot] << "@" << slot;
			first = false;
		}
		logFile << "\n";

		logFile << "textures=";
		for (std::size_t index = 0; index < a_metadata.textureSlots.size(); ++index) {
			if (index > 0) {
				logFile << ",";
			}
			logFile << a_metadata.textureSlots[index];
		}
		logFile << "\n";

		logFile << "textureDimensions=";
		for (std::size_t index = 0; index < a_metadata.textureDimensions.size(); ++index) {
			if (index > 0) {
				logFile << ",";
			}
			const auto [dimension, slot] = a_metadata.textureDimensions[index];
			logFile << dimension << "@" << slot;
		}
		logFile << "\n";

		logFile << "textureSampleCounts=";
		first = true;
		for (std::size_t slot = 0; slot < a_metadata.textureSampleCounts.size(); ++slot) {
			const auto count = a_metadata.textureSampleCounts[slot];
			if (count == 0) {
				continue;
			}
			if (!first) {
				logFile << ",";
			}
			logFile << slot << ":" << count;
			first = false;
		}
		logFile << "\n";

		logFile << std::format("textureSlotMask=0x{:X}\n", a_metadata.textureSlotMask);
		logFile << std::format("textureDimensionMask=0x{:X}\n", a_metadata.textureDimensionMask);
		logFile << "inputTextureCount=(" << a_metadata.inputTextureCount << ")\n";
		logFile << "inputcount=(" << a_metadata.inputCount << ")\n";
		logFile << std::format("inputMask=0x{:X}\n", a_metadata.inputMask);
		logFile << "outputcount=(" << a_metadata.outputCount << ")\n";
		logFile << std::format("outputMask=0x{:X}\n", a_metadata.outputMask);
		logFile << "instructionCount=(" << a_metadata.instructionCount << ")\n";
		logFile << "sampleInstructionCount=(" << a_metadata.sampleInstructionCount << ")\n";
		logFile << "immediateConstantBufferRows=(" << a_metadata.immediateConstantBufferRows << ")\n";
		logFile << "hasDiscard=" << (a_metadata.hasDiscard ? "true" : "false") << "\n";
		logFile << "hasImmediateConstantBuffer=" << (a_metadata.hasImmediateConstantBuffer ? "true" : "false") << "\n";
		logFile << "shader=;" << a_metadata.uid << "_replacement.hlsl\n";
		logFile << "log=true\n";
		logFile << "dump=true\n";
		logFile << "[/shaderDump]\n";
	}

	std::filesystem::path ShaderCache::GetDumpDirectory() const
	{
		return std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "CommunityShaders" / "ShaderDump" / State::GetSingleton()->GetRuntimeName();
	}

	std::string_view ShaderCache::GetStageName(ShaderStage a_stage) noexcept
	{
		switch (a_stage) {
		case ShaderStage::Vertex:
			return "VS";
		case ShaderStage::Pixel:
			return "PS";
		case ShaderStage::Compute:
			return "CS";
		case ShaderStage::Geometry:
			return "GS";
		case ShaderStage::Hull:
			return "HS";
		case ShaderStage::Domain:
			return "DS";
		default:
			return "Unknown";
		}
	}

	std::string_view ShaderCache::GetStageTypeName(ShaderStage a_stage) noexcept
	{
		switch (a_stage) {
		case ShaderStage::Vertex:
			return "vs";
		case ShaderStage::Pixel:
			return "ps";
		case ShaderStage::Compute:
			return "cs";
		case ShaderStage::Geometry:
			return "gs";
		case ShaderStage::Hull:
			return "hs";
		case ShaderStage::Domain:
			return "ds";
		default:
			return "unknown";
		}
	}

	std::uint32_t ShaderCache::HashText(std::string_view a_text) noexcept
	{
		std::uint32_t hash = 2166136261u;
		for (const auto value : a_text) {
			hash ^= static_cast<std::uint8_t>(value);
			hash *= 16777619u;
		}
		return hash;
	}

	std::uint32_t ShaderCache::GetResourceDimension(std::string_view a_resourceType) noexcept
	{
		std::string resourceType{ a_resourceType };
		std::ranges::transform(resourceType, resourceType.begin(), [](unsigned char a_ch) {
			return static_cast<char>(std::tolower(a_ch));
		});

		if (resourceType == "texture2d") return 4;
		if (resourceType == "texture2dms") return 6;
		if (resourceType == "texture2darray") return 5;
		if (resourceType == "texturecube") return 8;
		if (resourceType == "texturecubearray") return 11;
		if (resourceType == "texture3d") return 7;
		if (resourceType == "texture1d") return 3;
		if (resourceType == "buffer") return 1;
		return 0;
	}
}
