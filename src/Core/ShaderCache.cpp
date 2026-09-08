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
	namespace
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

#if defined(FALLOUT_POST_NG)
		REX::W32::ID3D11VertexShader* ToREVertexShader(ID3D11VertexShader* a_shader)
		{
			return reinterpret_cast<REX::W32::ID3D11VertexShader*>(a_shader);
		}

		REX::W32::ID3D11PixelShader* ToREPixelShader(ID3D11PixelShader* a_shader)
		{
			return reinterpret_cast<REX::W32::ID3D11PixelShader*>(a_shader);
		}
#else
		ID3D11VertexShader* ToREVertexShader(ID3D11VertexShader* a_shader)
		{
			return a_shader;
		}

		ID3D11PixelShader* ToREPixelShader(ID3D11PixelShader* a_shader)
		{
			return a_shader;
		}
#endif

		bool ReadDescriptorEnvironmentSwitch(const char* a_name)
		{
			return DebugSwitches::ReadSwitchEnabled(a_name);
		}

		std::uint32_t ReadDescriptorEnvironmentUInt(
			const char* a_name,
			std::uint32_t a_defaultValue,
			std::uint32_t a_minValue,
			std::uint32_t a_maxValue)
		{
			return DebugSwitches::ReadUIntClamped(a_name, a_defaultValue, a_minValue, a_maxValue);
		}

		bool ReadDescriptorCompileSwitch()
		{
			return ReadDescriptorEnvironmentSwitch(PreNGEnvironment::kPreNGDescriptorCompileEnv);
		}

#if defined(FALLOUT_PRE_NG)
		bool ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer()
		{
			static const bool enabled = [] {
				const bool requested = ReadDescriptorEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerEnv);
				const bool unsafeOverride = ReadDescriptorEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
				if (requested && !unsafeOverride) {
					logger::warn(
						"[ShaderCache] PreNG DFLight full-shadowed descriptor compile held; DFLightFullShadowedPS is not vanilla-equivalent and can black out sky-light-only views. Set {}=1 only for focused diagnostics.",
						kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
				}
				return requested && unsafeOverride;
			}();
			return enabled;
		}

		bool ShouldEnablePreNGDFLightFullContractVisibleLLF()
		{
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFLightFullContractVisibleLLFEnv);
			return enabled;
		}

		bool ShouldEnablePreNGDFLightForwardVisibleLLF()
		{
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFLightForwardLLFBindEnv);
			return enabled;
		}

		std::uint32_t GetPreNGDFLightFullContractVisibleMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFLightFullContractVisibleMaxLightsEnv,
				16,
				1,
				64);
			return maxLights;
		}

		std::uint32_t GetPreNGDFLightFullContractVisibleStrictMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFLightFullContractVisibleStrictMaxLightsEnv,
				15,
				0,
				15);
			return maxLights;
		}

		std::uint32_t GetPreNGDFLightFullContractVisibleClusterMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFLightFullContractVisibleClusterMaxLightsEnv,
				64,
				0,
				64);
			return maxLights;
		}

		bool IsPreNGDFCompositeVisibleLLFDescriptor(std::uint32_t a_descriptor)
		{
			return a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88 ||
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088;
		}

		bool ShouldEnablePreNGDFCompositeVisibleLLF()
		{
			static const bool enabled = [] {
				const bool requested = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeVisibleLLFEnv);
				if (requested) {
					logger::warn(
						"[ShaderCache] PreNG DFComposite visible LLF proof active; this screen-space contribution is diagnostic-only and uses the low-risk 0x88/0x10088 DFComposite consumers.");
				}
				return requested;
			}();
			return enabled;
		}

		std::uint32_t GetPreNGDFCompositeVisibleLLFScale1024()
		{
			static const auto scale = ReadDescriptorEnvironmentUInt(
				kPreNGDFCompositeVisibleLLFScale1024Env,
				16,
				0,
				1024);
			return scale;
		}

		std::uint32_t GetPreNGDFCompositeVisibleLLFMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFCompositeVisibleLLFMaxLightsEnv,
				16,
				1,
				64);
			return maxLights;
		}
#endif

		std::string ToLowerAscii(std::string a_value)
		{
			std::ranges::transform(a_value, a_value.begin(), [](unsigned char a_ch) {
				return static_cast<char>(std::tolower(a_ch));
			});
			return a_value;
		}

		bool StartsWith(std::string_view a_value, std::string_view a_prefix) noexcept
		{
			return a_value.size() >= a_prefix.size() &&
			       a_value.substr(0, a_prefix.size()) == a_prefix;
		}

		bool IsPreNGDFLightFxpName(std::string_view a_normalizedFxpFilename)
		{
#if defined(FALLOUT_PRE_NG)
			std::string name{ a_normalizedFxpFilename };
			if (name.empty() || name.front() == '<' || name.find('<') != std::string::npos) {
				return false;
			}
			if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
				name.erase(0, slash + 1);
			}
			for (const auto extension : { std::string_view{ ".hlsl" }, std::string_view{ ".fxp" }, std::string_view{ ".fx" } }) {
				if (name.ends_with(extension)) {
					name.resize(name.size() - extension.size());
					break;
				}
			}
			return name == PreNGEnvironment::kPreNGDFLightingFxpName;
