# fo4CS architecture (`community-shaders`)

This branch is the full-featured fork: everything the `main` plugin suite does
(DLSS / FSR upscaling, frame generation, Reflex) plus the Community Shaders port
(feature framework, shader replacement pipeline, LightLimitFix, deferred passes).
It ships as a single `CommunityShaders.dll`.

Keep this current: a change that adds a hook, a target or a log event updates the
matching section here. The `main` branch has its own `docs/architecture.md`; the
sections below marked *shared* are intentionally identical between the two.

## Build targets

Everything links into one DLL by default. `if(COMMUNITY_SHADERS)` in
`CMakeLists.txt` forces `AIO`, `FRAMEGEN`, `REFLEX`, `UPSCALER` and `OVERLAY` off,
so the other plugin targets exist only when the branch is configured with
`-DCOMMUNITY_SHADERS=OFF`.

| Target | Kind | Directory | Owns | Shared with `main` |
|--------|------|-----------|------|--------------------|
| `Fo4cs.Runtime` | INTERFACE | — | `FALLOUT_PRE_NG` / `FALLOUT_POST_NG` / `FALLOUT_POST_AE` | yes |
| `Fo4cs.Diagnostics` | headers | `src/Diagnostics/` | log event codes, F4SE log directory, hang trace | yes |
| `Fo4cs.Platform` | headers | `src/Platform/` | F4SE plugin boilerplate, version-specific game accessors, module paths | yes |
| `Fo4cs.Render` | OBJECT | `src/Render/` | D3D11 hooks, companion D3D12 device + swap chain, D3D11on12 interop, the swap-chain proxy, HDR colour-space pass, runtime adapter | yes |
| `Fo4cs.Upscaling` | OBJECT | `src/Upscaling/` | upscaler policy, render backend, frame-generation capture/composite, Streamline, FidelityFX | yes |
| `Fo4cs.ImGui` | OBJECT | `extern/imgui/` | ImGui core | yes |
| `Fo4cs.Overlay` | OBJECT | `src/Overlay/` | overlay host: panel registry, hotkey, WndProc | yes |
| `Fo4cs.Framework` | OBJECT | `src/Core/` | `Feature` base, feature registry and global instances, menu, runtime state, debug switches, diagnostics formatter | no |
| `Fo4cs.Shaders` | OBJECT | `src/Core/`, `src/Core/Shaders/` | shader cache, compiler, database, descriptor gates, BSShader and D3D11 shader hooks | no |
| `Fo4cs.Passes` | OBJECT | `src/Core/` | deferred, additive, draw-state proof, LLF pixel tracker | no |
| `Fo4cs.Features` | OBJECT | `src/Features/` | LightLimitFix, ExtendedMaterials, ShaderDump, Upscaling, FrameGeneration, Reflex, Overlay features | no |

`src/Presentation/` and `src/Plugins/` compile into the DLL target itself: they are
the composition root, not a reusable layer.

Compiler flags live in one place, `cmake/Fo4csTargets.cmake`
(`fo4cs_configure_target`), shared with `main`. `extern/Streamline/include` must
stay ahead of `include/` in the include order because both ship an `sl.h`.

## Dependency direction

```
Plugins ──▶ Features ──▶ Framework ──▶ Platform ──▶ Diagnostics
   │            │            ▲
   │            ├──▶ Shaders ┤
   │            └──▶ Passes ─┘
   ├──▶ Overlay ──▶ ImGui
   └──▶ Upscaling ──▶ Render
```

Two cycles are known and recorded as debt, not fixed:

- `Render` ↔ `Upscaling`: `DX11Hooks.cpp` and `DX12SwapChainPresent.cpp` call
  `Upscaling`, `Streamline` and `FidelityFX` directly.
- `Framework` ↔ `Features`: `Core/Globals.cpp` defines every feature instance by
  value, so the framework depends on the concrete feature types.

## Layers specific to this branch

### Feature framework

`Feature` (`src/Core/Feature.h`) is the base for every feature: settings load/save
to `Data/F4SE/Plugins/CommunityShaders/<ShortName>.ini`, ImGui panel, shader
defines, constraints and per-frame prepass hooks. `Core/Globals.cpp` owns one
instance of each feature by value and exposes the ordered list; the lifecycle
iterators (`LoadFeatures`, `DataLoaded`, `PostPostLoad`, `SetupResources`,
`ResetFeatures`) fan out over that list. `CommunityShaders::Runtime`
(`Core/CommunityShaders.cpp`) is the composition root that drives them.

### Shader replacement pipeline

1. `Core/ShaderDB` loads user-supplied shader hash tables from
   `Data/F4SE/Plugins/CommunityShaders/ShaderDB/<runtime>/`.
2. `Core/BSShaderHooks` detours `BSShader::ReloadShaders`, mutates semantic
   descriptors and queues replacements that drain once the device is stable.
3. `Core/Shaders/ShaderDescriptorGates` decides, per (stage, shader type, `.fxp`
   name, descriptor), whether a replacement may be compiled or bound, and which
   `#define` set it compiles with. Every predicate is gated by a Debug.ini switch
   read through `Core/Shaders/ShaderSwitches`.
4. `Core/ShaderCache` keys, compiles (via `Core/ShaderCompiler`) and caches the
   resulting D3D objects, then writes them into the game's shader structs.
