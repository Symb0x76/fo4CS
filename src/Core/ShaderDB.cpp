#include "Core/ShaderDB.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "Core/ShaderCache.h"

namespace CommunityShaders
{
	namespace
	{
		// Trim whitespace from both ends
		std::string_view Trim(std::string_view s)
		{
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
				s.remove_prefix(1);
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
				s.remove_suffix(1);
			return s;
		}

		// Remove surrounding parentheses, e.g. "(4684)" → "4684"
		std::string_view UnwrapParens(std::string_view s)
		{
			s = Trim(s);
			if (!s.empty() && s.front() == '(')
				s.remove_prefix(1);
			if (!s.empty() && s.back() == ')')
				s.remove_suffix(1);
			return s;
		}

		// Parse a CSV string like "0,1,2,12,15" into a vector
		std::vector<int> ParseIntCsv(std::string_view s)
		{
			std::vector<int> result;
			s = Trim(s);
			if (s.empty())
				return result;

			while (!s.empty()) {
				auto comma = s.find(',');
				auto token = Trim(s.substr(0, comma));
				if (!token.empty())
					result.push_back(std::atoi(token.data()));
				if (comma == std::string_view::npos)
					break;
				s.remove_prefix(comma + 1);
			}
			return result;
		}

		// Parse "dim@slot,dim@slot" → vector of (dim, slot) pairs
		std::vector<std::pair<int, int>> ParseDimSlotCsv(std::string_view s)
		{
			std::vector<std::pair<int, int>> result;
			s = Trim(s);
			if (s.empty())
				return result;

			while (!s.empty()) {
				auto comma = s.find(',');
				auto token = Trim(s.substr(0, comma));
				if (!token.empty()) {
					auto at = token.find('@');
					if (at != std::string_view::npos) {
						int dim = std::atoi(token.substr(0, at).data());
						int slot = std::atoi(token.substr(at + 1).data());
						result.emplace_back(dim, slot);
					}
				}
				if (comma == std::string_view::npos)
					break;
				s.remove_prefix(comma + 1);
			}
			return result;
		}

		// Parse "size@slot,size@slot" → map of slot → size
		std::map<int, int> ParseBufferSizes(std::string_view s)
		{
			std::map<int, int> result;
			s = Trim(s);
			if (s.empty())
				return result;

			while (!s.empty()) {
				auto comma = s.find(',');
				auto token = Trim(s.substr(0, comma));
				if (!token.empty()) {
					auto at = token.find('@');
					if (at != std::string_view::npos) {
						int size = std::atoi(token.substr(0, at).data());
						int slot = std::atoi(token.substr(at + 1).data());
						result[slot] = size;
					}
				}
				if (comma == std::string_view::npos)
					break;
				s.remove_prefix(comma + 1);
			}
			return result;
		}

		// Parse hex or decimal integer: "0x9007" or "7"
		std::uint32_t ParseU32(std::string_view s)
		{
			s = Trim(s);
			if (s.empty())
				return 0;

			if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
				return static_cast<std::uint32_t>(std::strtoul(s.data(), nullptr, 16));

			return static_cast<std::uint32_t>(std::atoi(s.data()));
		}

		// INI-style key=value line parser.
		// Returns (key, value) or empty pair on comment/blank/section.
		std::pair<std::string_view, std::string_view> ParseIniLine(std::string_view line)
		{
			line = Trim(line);
			if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[')
				return {};

			auto eq = line.find('=');
			if (eq == std::string_view::npos)
				return {};

			return { Trim(line.substr(0, eq)), Trim(line.substr(eq + 1)) };
		}

		std::string_view StageTypeFromFileName(std::string_view fileName)
		{
			// fileName is like "PS0017F01CI0O6.txt" → "PS"
			if (fileName.size() >= 2)
				return fileName.substr(0, 2);
			return {};
		}
	}

	ShaderDB* ShaderDB::GetSingleton()
	{
		static ShaderDB instance;
		return &instance;
	}

