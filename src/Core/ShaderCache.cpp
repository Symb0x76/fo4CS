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

#include "Core/Shaders/ShaderCacheInternal.h"

namespace CommunityShaders
{
	// The descriptor gates and switch readers moved to src/Core/Shaders/; they were
	// an anonymous namespace in this file, so the call sites below are unchanged.
	using namespace shadercache;

	ShaderCache* ShaderCache::GetSingleton()
	{
		static ShaderCache singleton;
		return std::addressof(singleton);
	}

	std::string ShaderCache::NormalizeFxpFilename(std::string_view a_fxpFilename)
	{
		if (a_fxpFilename.empty()) {
			return "<empty>";
		}

		std::string normalized{ a_fxpFilename };
		std::ranges::transform(normalized, normalized.begin(), [](unsigned char a_ch) {
			if (a_ch == '\\') {
				return '/';
			}
			return static_cast<char>(std::tolower(a_ch));
		});
		return normalized;
	}

	std::string ShaderCache::NormalizeFxpFilename(const char* a_fxpFilename)
	{
		return a_fxpFilename ? NormalizeFxpFilename(std::string_view{ a_fxpFilename }) : "<null>";
	}

	const char* ShaderCache::GetDescriptorCacheState(const std::optional<DescriptorShaderState>& a_state) noexcept
	{
		if (!a_state) {
			return "missing";
		}

		return a_state->found ? "vanilla-observed" : "miss-observed";
	}

	ShaderCache::DescriptorShaderKey ShaderCache::MakeDescriptorShaderKey(
		ShaderStage a_stage,
		const RE::BSShader& a_shader,
		std::uint32_t a_descriptor)
	{
		return {
			a_stage,
			a_shader.shaderType,
			a_descriptor,
			NormalizeFxpFilename(a_shader.fxpFilename)
		};
	}

	bool ShaderCache::ShouldCompileDescriptorShaders()
	{
#if defined(FALLOUT_PRE_NG)
		static const bool enabled = ReadDescriptorCompileSwitch();
		return enabled;
#else
		return false;
#endif
	}

	std::optional<std::string> ShaderCache::ResolveDescriptorShaderSource(const DescriptorShaderKey& a_key)
	{
#if !defined(FALLOUT_PRE_NG)
		if (ShouldCompilePostNGBSLightingConsumerShader(a_key.stage, a_key.shaderType)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGBSLightingLLFConsumerVisibleSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGBSLightingLLFConsumerVisibleSource };
			}
			return std::nullopt;
		}
