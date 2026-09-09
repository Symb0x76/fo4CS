#include "Core/ShaderHooks/PreNGRuntime.h"

#include "Core/PreNGEnvironment.h"

#include <cctype>
#include <string>

namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	bool IsReadableMemory(std::uintptr_t a_address, std::size_t a_size)
	{
		return F4Runtime::IsReadableAddress(a_address, a_size);
	}

	bool IsWritableMemory(std::uintptr_t a_address, std::size_t a_size)
	{
		return F4Runtime::IsWritableAddress(a_address, a_size);
	}


	bool ReadPreNGEnvironmentSwitch(const char* a_name)
	{
		return DebugSwitches::ReadSwitchEnabled(a_name);
	}

	const char* PreNGEnvironmentValueSourceName(PreNGEnvironmentValueSource a_source)
	{
		switch (a_source) {
		case PreNGEnvironmentValueSource::kDebugIni:
			return "debug-ini";
		default:
			return "none";
		}
	}

	PreNGEnvironmentValueSource ToPreNGEnvironmentSource(DebugSwitches::Source a_source)
	{
		return a_source == DebugSwitches::Source::kDebugIni ?
			PreNGEnvironmentValueSource::kDebugIni :
			PreNGEnvironmentValueSource::kNone;
	}

	PreNGEnvironmentUIntState ReadPreNGEnvironmentUInt(const char* a_name)
	{
		const auto state = DebugSwitches::ReadUInt(a_name);
		return {
			state.value,
			ToPreNGEnvironmentSource(state.source),
			state.present,
			state.valid
		};
	}

	std::uint32_t NormalizePreNGLightingVertexDescriptor(std::uint32_t a_descriptor)
	{
		return F4Runtime::PreNG::NormalizeLightingVertexDescriptor(a_descriptor);
	}

	std::uint32_t NormalizePreNGLightingPixelDescriptor(std::uint32_t a_descriptor)
	{
		return F4Runtime::PreNG::NormalizeLightingPixelDescriptor(a_descriptor);
	}

	std::uintptr_t ReadPreNGPointer(std::uintptr_t a_address)
	{
		return F4Runtime::ReadPointer(a_address);
	}

	std::uintptr_t ReadPreNGShaderEntryD3DObject(std::uintptr_t a_entry)
	{
		return F4Runtime::ReadPreNGShaderEntryD3DObject(a_entry);
	}

	std::string ReadPreNGCString(const char* a_value, std::size_t a_maxLength)
	{
		if (!a_value) {
			return "<null>";
		}

		const auto base = reinterpret_cast<std::uintptr_t>(a_value);
		std::string result;
		result.reserve(a_maxLength);

		for (std::size_t i = 0; i < a_maxLength; ++i) {
			char ch = 0;
			if (!ReadPreNGValue(base + i, ch)) {
				return result.empty() ? "<unreadable>" : result + "<unreadable-tail>";
			}
			if (ch == '\0') {
				return result.empty() ? "<empty>" : result;
			}

			const auto byte = static_cast<unsigned char>(ch);
			result.push_back(byte >= 0x20 && byte <= 0x7E ? ch : '?');
		}

		return result + "<truncated>";
	}

	std::string NormalizePreNGFxpFilename(std::string_view a_fxpFilename)
	{
		std::string normalized{ a_fxpFilename };
		for (auto& ch : normalized) {
			const auto byte = static_cast<unsigned char>(ch);
			ch = byte == '\\' ? '/' : static_cast<char>(std::tolower(byte));
		}
		return normalized;
	}

	bool IsPreNGFxpBaseName(std::string_view a_fxpFilename, std::string_view a_expectedBaseName)
	{
		auto normalized = NormalizePreNGFxpFilename(a_fxpFilename);
		if (normalized.empty() || normalized.front() == '<' || normalized.find('<') != std::string::npos) {
			return false;
		}

		if (const auto slash = normalized.find_last_of('/'); slash != std::string::npos) {
			normalized.erase(0, slash + 1);
		}
		for (const auto extension : { std::string_view{ ".hlsl" }, std::string_view{ ".fxp" }, std::string_view{ ".fx" } }) {
			if (normalized.ends_with(extension)) {
				normalized.resize(normalized.size() - extension.size());
				break;
			}
		}

		return normalized == a_expectedBaseName;
	}

	bool IsPreNGBSLightingFxpName(std::string_view a_fxpFilename)
	{
		return IsPreNGFxpBaseName(a_fxpFilename, PreNGEnvironment::kPreNGBSLightingFxpName);
	}

	bool IsPreNGDFLightFxpName(std::string_view a_fxpFilename)
	{
		return IsPreNGFxpBaseName(a_fxpFilename, PreNGEnvironment::kPreNGDFLightingFxpName);
	}

	bool IsPreNGDFCompositeFxpName(std::string_view a_fxpFilename)
	{
		return IsPreNGFxpBaseName(a_fxpFilename, PreNGEnvironment::kPreNGDFCompositeFxpName);
	}

	bool IsPreNGPowerOfTwo(std::uint32_t a_value)
	{
		return a_value != 0 && (a_value & (a_value - 1)) == 0;
	}
#endif
}
