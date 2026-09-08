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
	void ShaderCache::LogDescriptorBridgeHeld(
		ShaderStage a_stage,
		const RE::BSShader& a_shader,
		std::uint32_t a_descriptor,
		std::string_view a_operation)
	{
		const auto state = GetDescriptorShaderState(a_stage, a_shader, a_descriptor);
		const auto normalizedFxp = state ? state->fxpFilename : NormalizeFxpFilename(a_shader.fxpFilename);
		const auto heldKey = std::format(
			"{}:{}:{}:{:08X}:{}",
			GetStageName(a_stage),
			a_shader.shaderType,
			normalizedFxp,
			a_descriptor,
			a_operation);

		{
			std::scoped_lock lock(descriptorLock);
			if (!descriptorHeldLogs.insert(heldKey).second) {
				return;
			}
		}

		logger::info(
			"[ShaderCache] FO4 descriptor shader cache {} held shaderType={} fxp={} stage={} descriptor=0x{:X} descriptorBridge=available descriptorCache={} hits={} customCompile=held customBind=held",
			a_operation,
			a_shader.shaderType,
			normalizedFxp,
			GetStageName(a_stage),
			a_descriptor,
			GetDescriptorCacheState(state),
			state ? state->hits : 0);
	}

	void ShaderCache::LogDescriptorCompileEvent(
		ShaderStage a_stage,
		const RE::BSShader& a_shader,
		std::uint32_t a_descriptor,
		std::string_view a_operation,
		std::string_view a_compileState,
		std::string_view a_reason,
		std::string_view a_extra)
	{
		const auto state = GetDescriptorShaderState(a_stage, a_shader, a_descriptor);
		const auto normalizedFxp = state ? state->fxpFilename : NormalizeFxpFilename(a_shader.fxpFilename);
		const auto logKey = std::format(
			"{}:{}:{}:{:08X}:{}:{}:{}:{}",
			GetStageName(a_stage),
			a_shader.shaderType,
			normalizedFxp,
			a_descriptor,
			a_operation,
			a_compileState,
			a_reason,
			a_extra);

		{
			std::scoped_lock lock(descriptorLock);
			if (!descriptorCompileLogs.insert(logKey).second) {
				return;
			}
		}

		const auto descriptorState = GetDescriptorCacheState(state);
		const auto hits = state ? state->hits : 0;
		if (a_extra.empty()) {
			logger::info(
				"[ShaderCache] FO4 descriptor shader cache {} shaderType={} fxp={} stage={} descriptor=0x{:X} descriptorBridge=available descriptorCache={} hits={} customCompile={} customBind=held reason={}",
				a_operation,
				a_shader.shaderType,
				normalizedFxp,
				GetStageName(a_stage),
				a_descriptor,
				descriptorState,
				hits,
				a_compileState,
				a_reason);
			return;
		}

		logger::info(
			"[ShaderCache] FO4 descriptor shader cache {} shaderType={} fxp={} stage={} descriptor=0x{:X} descriptorBridge=available descriptorCache={} hits={} customCompile={} customBind=held reason={} {}",
			a_operation,
			a_shader.shaderType,
			normalizedFxp,
			GetStageName(a_stage),
			a_descriptor,
			descriptorState,
			hits,
			a_compileState,
			a_reason,
			a_extra);
	}
}
