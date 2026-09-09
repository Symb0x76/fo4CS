#pragma once

// Internal helpers shared by the ShaderCache translation units under
// src/Core/Shaders/. These were an anonymous namespace inside ShaderCache.cpp;
// splitting that file means they need external linkage, so they live in
// CommunityShaders::shadercache and this header stays private to src/Core.
//
// Each predicate owns a one-shot `static const bool` cache (some of which log on
// first call), so every one of them must have exactly one definition. Do not
// make any of them inline or move a body into this header.

#include "Core/ShaderCache.h"

#include <RE/FO4Runtime.h>

#include <d3dcommon.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommunityShaders::shadercache
{
		namespace F4Runtime = RE::FO4Runtime;
		constexpr std::int32_t kPreNGBSLightingShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::BS_LIGHTING_SHADER_TYPE);
#if defined(FALLOUT_PRE_NG)
		constexpr std::int32_t kPreNGDFLightingShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::DF_LIGHTING_SHADER_TYPE);
		constexpr std::int32_t kPreNGDFCompositeShaderType = static_cast<std::int32_t>(F4Runtime::PreNG::DF_COMPOSITE_SHADER_TYPE);
		constexpr const char* kPreNGBSLightingContractCompileEnv = "FO4CS_LLF_PRENG_BSLIGHTING_CONTRACT_COMPILE";
		constexpr const char* kPreNGBSLightingConsumerCompileEnv = "FO4CS_LLF_PRENG_BSLIGHTING_CONSUMER_COMPILE";
		constexpr const char* kPreNGBSLightingLLFBindEnv = "FO4CS_LLF_PRENG_BSLIGHTING_LLF_BIND";
		constexpr const char* kPreNGDFLightFullShadowedDescriptorConsumerEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER";
		constexpr const char* kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_SHADOWED_DESCRIPTOR_CONSUMER_UNSAFE";
		constexpr const char* kPreNGDFLightFullContractVisibleLLFEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_VISIBLE_LLF";
		constexpr const char* kPreNGDFLightFullContractVisibleMaxLightsEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_VISIBLE_MAX_LIGHTS";
		constexpr const char* kPreNGDFLightFullContractVisibleStrictMaxLightsEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_VISIBLE_STRICT_MAX_LIGHTS";
		constexpr const char* kPreNGDFLightFullContractVisibleClusterMaxLightsEnv = "FO4CS_LLF_PRENG_DFLIGHT_FULL_CONTRACT_VISIBLE_CLUSTER_MAX_LIGHTS";
		constexpr const char* kPreNGDFCompositeDescriptorCompileEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_DESCRIPTOR_COMPILE";
		constexpr const char* kPreNGDFCompositeSafeBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_SAFE_BIND";
		constexpr const char* kPreNGDFCompositeFogSafeBindEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_FOG_SAFE_BIND";
		constexpr const char* kPreNGDFCompositeVisibleLLFEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_VISIBLE_LLF";
		constexpr const char* kPreNGDFCompositeVisibleLLFScale1024Env = "FO4CS_LLF_PRENG_DFCOMPOSITE_VISIBLE_LLF_SCALE_1024";
		constexpr const char* kPreNGDFCompositeVisibleLLFMaxLightsEnv = "FO4CS_LLF_PRENG_DFCOMPOSITE_VISIBLE_LLF_MAX_LIGHTS";
#endif
#if !defined(FALLOUT_PRE_NG)
		constexpr const char* kPostNGBSLightingDescriptorObserveEnv = "FO4CS_LLF_POSTNG_BSLIGHTING_DESCRIPTOR_OBSERVE";
		constexpr const char* kPostNGBSLightingLLFBindEnv = "FO4CS_LLF_POSTNG_BSLIGHTING_LLF_BIND";
#endif
		constexpr std::string_view kPreNGBSLightingContractProbeSource = "LightLimitFix/BSLightingContractProbePS.hlsl";
		constexpr std::string_view kPreNGBSLightingConsumerProbeSource = "LightLimitFix/BSLightingLLFConsumerProbePS.hlsl";
		constexpr std::string_view kPreNGBSLightingLLFConsumerVisibleSource = "LightLimitFix/BSLightingLLFConsumerPS.hlsl";
		constexpr std::string_view kPreNGDFLightFullContractDescriptorSource = "LightLimitFix/DFLightFullContractPS.hlsl";
		constexpr std::string_view kPreNGDFLightFullShadowedDescriptorSource = "LightLimitFix/DFLightFullShadowedPS.hlsl";
		constexpr std::string_view kPreNGDFLightForwardConsumerSource = "LightLimitFix/DFLightForwardConsumerPS.hlsl";
		constexpr std::string_view kPreNGDFLightForwardZeroSource = "LightLimitFix/DFLightZeroOutputPS.hlsl";
		constexpr const char* kPreNGDFLightForwardLLFBindEnv = "FO4CS_LLF_PRENG_DFLIGHT_FORWARD_LLF_BIND";
		constexpr std::string_view kPreNGDFCompositeDescriptorProbeSource = "LightLimitFix/DFCompositeContractProbePS.hlsl";
		constexpr std::string_view kPreNGDFCompositeVanilla40Source = "LightLimitFix/DFCompositeVanilla40PS.hlsl";
		constexpr std::string_view kPreNGDFCompositeVanilla88Source = "LightLimitFix/DFCompositeVanilla88PS.hlsl";
		constexpr std::string_view kPreNGDFCompositeVanilla10040Source = "LightLimitFix/DFCompositeVanilla10040PS.hlsl";
		constexpr std::string_view kPreNGDFCompositeVanilla10088Source = "LightLimitFix/DFCompositeVanilla10088PS.hlsl";

		struct DescriptorDefineSet
		{
			std::vector<std::pair<std::string, std::string>> storage;
			std::vector<D3D_SHADER_MACRO> macros;
		};

