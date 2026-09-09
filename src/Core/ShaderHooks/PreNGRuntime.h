#pragma once

#include "Core/DebugSwitches.h"
#include "Core/ShaderHooks/PreNGShaderHookConstants.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Leaf primitives for the PreNG shader hooks: guarded memory access, the
// Debug.ini readers, descriptor normalisation and fxp-name matching. No state,
// no logging, no engine mutation -- everything here is a pure function or a
// thin forward to RE::FO4Runtime.
//
// The two templates stay in this header because ReadPreNGCString instantiates
// ReadPreNGValue<char>, and other clusters instantiate both at their own types.
namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)
	enum class PreNGEnvironmentValueSource
	{
		kNone,
		kDebugIni
	};

	struct PreNGEnvironmentUIntState
	{
		std::uint32_t value = 0;
		PreNGEnvironmentValueSource source = PreNGEnvironmentValueSource::kNone;
		bool present = false;
		bool valid = false;
	};

	template <class T>
	bool WritePreNGValue(std::uintptr_t a_address, const T& a_value)
	{
		return F4Runtime::WriteValue(a_address, a_value);
	}

	template <class T>
	bool ReadPreNGValue(std::uintptr_t a_address, T& a_value)
	{
		return F4Runtime::ReadValue(a_address, a_value);
	}

	bool IsReadableMemory(std::uintptr_t a_address, std::size_t a_size);
	bool IsWritableMemory(std::uintptr_t a_address, std::size_t a_size);
	bool ReadPreNGEnvironmentSwitch(const char* a_name);
	const char* PreNGEnvironmentValueSourceName(PreNGEnvironmentValueSource a_source);
	PreNGEnvironmentValueSource ToPreNGEnvironmentSource(DebugSwitches::Source a_source);
	PreNGEnvironmentUIntState ReadPreNGEnvironmentUInt(const char* a_name);
	std::uint32_t NormalizePreNGLightingVertexDescriptor(std::uint32_t a_descriptor);
	std::uint32_t NormalizePreNGLightingPixelDescriptor(std::uint32_t a_descriptor);
	std::uintptr_t ReadPreNGPointer(std::uintptr_t a_address);
	std::uintptr_t ReadPreNGShaderEntryD3DObject(std::uintptr_t a_entry);
	std::string ReadPreNGCString(const char* a_value, std::size_t a_maxLength);
	std::string NormalizePreNGFxpFilename(std::string_view a_fxpFilename);
	bool IsPreNGFxpBaseName(std::string_view a_fxpFilename, std::string_view a_expectedBaseName);
	bool IsPreNGBSLightingFxpName(std::string_view a_fxpFilename);
	bool IsPreNGDFLightFxpName(std::string_view a_fxpFilename);
	bool IsPreNGDFCompositeFxpName(std::string_view a_fxpFilename);
	bool IsPreNGPowerOfTwo(std::uint32_t a_value);
#endif
}