#else
			(void)a_normalizedFxpFilename;
			return false;
#endif
		}

		bool IsPreNGBSLightingFxpName(std::string_view a_normalizedFxpFilename)
		{
#if defined(FALLOUT_PRE_NG)
			std::string name{ a_normalizedFxpFilename };
			if (name.empty() || name.front() == '<' || name.find('<') != std::string::npos) {
				return false;
			}
			if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
				name.erase(0, slash + 1);
			}
			for (const auto extension : { std::string_view{ ".hlsl" }, std::string_view{ ".fxp" }, std::string_view{ ".fx" } }) {
				if (name.ends_with(extension)) {
					name.resize(name.size() - extension.size());
					break;
				}
			}
			return name == PreNGEnvironment::kPreNGBSLightingFxpName;
#else
			(void)a_normalizedFxpFilename;
			return false;
#endif
		}

		bool IsPreNGDFCompositeFxpName(std::string_view a_normalizedFxpFilename)
		{
#if defined(FALLOUT_PRE_NG)
			std::string name{ a_normalizedFxpFilename };
			if (name.empty() || name.front() == '<' || name.find('<') != std::string::npos) {
				return false;
			}
			if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
				name.erase(0, slash + 1);
			}
			for (const auto extension : { std::string_view{ ".hlsl" }, std::string_view{ ".fxp" }, std::string_view{ ".fx" } }) {
				if (name.ends_with(extension)) {
					name.resize(name.size() - extension.size());
					break;
				}
			}
			return name == PreNGEnvironment::kPreNGDFCompositeFxpName;
#else
			(void)a_normalizedFxpFilename;
			return false;
#endif
		}

		bool IsPreNGDFLightFullShadowedDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFLightingShaderType &&
			       IsPreNGDFLightFxpName(a_normalizedFxpFilename) &&
			       F4Runtime::PreNG::IsDFLightFullShadowedPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool IsPreNGBSLightingContractPixelDescriptor(std::uint32_t a_descriptor)
		{
			return F4Runtime::PreNG::IsBSLightingContractPixelDescriptor(a_descriptor);
		}

		bool IsPreNGBSLightingContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGBSLightingShaderType &&
			       IsPreNGBSLightingFxpName(a_normalizedFxpFilename) &&
			       IsPreNGBSLightingContractPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldCompilePreNGBSLightingContractShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGBSLightingContractCompileEnv);
			return enabled &&
			       IsPreNGBSLightingContractDescriptorShader(
					   a_stage,
					   a_shaderType,
					   a_normalizedFxpFilename,
					   a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldCompilePreNGBSLightingConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGBSLightingConsumerCompileEnv);
			return enabled &&
			       IsPreNGBSLightingContractDescriptorShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldBindPreNGBSLightingLLFVisibleConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGBSLightingLLFBindEnv);
			return enabled &&
			       IsPreNGBSLightingContractDescriptorShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool IsPreNGDFLightFullContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFLightingShaderType &&
			       IsPreNGDFLightFxpName(a_normalizedFxpFilename) &&
			       F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool IsPreNGDFCompositeContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       F4Runtime::PreNG::IsDFCompositeObservedPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldCompilePreNGDFCompositeDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeDescriptorCompileEnv);
			return enabled &&
			       IsPreNGDFCompositeContractDescriptorShader(
					   a_stage,
					   a_shaderType,
					   a_normalizedFxpFilename,
					   a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldEnablePreNGDFCompositeFogSafeBindShader()
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeFogSafeBindEnv);
			return enabled;
