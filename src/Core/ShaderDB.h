#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommunityShaders
{
	enum class ShaderStage;

	struct ShaderDef
	{
		std::string uid;
		std::string type;  // "ps" / "vs" / "cs" / "gs" / "hs" / "ds"
		std::uint32_t hash = 0;
		std::uint32_t asmHash = 0;
		std::string shader;    // replacement .hlsl filename (empty = none)
		bool active = true;
		int priority = 0;

		std::optional<std::uint32_t> size;
		std::map<int, int> bufferSizes;          // slot → size
		std::vector<int> textureSlots;
		std::vector<std::pair<int, int>> textureDimensions;  // dim, slot
		std::uint32_t textureSlotMask = 0;
		std::uint32_t textureDimensionMask = 0;
		std::uint32_t inputTextureCount = 0;
		std::uint32_t inputCount = 0;
		std::uint32_t inputMask = 0;
		std::uint32_t outputCount = 0;
		std::uint32_t outputMask = 0;
	};

	class ShaderDB
	{
	public:
		static ShaderDB* GetSingleton();

		void Load(std::string_view a_runtimeName, std::string_view a_dbPath);
		void Clear();

		[[nodiscard]] const ShaderDef* Find(std::string_view a_runtime, std::string_view a_uid) const;
		[[nodiscard]] const ShaderDef* FindByHash(std::string_view a_runtime, std::string_view a_stageType, std::uint32_t a_asmHash) const;

		[[nodiscard]] size_t GetCount(std::string_view a_runtime) const;

	private:
		ShaderDB() = default;

		void LoadDirectory(std::string_view a_runtimeName, std::string_view a_stageType, std::string_view a_dirPath);
		static ShaderDef ParseFile(std::string_view a_filePath);

		// runtimeName → stageType → [ShaderDef]
		std::map<std::string, std::map<std::string, std::vector<ShaderDef>>> entries;
	};
}
