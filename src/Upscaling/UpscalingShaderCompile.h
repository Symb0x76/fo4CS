#pragma once

#include <d3d11.h>

// Runtime HLSL compilation for the frame-generation compute shaders. Returns a
// freshly created shader object (caller owns the reference) or nullptr.
ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const char* ProgramType, const char* Program = "main");
ID3D11DeviceChild* CompileFrameGenerationShader(const wchar_t* fileName, const char* programType, const char* program = "main");