5. `Core/Hooks` and `Core/D3D11DeviceHooks` observe created shader bytecode for
   the ShaderDump reverse-engineering tooling; they never replace shaders.

`Core/Shaders/ShaderCacheInternal.h` is private to `src/Core` and declares the
gate predicates. Each predicate owns a one-shot `static const bool` cache, some of
which log on first call, so each must keep exactly one definition — never make one
inline or move a body into that header.

### Debug switches

This branch reads its switches from `Data/F4SE/Plugins/CommunityShaders/Debug.ini`
(`Core/DebugSwitches.h`), not from environment variables. The file is loaded once
via `std::call_once` and missing switches default to off.

## Hook ownership

| Hook | Installed by | Where | Variant-specific |
|------|--------------|-------|------------------|
| `D3D11CreateDeviceAndSwapChain`, `D3D11CreateDevice` IAT | `DX11Hooks::Install` | `Render/DX11Hooks.cpp` | no |
| `IDXGIFactory::CreateSwapChain(ForHwnd)` vtable | `DX11Hooks` | `Render/DX11Hooks.cpp` | no |
| Renderer `WindowSizeChanged`, dynamic-resolution viewport, `DrawWorld` | `Upscaling::InstallHooks` | `Upscaling/UpscalingHooks.cpp` | `REL::ID` per variant |
| Dynamic-resolution update, TAA active, pre-UI passes | `InstallUpscalerRenderBackendHooks` | `Upscaling/UpscalerRenderBackendHooks.cpp` | `REL::ID` per variant |
| `BSShader::ReloadShaders` | `BSShaderHooks::Install` | `Core/BSShaderHooks.cpp` | yes |
| D3D11 `Create*Shader` vtable | `CommunityShaders::Hooks::Install` | `Core/Hooks.cpp`, `Core/D3D11DeviceHooks.cpp` | no |
| Deferred draw vtable + pass hooks | `Deferred::Hooks::Install` | `Core/Deferred.cpp` | yes |
| `BSLightingShader::SetupGeometry` (vfunc 7), point-light setup, batch setup | `LightLimitFix` | `Features/LightLimitFix.cpp` | heavily |
| Overlay `WndProc` | `Overlay::Initialize` | `Overlay/Overlay.cpp` | no |

Raw `REL::ID` literals live in the hook files above and in `Platform/RE/*.h`. New
game-address access goes through the `Platform/RE` accessors.

## Lifecycle

1. `F4SEPlugin_Load` (`Plugins/CommunityShadersPlugin.cpp`, main thread):
   `InitializeLog` → `BOOT`; `Runtime::Load` refreshes state, installs the shader,
   BSShader and deferred hooks, loads the shader database and every feature →
   `FEATURE_STATE` then `HOOK_INSTALL`; `DX11Hooks::Install`.
2. Game creates its D3D11 device/swap chain (render thread): the IAT hook builds
   the companion D3D12 device, swap chain and interop → `DEVICE_READY`, or falls
   back to plain D3D11 → `ERROR`.
3. `Runtime::OnD3D11DeviceCreated` → `DEVICE_READY`, then deferred and feature
   `SetupResources` → `RESOURCE_CREATE`.
4. F4SE `kPostPostLoad`: feature `PostPostLoad`, which is where LightLimitFix
   installs its engine hooks.
5. Per frame: `Runtime::OnFrame` drains the deferred shader-replacement queue,
   resets features and draws the menu; the render-backend hooks override proxy
   targets; `Present` runs upscaling/frame generation and the overlay.

## Threads

Hooks, `Present` and the feature prepasses run on the game's render thread.
`F4SEPlugin_Load` and `kPostPostLoad` run on the main thread before rendering
starts. Explicit synchronisation exists in three places: `ShaderCache`
(`observedLock`, `descriptorLock`), `Deferred` (`deferredTraceLock`,
`lightingShaderLock`, `blendStateLock`, and a `thread_local` OM save slot), and the
overlay panel registry. `Deferred::ClearShaderCache` takes `lightingShaderLock` and
`blendStateLock` together — any new lock must respect that order.

## Runtime variants

| | PreNG | PostNG | PostAE |
|--|-------|--------|--------|
| Macros | `FALLOUT_PRE_NG` | `FALLOUT_POST_NG` | `FALLOUT_POST_NG` + `FALLOUT_POST_AE` |
| CommonLibF4 | `extern/CommonLibF4PreNG` | `extern/CommonLibF4PostNG` | `extern/CommonLibF4PostAE` |
| Streamline | skipped (blocks `slInit`) | loaded | loaded |
| LightLimitFix path | clustered prepass + engine-lighting shader replacement | descriptor observation only | as PostNG |

LightLimitFix is overwhelmingly PreNG code: two conditional regions in
`Features/LightLimitFix.cpp` span roughly lines 262–1822 and 3919–4665.

## Log contract

The DLL writes `Documents\My Games\Fallout4\F4SE\CommunityShaders.log`; the opt-in
hang trace writes `CommunityShaders.hangtrace.log` beside it. Structured lines have
the form `event=<CODE> <text>`; the codes are in `src/Diagnostics/LogEvents.h` and
`tools/validate-runtime-log.ps1` checks them. See `docs/validation-matrix.md`.
