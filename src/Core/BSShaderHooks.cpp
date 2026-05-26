#include "Core/BSShaderHooks.h"
#include "Core/CommunityShaders.h"
#include "Core/Feature.h"
#include "Core/ShaderCompiler.h"

#include <vector>

namespace CommunityShaders
{
	// ── BSShader::ReloadShaders hook ──────────────────────────────
	//
	// Port of Skyrim CS BSShader::LoadShaders hook.
	// FO4 equivalent: BSShader::ReloadShaders(bool) at vfunc 0x0B.
	//
	// Deferred execution pattern:
	//   ReloadShaders may fire before the D3D device is stable (loading
	//   screens, device creation).  Instead of silently dropping these
	//   calls, we enqueue the BSShader* and drain the queue once per
	//   frame when the device is ready (≥ kStableFrame frames).
	//
	// PreNG vtable offsets (verified via IDA static analysis):
	//   BSLightingShader vtable @ imagebase + 0x309AAB8
	//   vfunc 0x0B             @ imagebase + 0x309AB10

	static constexpr std::uintptr_t kPreNG_VTableOffset = 0x309AAB8;
	static constexpr std::uint64_t  kStableFrame      = 5;

	// ── Forward declarations ────────────────────────────────────

	void ReplacePixelShaders(RE::BSShader* shader);
	static ID3D11PixelShader* CompileReplacementPS(
		ID3D11Device*, const RE::BSShader&, std::uint32_t);

	// ── Deferred shader replacement queue ─────────────────────────

	struct PendingReplace { RE::BSShader* shader; };
	static std::vector<PendingReplace> s_pending;

	static void DrainPending()
	{
		if (s_pending.empty()) return;

		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		auto* device = runtime->GetDevice();
		if (!device || runtime->GetFrameCount() < kStableFrame) return;

		logger::info("[BSShaderHooks] DrainPending: {} queued shader(s) at frame {}",
		             s_pending.size(), runtime->GetFrameCount());

		// Move out so ReplacePixelShaders can re-enqueue on failure
		auto pending = std::move(s_pending);
		s_pending.clear();

		for (auto& item : pending)
			ReplacePixelShaders(item.shader);
	}

	// ── Hook thunk ────────────────────────────────────────────────

	struct BSShader_ReloadShaders
	{
		static void thunk(RE::BSShader* shader, bool a_clear)
		{
			logger::info("[BSShaderHooks] thunk: type={} clear={}", shader->shaderType, a_clear);

			func(shader, a_clear);  // let game load originals first

			auto* runtime = CommunityShaders::Runtime::GetSingleton();
			if (!runtime->GetDevice() || runtime->GetFrameCount() < kStableFrame) {
				// Defer: device not stable yet — enqueue for later
				s_pending.push_back({ shader });
				return;
			}

			ReplacePixelShaders(shader);
		}
		static inline void (*func)(RE::BSShader*, bool) = nullptr;
	};

	void ReplacePixelShaders(RE::BSShader* shader)
	{
		auto* device = CommunityShaders::Runtime::GetSingleton()->GetDevice();
		if (!device) return;

		for (auto* entry : shader->pixelShaders) {
			if (!entry->shader) continue;

			auto pixelDesc = entry->id;
			auto vertexDesc = entry->id;
			ModifyShaderLookup(*shader, vertexDesc, pixelDesc);

			auto* newPS = CompileReplacementPS(device, *shader, pixelDesc);
			if (newPS) {
				entry->shader->Release();
				entry->shader = reinterpret_cast<decltype(entry->shader)>(newPS);
				logger::info("[BSShaderHooks] Replaced PS: type={} desc=0x{:08X}",
				             shader->shaderType, pixelDesc);
			}
		}
	}

	// ── Shader compilation ─────────────────────────────────────
	//
	// Compiles Lighting.hlsl with Feature defines injected.
	// Called by ReplacePixelShaders for each pixel shader entry.

	static ID3D11PixelShader* CompileReplacementPS(
		ID3D11Device* device,
		const RE::BSShader& /*shader*/,
		std::uint32_t /*descriptor*/)
	{
		std::vector<D3D_SHADER_MACRO> defines;

		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->loaded) continue;
			auto name = feature->GetShaderDefineName();
			if (name.empty()) continue;
			defines.push_back({ name.data(), "1" });
		}

		D3D_SHADER_MACRO nullTerm{};
		defines.push_back(nullTerm);

		auto* compiler = ShaderCompiler::GetSingleton();
		auto bytecode = compiler->CompileFromFile(
			"Lighting.hlsl", "ps_5_0", defines.data(), "main");

		if (!bytecode) return nullptr;

		ID3D11PixelShader* ps = nullptr;
		if (FAILED(device->CreatePixelShader(
			bytecode->data(), bytecode->size(), nullptr, &ps))) {
			return nullptr;
		}
		return ps;
	}

	// ── ModifyShaderLookup ──────────────────────────────────────

	void ModifyShaderLookup(const RE::BSShader& /*a_shader*/,
	                        std::uint32_t& /*a_vertexDescriptor*/,
	                        std::uint32_t& /*a_pixelDescriptor*/)
	{
	}

	// ── Install / Frame hook ────────────────────────────────────

	void BSShaderHooks::Install()
	{
#if defined(FALLOUT_POST_AE)
		auto imageBase = REX::FModule::GetExecutingModule().GetBaseAddress();
#else
		auto imageBase = REL::Module::get().base();
#endif

#if defined(FALLOUT_PRE_NG)
		auto vtableAddr = imageBase + kPreNG_VTableOffset;
#else
		auto vtableReloc = REL::Relocation<std::uintptr_t>(
			RE::VTABLE::BSLightingShader[0]);
		auto vtableAddr = vtableReloc.address();
#endif
		auto* vtable = reinterpret_cast<std::uintptr_t*>(vtableAddr);
		auto reloadShadersFn = vtable[0x0B];

		logger::info("[BSShaderHooks] imageBase=0x{:X} vtable=0x{:X} vfunc[0x0B]=0x{:X}",
		             imageBase, vtableAddr, reloadShadersFn);

		BSShader_ReloadShaders::func = reinterpret_cast<void(*)(RE::BSShader*, bool)>(
			Detours::X64::DetourFunction(
				reloadShadersFn,
				reinterpret_cast<std::uintptr_t>(BSShader_ReloadShaders::thunk)));

		logger::info("[BSShaderHooks] Detoured ReloadShaders; drain gate at frame {}",
		             kStableFrame);
	}

	void BSShaderHooks::OnFrame()
	{
		DrainPending();
	}
}
