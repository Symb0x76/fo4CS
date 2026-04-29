#pragma once

#include <d3d11.h>
#include <winrt/base.h>

#include <array>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include <unordered_set>

namespace CommunityShaders
{
	enum class ShaderStage
	{
		Vertex,
		Pixel,
		Compute,
		Geometry,
		Hull,
		Domain
	};

	class ShaderCache
	{
	public:
		static ShaderCache* GetSingleton();

		void SetDumpAllShaders(bool a_enabled) noexcept { dumpAllShaders = a_enabled; }
		[[nodiscard]] bool ShouldDumpAllShaders() const noexcept { return dumpAllShaders; }

		void ObserveShader(ShaderStage a_stage, const void* a_bytecode, SIZE_T a_bytecodeLength);
		[[nodiscard]] std::string HashShaderBytecode(const void* a_bytecode, SIZE_T a_bytecodeLength) const;

	private:
		struct ShaderMetadata
		{
			std::string uid;
			std::uint32_t hash = 0;
			std::uint32_t asmHash = 0;
			std::uint32_t size = 0;
			std::array<std::uint32_t, 14> constantBufferSizes{};
			std::vector<std::uint32_t> textureSlots;
			std::vector<std::pair<std::uint32_t, std::uint32_t>> textureDimensions;
			std::uint32_t textureSlotMask = 0;
			std::uint32_t textureDimensionMask = 0;
			std::uint32_t inputTextureCount = 0;
			std::uint32_t inputCount = 0;
			std::uint32_t inputMask = 0;
			std::uint32_t outputCount = 0;
			std::uint32_t outputMask = 0;
		};

		ShaderCache() = default;

		void DumpShader(ShaderStage a_stage, std::span<const std::byte> a_bytecode, std::string_view a_hash);
		[[nodiscard]] ShaderMetadata BuildMetadata(ShaderStage a_stage, std::span<const std::byte> a_bytecode, std::string_view a_disassembly) const;
		void WriteMetadataFiles(const std::filesystem::path& a_dumpDirectory, ShaderStage a_stage, const ShaderMetadata& a_metadata) const;
		[[nodiscard]] std::filesystem::path GetDumpDirectory() const;
		[[nodiscard]] static std::string_view GetStageName(ShaderStage a_stage) noexcept;
		[[nodiscard]] static std::string_view GetStageTypeName(ShaderStage a_stage) noexcept;
		[[nodiscard]] static std::uint32_t HashText(std::string_view a_text) noexcept;
		[[nodiscard]] static std::uint32_t GetResourceDimension(std::string_view a_resourceType) noexcept;

		bool dumpAllShaders = false;
		std::unordered_set<std::string> observedHashes;
		std::mutex observedLock;
	};
}