#endif
		// Visible LLF consumer takes precedence over the compile-only probe when
		// FO4CS_LLF_PRENG_BSLIGHTING_LLF_BIND is enabled. The probe path stays as
		// the fallback for compile/observe profiles.
		if (ShouldBindPreNGBSLightingLLFVisibleConsumerShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGBSLightingLLFConsumerVisibleSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGBSLightingLLFConsumerVisibleSource };
			}
			return std::nullopt;
		}

		if (ShouldCompilePreNGBSLightingConsumerShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGBSLightingConsumerProbeSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGBSLightingConsumerProbeSource };
			}
			return std::nullopt;
		}

		if (ShouldCompilePreNGBSLightingContractShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGBSLightingContractProbeSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGBSLightingContractProbeSource };
			}
			return std::nullopt;
		}

		if (IsPreNGDFLightForwardConsumerShader(
			    a_key.stage,
			    a_key.shaderType,
			    a_key.fxpFilename,
			    a_key.descriptor)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGDFLightForwardConsumerSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGDFLightForwardConsumerSource };
			}
			return std::nullopt;
		}

		if (IsPreNGDFLightFullContractDescriptorShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGDFLightFullContractDescriptorSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGDFLightFullContractDescriptorSource };
			}
			return std::nullopt;
		}

		if (IsPreNGDFLightFullShadowedDescriptorShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(kPreNGDFLightFullShadowedDescriptorSource);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ kPreNGDFLightFullShadowedDescriptorSource };
			}
			return std::nullopt;
		}

		if (IsPreNGDFCompositeContractDescriptorShader(a_key.stage, a_key.shaderType, a_key.fxpFilename, a_key.descriptor)) {
			std::string_view sourcePath = kPreNGDFCompositeDescriptorProbeSource;
			if (ShouldUsePreNGDFCompositeVanilla40Shader(
					a_key.stage,
					a_key.shaderType,
					a_key.fxpFilename,
					a_key.descriptor)) {
				sourcePath = kPreNGDFCompositeVanilla40Source;
			} else if (ShouldUsePreNGDFCompositeVanilla88Shader(
					a_key.stage,
					a_key.shaderType,
					a_key.fxpFilename,
					a_key.descriptor)) {
				sourcePath = kPreNGDFCompositeVanilla88Source;
			} else if (ShouldUsePreNGDFCompositeVanilla10040Shader(
						   a_key.stage,
						   a_key.shaderType,
						   a_key.fxpFilename,
						   a_key.descriptor)) {
				sourcePath = kPreNGDFCompositeVanilla10040Source;
			} else if (ShouldUsePreNGDFCompositeVanilla10088Shader(
						   a_key.stage,
						   a_key.shaderType,
						   a_key.fxpFilename,
						   a_key.descriptor)) {
				sourcePath = kPreNGDFCompositeVanilla10088Source;
			}
			const auto diskPath = std::filesystem::path("Data\\Shaders") / std::filesystem::path(sourcePath);
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return std::string{ sourcePath };
			}
			return std::nullopt;
		}

		auto source = a_key.fxpFilename;
		if (source.empty() || source.front() == '<' || source.find('<') != std::string::npos) {
			return std::nullopt;
		}

		if (StartsWith(source, "data/shaders/")) {
			source.erase(0, std::string_view{ "data/shaders/" }.size());
		} else if (StartsWith(source, "shaders/")) {
			source.erase(0, std::string_view{ "shaders/" }.size());
		}

		std::filesystem::path sourcePath{ source };
		const auto extension = ToLowerAscii(sourcePath.extension().string());
		if (extension == ".fxp" || extension == ".fx") {
			sourcePath.replace_extension(".hlsl");
		} else if (extension.empty()) {
			sourcePath += ".hlsl";
		} else if (extension != ".hlsl") {
			return std::nullopt;
		}

		std::vector<std::filesystem::path> candidates;
		candidates.push_back(sourcePath);
		if (sourcePath.has_parent_path()) {
			candidates.push_back(sourcePath.filename());
		}

		for (const auto& candidate : candidates) {
			const auto diskPath = std::filesystem::path("Data\\Shaders") / candidate;
			std::error_code ec;
			if (std::filesystem::exists(diskPath, ec) && std::filesystem::is_regular_file(diskPath, ec)) {
				return candidate.generic_string();
			}
		}

		return std::nullopt;
	}

	void ShaderCache::ObserveDescriptorShader(
		ShaderStage a_stage,
		const RE::BSShader& a_shader,
		std::uint32_t a_descriptor,
		std::string_view a_fxpFilename,
		bool a_found,
		std::uintptr_t a_shaderEntry,
		std::uintptr_t a_d3dObject)
	{
		const auto normalizedFxp = NormalizeFxpFilename(a_fxpFilename);
		const DescriptorShaderKey key{ a_stage, a_shader.shaderType, a_descriptor, normalizedFxp };

		std::scoped_lock lock(descriptorLock);
		auto [it, inserted] = descriptorShaders.try_emplace(key);
		auto& state = it->second;
		if (inserted) {
			state.stage = a_stage;
			state.shaderType = a_shader.shaderType;
			state.descriptor = a_descriptor;
			state.fxpFilename = normalizedFxp;
		}

		state.found = state.found || a_found;
		if (a_shaderEntry != 0) {
			state.shaderEntry = a_shaderEntry;
		}
		if (a_d3dObject != 0) {
			state.d3dObject = a_d3dObject;
		}
		++state.hits;
	}

	std::optional<ShaderCache::DescriptorShaderState> ShaderCache::GetDescriptorShaderState(
		ShaderStage a_stage,
		const RE::BSShader& a_shader,
		std::uint32_t a_descriptor) const
	{
		return GetDescriptorShaderState(a_stage, a_shader.shaderType, a_descriptor, NormalizeFxpFilename(a_shader.fxpFilename));
	}

	std::optional<ShaderCache::DescriptorShaderState> ShaderCache::GetDescriptorShaderState(
		ShaderStage a_stage,
		std::int32_t a_shaderType,
		std::uint32_t a_descriptor,
		std::string_view a_fxpFilename) const
	{
		const DescriptorShaderKey key{ a_stage, a_shaderType, a_descriptor, NormalizeFxpFilename(a_fxpFilename) };
		std::scoped_lock lock(descriptorLock);
		if (const auto it = descriptorShaders.find(key); it != descriptorShaders.end()) {
			return it->second;
		}

		return std::nullopt;
	}

	RE::BSGraphics::VertexShader* ShaderCache::GetVertexShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor)
	{
		const auto key = MakeDescriptorShaderKey(ShaderStage::Vertex, a_shader, a_descriptor);
		RE::BSGraphics::VertexShader* cachedEntry = nullptr;
		{
			std::scoped_lock lock(descriptorLock);
			if (const auto it = descriptorVertexShaders.find(key); it != descriptorVertexShaders.end()) {
				cachedEntry = std::addressof(it->second->entry);
			}
		}
		if (cachedEntry) {
			LogDescriptorCompileEvent(ShaderStage::Vertex, a_shader, a_descriptor, "GetVertexShader", "cache-hit", "owned-entry");
			return cachedEntry;
		}

		if (!CanCompileDescriptorShader(ShaderStage::Vertex, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(
				ShaderStage::Vertex,
				a_shader,
				a_descriptor,
				"GetVertexShader",
				"gated",
				"FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE-off");
			return nullptr;
		}

		return MakeAndAddVertexShader(a_shader, a_descriptor);
	}

	RE::BSGraphics::PixelShader* ShaderCache::GetPixelShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor)
	{
		const auto key = MakeDescriptorShaderKey(ShaderStage::Pixel, a_shader, a_descriptor);
		RE::BSGraphics::PixelShader* cachedEntry = nullptr;
		{
			std::scoped_lock lock(descriptorLock);
			if (const auto it = descriptorPixelShaders.find(key); it != descriptorPixelShaders.end()) {
				cachedEntry = std::addressof(it->second->entry);
			}
		}
		if (cachedEntry) {
			LogDescriptorCompileEvent(ShaderStage::Pixel, a_shader, a_descriptor, "GetPixelShader", "cache-hit", "owned-entry");
			return cachedEntry;
		}

		if (!CanCompileDescriptorShader(ShaderStage::Pixel, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(
				ShaderStage::Pixel,
				a_shader,
				a_descriptor,
				"GetPixelShader",
				"gated",
				"FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE-off");
			return nullptr;
		}

		return MakeAndAddPixelShader(a_shader, a_descriptor);
	}

	RE::BSGraphics::VertexShader* ShaderCache::MakeAndAddVertexShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor)
	{
		constexpr auto stage = ShaderStage::Vertex;
		const auto key = MakeDescriptorShaderKey(stage, a_shader, a_descriptor);

		if (!CanCompileDescriptorShader(stage, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddVertexShader", "gated", "FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE-off");
			return nullptr;
		}

		if (!CanActivelyCompilePreNGLightingDescriptorShader(stage, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddVertexShader", "skipped", "unsupported-shaderType");
			return nullptr;
		}

		RE::BSGraphics::VertexShader* cachedEntry = nullptr;
		{
			std::scoped_lock lock(descriptorLock);
			if (const auto it = descriptorVertexShaders.find(key); it != descriptorVertexShaders.end()) {
				cachedEntry = std::addressof(it->second->entry);
			}
		}
		if (cachedEntry) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddVertexShader", "cache-hit", "owned-entry");
			return cachedEntry;
		}

		auto* device = Runtime::GetSingleton()->GetDevice();
		if (!device) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddVertexShader", "failed", "device-unavailable");
			return nullptr;
		}

		const auto source = ResolveDescriptorShaderSource(key);
		if (!source) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddVertexShader", "failed", "source-unresolved");
			return nullptr;
		}

		auto defines = BuildDescriptorDefineSet(stage, a_shader.shaderType, a_descriptor);
		const auto defineList = BuildFeatureDefineList(defines.macros);
		auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(*source, GetDescriptorTarget(stage), defines.macros.data(), "main");
		if (!bytecode) {
			LogDescriptorCompileEvent(
				stage,
				a_shader,
				a_descriptor,
				"MakeAndAddVertexShader",
				"failed",
				"compile-failed",
				std::format("source={} target={} defines={}", *source, GetDescriptorTarget(stage), defineList));
			return nullptr;
		}

		ID3D11VertexShader* shader = nullptr;
		const auto hr = device->CreateVertexShader(bytecode->data(), bytecode->size(), nullptr, &shader);
		if (FAILED(hr)) {
			LogDescriptorCompileEvent(
				stage,
				a_shader,
				a_descriptor,
				"MakeAndAddVertexShader",
				"failed",
				"CreateVertexShader-failed",
				std::format("source={} target={} hr=0x{:08X}", *source, GetDescriptorTarget(stage), static_cast<std::uint32_t>(hr)));
			return nullptr;
		}

		auto owned = std::make_unique<OwnedDescriptorVertexShader>();
		owned->d3dShader.attach(shader);
		owned->bytecode = std::move(*bytecode);
		owned->entry.id = a_descriptor;
		owned->entry.shader = ToREVertexShader(owned->d3dShader.get());
		owned->entry.byteCodeSize = static_cast<std::uint32_t>(owned->bytecode.size());
		owned->entry.shaderDesc = a_descriptor;

		RE::BSGraphics::VertexShader* entry = nullptr;
		bool inserted = false;
		{
			std::scoped_lock lock(descriptorLock);
			auto [it, wasInserted] = descriptorVertexShaders.try_emplace(key, std::move(owned));
			inserted = wasInserted;
			entry = std::addressof(it->second->entry);
		}
		LogDescriptorCompileEvent(
			stage,
			a_shader,
			a_descriptor,
			"MakeAndAddVertexShader",
			inserted ? "created" : "cache-hit",
			inserted ? "owned-entry-created" : "owned-entry-created-by-peer",
			std::format("source={} target={} bytecode={} defines={}", *source, GetDescriptorTarget(stage), entry->byteCodeSize, defineList));

		return entry;
	}

	RE::BSGraphics::PixelShader* ShaderCache::MakeAndAddPixelShader(const RE::BSShader& a_shader, std::uint32_t a_descriptor)
	{
		constexpr auto stage = ShaderStage::Pixel;
		const auto key = MakeDescriptorShaderKey(stage, a_shader, a_descriptor);

		if (!CanCompileDescriptorShader(stage, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddPixelShader", "gated", "FO4CS_LLF_PRENG_DESCRIPTOR_COMPILE-off");
			return nullptr;
		}

		if (!CanActivelyCompilePreNGLightingDescriptorShader(stage, a_shader.shaderType, key.fxpFilename, a_descriptor)) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddPixelShader", "skipped", "unsupported-shaderType");
			return nullptr;
		}

		RE::BSGraphics::PixelShader* cachedEntry = nullptr;
		{
			std::scoped_lock lock(descriptorLock);
			if (const auto it = descriptorPixelShaders.find(key); it != descriptorPixelShaders.end()) {
				cachedEntry = std::addressof(it->second->entry);
			}
		}
		if (cachedEntry) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddPixelShader", "cache-hit", "owned-entry");
			return cachedEntry;
		}

		auto* device = Runtime::GetSingleton()->GetDevice();
		if (!device) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddPixelShader", "failed", "device-unavailable");
			return nullptr;
		}

		const auto source = ResolveDescriptorShaderSource(key);
		if (!source) {
			LogDescriptorCompileEvent(stage, a_shader, a_descriptor, "MakeAndAddPixelShader", "failed", "source-unresolved");
			return nullptr;
		}

		auto defines = BuildDescriptorDefineSet(stage, a_shader.shaderType, a_descriptor);
		const auto defineList = BuildFeatureDefineList(defines.macros);
		auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(*source, GetDescriptorTarget(stage), defines.macros.data(), "main");
		if (!bytecode) {
			LogDescriptorCompileEvent(
				stage,
				a_shader,
				a_descriptor,
				"MakeAndAddPixelShader",
				"failed",
				"compile-failed",
				std::format("source={} target={} defines={}", *source, GetDescriptorTarget(stage), defineList));
			return nullptr;
		}

		ID3D11PixelShader* shader = nullptr;
		const auto hr = device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, &shader);
		if (FAILED(hr)) {
			LogDescriptorCompileEvent(
				stage,
				a_shader,
				a_descriptor,
				"MakeAndAddPixelShader",
				"failed",
				"CreatePixelShader-failed",
				std::format("source={} target={} hr=0x{:08X}", *source, GetDescriptorTarget(stage), static_cast<std::uint32_t>(hr)));
			return nullptr;
		}

		auto owned = std::make_unique<OwnedDescriptorPixelShader>();
		owned->d3dShader.attach(shader);
		owned->bytecode = std::move(*bytecode);
		owned->entry.id = a_descriptor;
		owned->entry.shader = ToREPixelShader(owned->d3dShader.get());
		if (const auto metadata = GetMetadataForBytecode(stage, owned->bytecode.data(), owned->bytecode.size())) {
			ObserveD3DShaderObject(stage, reinterpret_cast<std::uintptr_t>(owned->d3dShader.get()), *metadata);
		}

		RE::BSGraphics::PixelShader* entry = nullptr;
		std::size_t bytecodeSize = 0;
		bool inserted = false;
		{
			std::scoped_lock lock(descriptorLock);
			auto [it, wasInserted] = descriptorPixelShaders.try_emplace(key, std::move(owned));
			inserted = wasInserted;
			entry = std::addressof(it->second->entry);
			bytecodeSize = it->second->bytecode.size();
		}
		LogDescriptorCompileEvent(
			stage,
			a_shader,
			a_descriptor,
			"MakeAndAddPixelShader",
			inserted ? "created" : "cache-hit",
			inserted ? "owned-entry-created" : "owned-entry-created-by-peer",
			std::format("source={} target={} bytecode={} defines={}", *source, GetDescriptorTarget(stage), bytecodeSize, defineList));

		return entry;
	}
}
