#pragma once

namespace RE { class BSShader; }

namespace CommunityShaders
{
	// Engine-level BSShader hooks — port of Skyrim CS architecture.
	//
	// 1. BSShader::ReloadShaders detour → semantic descriptor mutation
	// 2. Deferred execution queue → drains when D3D device is stable
	// 3. Feature::GetShaderDefineName → injects #define at compile time

	class BSShaderHooks
	{
	public:
		static void Install();
		static void OnFrame();   // call from Runtime::OnFrame — drains pending queue
	};

	void ModifyShaderLookup(const RE::BSShader& a_shader,
	                        std::uint32_t& a_vertexDescriptor,
	                        std::uint32_t& a_pixelDescriptor);

	void ReplacePixelShaders(RE::BSShader* shader);
}