#if defined(FALLOUT_POST_NG)
		REX::W32::ID3D11VertexShader* ToREVertexShader(ID3D11VertexShader* a_shader);
		REX::W32::ID3D11PixelShader* ToREPixelShader(ID3D11PixelShader* a_shader);
#else
		ID3D11VertexShader* ToREVertexShader(ID3D11VertexShader* a_shader);
		ID3D11PixelShader* ToREPixelShader(ID3D11PixelShader* a_shader);
#endif

		bool ReadDescriptorEnvironmentSwitch(const char* a_name);
		std::uint32_t ReadDescriptorEnvironmentUInt(
			const char* a_name,
			std::uint32_t a_defaultValue,
			std::uint32_t a_minValue,
			std::uint32_t a_maxValue);
		bool ReadDescriptorCompileSwitch();
		bool ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer();
		bool ShouldEnablePreNGDFLightFullContractVisibleLLF();
		bool ShouldEnablePreNGDFLightForwardVisibleLLF();
		std::uint32_t GetPreNGDFLightFullContractVisibleMaxLights();
		std::uint32_t GetPreNGDFLightFullContractVisibleStrictMaxLights();
		std::uint32_t GetPreNGDFLightFullContractVisibleClusterMaxLights();
		bool IsPreNGDFCompositeVisibleLLFDescriptor(std::uint32_t a_descriptor);
		bool ShouldEnablePreNGDFCompositeVisibleLLF();
		std::uint32_t GetPreNGDFCompositeVisibleLLFScale1024();
		std::uint32_t GetPreNGDFCompositeVisibleLLFMaxLights();

		std::string ToLowerAscii(std::string a_value);
		bool StartsWith(std::string_view a_value, std::string_view a_prefix) noexcept;
		bool IsPreNGDFLightFxpName(std::string_view a_normalizedFxpFilename);
		bool IsPreNGBSLightingFxpName(std::string_view a_normalizedFxpFilename);
		bool IsPreNGDFCompositeFxpName(std::string_view a_normalizedFxpFilename);
		// The PreNG DFLight forward visible-consumer descriptor.
		//
		// Deliberately callable on every runtime, returning a hard false off PreNG.
		// The shader-type constant and the descriptor predicate behind it are both
		// PreNG-only, and this same five-term test is needed from two all-runtime
		// boolean chains -- CanCompileDescriptorShader and the ShaderCache source
		// override. Naming it once keeps the #if in the definition instead of
		// leaking into the middle of those chains, which is how PostNG and PostAE
		// came to reference PreNG-only symbols and stopped compiling.
		bool IsPreNGDFLightForwardConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);

		bool IsPreNGDFLightFullShadowedDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool IsPreNGBSLightingContractPixelDescriptor(std::uint32_t a_descriptor);
		bool IsPreNGBSLightingContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool ShouldCompilePreNGBSLightingContractShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool ShouldCompilePreNGBSLightingConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool ShouldBindPreNGBSLightingLLFVisibleConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool IsPreNGDFLightFullContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool IsPreNGDFCompositeContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool ShouldCompilePreNGDFCompositeDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool ShouldEnablePreNGDFCompositeFogSafeBindShader();
		bool ShouldUsePreNGDFCompositeVanilla40Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool ShouldUsePreNGDFCompositeVanilla88Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool ShouldUsePreNGDFCompositeVanilla10040Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool ShouldUsePreNGDFCompositeVanilla10088Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool ShouldUsePreNGDFCompositeVanillaSafeBindShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool IsPreNGDFLightLLFConsumerDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool CanActivelyCompilePreNGLightingDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		bool ShouldCompilePostNGBSLightingConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType);
		bool CanCompileDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor);
		std::string BuildFeatureDefineList(const std::vector<D3D_SHADER_MACRO>& a_defines);
		DescriptorDefineSet BuildDescriptorDefineSet(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::uint32_t a_descriptor);
		std::string_view GetDescriptorTarget(ShaderStage a_stage) noexcept;
}
