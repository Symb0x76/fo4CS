#pragma once

#include <d3d11.h>
#include <atomic>
#include <cstdint>
#include <optional>
#include <winrt/base.h>

#include <array>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

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
		[[nodiscard]] std::optional<std::uint32_t> GetAsmHashForBytecode(const void* a_bytecode, SIZE_T a_bytecodeLength);

		void SetTracePipeline(bool a_enabled) noexcept { tracePipeline = a_enabled; }
		[[nodiscard]] bool ShouldTracePipeline() const noexcept { return tracePipeline; }

		// Shader 创建计数器 — 用于验证 Deferred hook 触发时序
		// 每创建新 shader 递增（去重前），由 hook 日志读取
		[[nodiscard]] uint32_t GetShaderCreationCount() const noexcept { return m_hookVerifyCounter.load(); }

	public:
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

	private:

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
		std::unordered_map<std::string, std::uint32_t> bytecodeToAsmHash;
		bool tracePipeline = false;
		std::unordered_set<std::string> traceStackHashes;
		void TraceShaderCreation(ShaderStage a_stage, SIZE_T a_len, std::string_view a_hash);
		std::atomic<uint32_t> m_hookVerifyCounter{ 0 };
	};
}