#else
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla40Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       ShouldEnablePreNGDFCompositeFogSafeBindShader() &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_40;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla88Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla10040Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       ShouldEnablePreNGDFCompositeFogSafeBindShader() &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10040;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla10088Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanillaSafeBindShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
			return ShouldUsePreNGDFCompositeVanilla40Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanilla88Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanilla10040Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanilla10088Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor);
		}

		bool IsPreNGDFLightLLFConsumerDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFLightingShaderType &&
			       IsPreNGDFLightFxpName(a_normalizedFxpFilename) &&
			       (F4Runtime::PreNG::IsDFLightLLFConsumerPixelDescriptor(a_descriptor) ||
			        (ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer() &&
			         F4Runtime::PreNG::IsDFLightFullShadowedPixelDescriptor(a_descriptor)) ||
			        (ShouldEnablePreNGDFLightForwardVisibleLLF() &&
			         F4Runtime::PreNG::IsDFLightForwardPixelDescriptor(a_descriptor)));
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool CanActivelyCompilePreNGLightingDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
			return a_shaderType == kPreNGBSLightingShaderType ||
			       IsPreNGDFLightLLFConsumerDescriptorShader(a_stage, a_shaderType, a_normalizedFxpFilename, a_descriptor) ||
			       IsPreNGDFCompositeContractDescriptorShader(a_stage, a_shaderType, a_normalizedFxpFilename, a_descriptor);
		}

		bool ShouldCompilePostNGBSLightingConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType)
		{
#if defined(FALLOUT_PRE_NG)
			(void)a_stage;
			(void)a_shaderType;
			return false;
#else
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGBSLightingShaderType &&
			       (DebugSwitches::ReadSwitchEnabled(kPostNGBSLightingLLFBindEnv) ||
			        DebugSwitches::ReadSwitchEnabled(kPostNGBSLightingDescriptorObserveEnv));
#endif
		}

		bool CanCompileDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
			return (a_stage == ShaderStage::Pixel &&
			           a_shaderType == kPreNGBSLightingShaderType &&
			           F4Runtime::IsDeferredLightingPixelDescriptor(a_descriptor)) ||
			       ReadDescriptorCompileSwitch() ||
			       (a_stage == ShaderStage::Pixel &&
			           a_shaderType == kPreNGDFLightingShaderType &&
			           IsPreNGDFLightFxpName(a_normalizedFxpFilename) &&
			           ShouldEnablePreNGDFLightForwardVisibleLLF() &&
			           F4Runtime::PreNG::IsDFLightForwardPixelDescriptor(a_descriptor)) ||
			       ShouldCompilePreNGBSLightingContractShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldCompilePreNGBSLightingConsumerShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldBindPreNGBSLightingLLFVisibleConsumerShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanillaSafeBindShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldCompilePreNGDFCompositeDescriptorShader(
					   a_stage,
					   a_shaderType,
					   a_normalizedFxpFilename,
					   a_descriptor) ||
			       ShouldCompilePostNGBSLightingConsumerShader(a_stage, a_shaderType);
		}

		std::string BuildFeatureDefineList(const std::vector<D3D_SHADER_MACRO>& a_defines)
		{
			std::string result;
			for (const auto& define : a_defines) {
				if (!define.Name) {
					break;
				}
				if (!result.empty()) {
					result += ',';
				}
				result += define.Name;
			}
			return result.empty() ? "<none>" : result;
		}

		struct DescriptorDefineSet
		{
			std::vector<std::pair<std::string, std::string>> storage;
			std::vector<D3D_SHADER_MACRO> macros;
		};

		DescriptorDefineSet BuildDescriptorDefineSet(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::uint32_t a_descriptor)
		{
			DescriptorDefineSet result;

			for (auto* feature : Feature::GetFeatureList()) {
				if (!feature || !feature->loaded) {
					continue;
				}

				const auto name = feature->GetShaderDefineName();
				if (!name.empty()) {
					result.storage.emplace_back(std::string{ name }, "1");
				}
			}

			result.storage.emplace_back("FO4CS_DESCRIPTOR_SHADER", "1");
			result.storage.emplace_back("FO4CS_SHADER_TYPE", std::to_string(a_shaderType));
			result.storage.emplace_back("FO4CS_SHADER_DESCRIPTOR", std::to_string(a_descriptor));
			result.storage.emplace_back(
				a_stage == ShaderStage::Vertex ? "FO4CS_DESCRIPTOR_VERTEX_SHADER" : "FO4CS_DESCRIPTOR_PIXEL_SHADER",
				"1");

#if defined(FALLOUT_PRE_NG)
			if (a_stage == ShaderStage::Pixel &&
				a_shaderType == kPreNGBSLightingShaderType &&
				F4Runtime::IsDeferredLightingPixelDescriptor(a_descriptor)) {
				result.storage.emplace_back("DEFERRED", "1");
				result.storage.emplace_back("FO4CS_DEFERRED_LIGHTING_DESCRIPTOR", "1");
			}
			if (ShouldCompilePreNGBSLightingContractShader(
					a_stage,
					a_shaderType,
					PreNGEnvironment::kPreNGBSLightingFxpName,
					a_descriptor)) {
				result.storage.emplace_back("FO4CS_BSLIGHTING_CONTRACT_DESCRIPTOR", "1");
			}
			if (ShouldCompilePreNGBSLightingConsumerShader(
					a_stage,
					a_shaderType,
					PreNGEnvironment::kPreNGBSLightingFxpName,
					a_descriptor)) {
				result.storage.emplace_back("FO4CS_BSLIGHTING_LLF_CONSUMER_DESCRIPTOR", "1");
			}
			if (ShouldBindPreNGBSLightingLLFVisibleConsumerShader(
					a_stage,
					a_shaderType,
					PreNGEnvironment::kPreNGBSLightingFxpName,
					a_descriptor)) {
				result.storage.emplace_back("FO4CS_BSLIGHTING_LLF_CONSUMER_DESCRIPTOR", "1");
				result.storage.emplace_back("FO4CS_BSLIGHTING_LLF_VISIBLE_CONSUMER", "1");
			}
			if (a_stage == ShaderStage::Pixel &&
				a_shaderType == kPreNGDFLightingShaderType &&
				F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(a_descriptor)) {
				result.storage.emplace_back("FO4CS_DFLIGHT_FULL_CONTRACT_DESCRIPTOR", "1");
				if (ShouldEnablePreNGDFLightFullContractVisibleLLF()) {
					result.storage.emplace_back("FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_LLF", "1");
					result.storage.emplace_back(
						"FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_MAX_LIGHTS",
						std::to_string(GetPreNGDFLightFullContractVisibleMaxLights()));
					result.storage.emplace_back(
						"FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_STRICT_MAX_LIGHTS",
						std::to_string(GetPreNGDFLightFullContractVisibleStrictMaxLights()));
					result.storage.emplace_back(
						"FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_CLUSTER_MAX_LIGHTS",
						std::to_string(GetPreNGDFLightFullContractVisibleClusterMaxLights()));
				}
			}
			if (a_stage == ShaderStage::Pixel &&
				a_shaderType == kPreNGDFCompositeShaderType &&
				F4Runtime::PreNG::IsDFCompositeObservedPixelDescriptor(a_descriptor)) {
				result.storage.emplace_back("FO4CS_DFCOMPOSITE_CONTRACT_DESCRIPTOR", "1");
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_40 &&
					ShouldUsePreNGDFCompositeVanilla40Shader(
						a_stage,
						a_shaderType,
						PreNGEnvironment::kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_40", "1");
				}
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88 &&
					ShouldUsePreNGDFCompositeVanilla88Shader(
						a_stage,
						a_shaderType,
						PreNGEnvironment::kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_88", "1");
				}
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10040 &&
					ShouldUsePreNGDFCompositeVanilla10040Shader(
						a_stage,
						a_shaderType,
						PreNGEnvironment::kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_10040", "1");
				}
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088 &&
					ShouldUsePreNGDFCompositeVanilla10088Shader(
						a_stage,
						a_shaderType,
						PreNGEnvironment::kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_10088", "1");
				}
				if (ShouldEnablePreNGDFCompositeVisibleLLF() &&
					IsPreNGDFCompositeVisibleLLFDescriptor(a_descriptor) &&
					ShouldUsePreNGDFCompositeVanillaSafeBindShader(
						a_stage,
						a_shaderType,
						PreNGEnvironment::kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VISIBLE_LLF", "1");
					result.storage.emplace_back(
						"FO4CS_DFCOMPOSITE_VISIBLE_LLF_SCALE_1024",
						std::to_string(GetPreNGDFCompositeVisibleLLFScale1024()));
					result.storage.emplace_back(
						"FO4CS_DFCOMPOSITE_VISIBLE_LLF_MAX_LIGHTS",
						std::to_string(GetPreNGDFCompositeVisibleLLFMaxLights()));
				}
			}
#else
			if (a_stage == ShaderStage::Pixel && a_shaderType == kPreNGBSLightingShaderType) {
				// FO4 forward consumer: clusters-only (no b3 strict-light buffer).
				result.storage.emplace_back("LLF_CLUSTERS_ONLY", "1");
				result.storage.emplace_back("FO4CS_BSLIGHTING_LLF_CONSUMER_DESCRIPTOR", "1");
			}
#endif

			result.macros.reserve(result.storage.size() + 1);
			for (auto& [name, value] : result.storage) {
				result.macros.push_back({ name.c_str(), value.c_str() });
			}
			result.macros.push_back({});
			return result;
		}

		std::string_view GetDescriptorTarget(ShaderStage a_stage) noexcept
		{
			return a_stage == ShaderStage::Vertex ? "vs_5_0" : "ps_5_0";
		}
	}

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

		if (ShouldEnablePreNGDFLightForwardVisibleLLF() &&
		    a_key.stage == ShaderStage::Pixel &&
		    a_key.shaderType == kPreNGDFLightingShaderType &&
		    IsPreNGDFLightFxpName(a_key.fxpFilename) &&
		    F4Runtime::PreNG::IsDFLightForwardPixelDescriptor(a_key.descriptor)) {
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