	ShaderDef ShaderDB::ParseFile(std::string_view a_filePath)
	{
		ShaderDef def;
		std::ifstream file(a_filePath.data());
		if (!file.is_open())
			return def;

		std::string line;
		bool inSection = false;
		while (std::getline(file, line)) {
			line = std::string(Trim(line));
			if (line.empty())
				continue;

			// Section header: [pixelHunt] or [vertexHunt]
			if (line.front() == '[') {
				inSection = (line.find("pixelHunt") != std::string::npos ||
					line.find("vertexHunt") != std::string::npos ||
					line.find("computeHunt") != std::string::npos ||
					line.find("geometryHunt") != std::string::npos ||
					line.find("hullHunt") != std::string::npos ||
					line.find("domainHunt") != std::string::npos ||
					line.find("shaderDump") != std::string::npos);
				continue;
			}

			// Closing tag: [/pixelHunt] etc.
			if (line.front() == '/' || line == "[/pixelHunt]" || line == "[/vertexHunt]" || line == "[/shaderDump]") {
				inSection = false;
				continue;
			}

			if (!inSection)
				continue;

			auto [key, value] = ParseIniLine(line);
			if (key.empty())
				continue;

			if (key == "shaderUID" || key == "uid")
				def.uid = value;
			else if (key == "type")
				def.type = value;
			else if (key == "hash")
				def.hash = ParseU32(value);
			else if (key == "asmHash")
				def.asmHash = ParseU32(value);
			else if (key == "shader") {
				// Strip leading ';' if present (unset replacement indicator)
				if (!value.empty() && value.front() == ';')
					value.remove_prefix(1);
				def.shader = Trim(value);
			}
			else if (key == "active")
				def.active = (value == "true");
			else if (key == "priority")
				def.priority = std::atoi(value.data());
			else if (key == "size")
				def.size = ParseU32(UnwrapParens(value));
			else if (key == "buffersize")
				def.bufferSizes = ParseBufferSizes(value);
			else if (key == "textures")
				def.textureSlots = ParseIntCsv(value);
			else if (key == "textureDimensions")
				def.textureDimensions = ParseDimSlotCsv(value);
			else if (key == "textureSlotMask")
				def.textureSlotMask = ParseU32(value);
			else if (key == "textureDimensionMask")
				def.textureDimensionMask = ParseU32(value);
			else if (key == "inputTextureCount")
				def.inputTextureCount = ParseU32(UnwrapParens(value));
			else if (key == "inputcount")
				def.inputCount = ParseU32(UnwrapParens(value));
			else if (key == "inputMask")
				def.inputMask = ParseU32(value);
			else if (key == "outputcount")
				def.outputCount = ParseU32(UnwrapParens(value));
			else if (key == "outputMask")
				def.outputMask = ParseU32(value);
		}

		// If no type was set, try to infer from UID or file name
		if (def.type.empty() && !def.uid.empty())
			def.type = def.uid.substr(0, 2);

		return def;
	}

	void ShaderDB::LoadDirectory(std::string_view a_runtimeName, std::string_view a_stageType, std::string_view a_dirPath)
	{
		std::error_code ec;
		if (!std::filesystem::exists(a_dirPath, ec) || !std::filesystem::is_directory(a_dirPath, ec))
			return;

		std::string runtime(a_runtimeName);
		std::string stage(a_stageType);
		auto& stageEntries = entries[runtime][stage];

		for (const auto& entry : std::filesystem::directory_iterator(a_dirPath, ec)) {
			if (!entry.is_directory())
				continue;

			auto shaderDir = entry.path();
			auto uid = shaderDir.filename().string();

			// Look for .txt file inside: {uid}.txt
			auto txtPath = shaderDir / (uid + ".txt");
			if (!std::filesystem::exists(txtPath, ec))
				continue;

			auto def = ParseFile(txtPath.string());
			if (def.uid.empty())
				def.uid = uid;

			if (def.type.empty())
				def.type = a_stageType;

			stageEntries.push_back(std::move(def));
		}

		logger::info("[ShaderDB] Loaded {} {} shaders for runtime {}", stageEntries.size(), stage, runtime);
	}

	void ShaderDB::Load(std::string_view a_runtimeName, std::string_view a_dbPath)
	{
		std::string dbPath(a_dbPath);
		std::error_code ec;

		if (!std::filesystem::exists(dbPath, ec) || !std::filesystem::is_directory(dbPath, ec)) {
			logger::warn("[ShaderDB] ShaderDB path does not exist: {}", dbPath);
			return;
		}

		// Load each stage subdirectory
		static constexpr std::pair<std::string_view, std::string_view> kStages[] = {
			{ "PS", "Pixel" },
			{ "VS", "Vertex" },
			{ "CS", "Compute" },
			{ "GS", "Geometry" },
			{ "HS", "Hull" },
			{ "DS", "Domain" },
		};

		for (auto [stageType, dirName] : kStages) {
			auto stagePath = std::filesystem::path(dbPath) / std::string(dirName);
			if (std::filesystem::exists(stagePath, ec) && std::filesystem::is_directory(stagePath, ec))
				LoadDirectory(a_runtimeName, stageType, stagePath.string());
		}
	}

	void ShaderDB::Clear()
	{
		entries.clear();
	}

	const ShaderDef* ShaderDB::Find(std::string_view a_runtime, std::string_view a_uid) const
	{
		auto runtimeIt = entries.find(std::string(a_runtime));
		if (runtimeIt == entries.end())
			return nullptr;

		for (const auto& [stageType, defs] : runtimeIt->second) {
			for (const auto& def : defs) {
				if (def.uid == a_uid && def.active)
					return &def;
			}
		}
		return nullptr;
	}

	const ShaderDef* ShaderDB::FindByHash(std::string_view a_runtime, std::string_view a_stageType, std::uint32_t a_asmHash) const
	{
		auto runtimeIt = entries.find(std::string(a_runtime));
		if (runtimeIt == entries.end())
			return nullptr;

		auto stageIt = runtimeIt->second.find(std::string(a_stageType));
		if (stageIt == runtimeIt->second.end())
			return nullptr;

		for (const auto& def : stageIt->second) {
			if (def.asmHash == a_asmHash && def.active)
				return &def;
		}
		return nullptr;
	}

	size_t ShaderDB::GetCount(std::string_view a_runtime) const
	{
		auto runtimeIt = entries.find(std::string(a_runtime));
		if (runtimeIt == entries.end())
			return 0;

		size_t count = 0;
		for (const auto& [stageType, defs] : runtimeIt->second)
			count += defs.size();
		return count;
	}
}
