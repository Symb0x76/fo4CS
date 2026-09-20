# fo4CS `main` refactor plan (branch `codex/main-refactor`)

Source of requirements: `.codex/docs/thermo-nuclear-code-quality-review.md`, section
"Main 分支适用性评估 (2026-09-07)". This file is the executable checklist for that
section. Update the checkboxes as steps land; each step is one commit.

## Ground rules

- Every step must build `PreNG` Release with `/W4 /WX` and produce the same DLL
  names (`NuclearGFX.dll`, `FrameGen.dll`, `Upscaler.dll`, `Reflex.dll`, `Overlay.dll`).
- Phases 1–2 are move-only. No behavior change, no renamed public symbols, no
  "while I'm here" fixes. Bugs found during the move are recorded in
  `## Deferred findings` and fixed in separate commits after the split.
- Function-local `static` latches (`logged*`, `previous*`, `lastFrame`, `nextFrameID`,
  `upscalingCB`) move with their owning function and stay a single instance. Never
  convert them into `inline` header helpers.
- `REL::ID(...)` literals, vtable indices, `+0x..` offsets and every
  `#if defined(FALLOUT_PRE_NG / FALLOUT_POST_NG / FALLOUT_POST_AE)` block are
  transcribed verbatim.
- Anonymous-namespace helpers that gain cross-TU callers move to a named internal
  namespace in an `*Internal.h`; helpers whose address is stored (e.g.
  `StreamlineLogCallback`) keep external linkage.
- Runtime validation in-game is manual and owned by BOSS. This plan only automates
  build, static checks and log extraction.

## Phase 0 — Baseline

- [x] Rebase `codex/main-refactor` onto `main` (`7560022`); Codex's duplicate
      clang-tidy commit dropped.
- [x] Initialize submodules `CommonLibF4PreNG`, `Streamline`, `FidelityFX-SDK`, `imgui`.
- [x] Revert Codex's accidental trailing-newline strip in `CMakeLists.txt`.
- [x] `cmake --preset PreNG` (17 s) + `cmake --build build/PreNG --config Release`
      (27 s clean, 0 warnings, `NuclearGFX.dll` + `Overlay.dll`) — 2026-09-08.
- [x] Snapshot of `build/PreNG/*.vcxproj` compile settings taken (session
      scratchpad) so Phase 1 can prove compile flags are unchanged.
- [x] Codex WIP (`src/Diagnostics/LogEvents.h`, BOOT event in `PluginCommon.h`)
      committed as the seed of Phase 3 step 1.

Baseline facts:

| Item | Value |
|------|-------|
| CMake | system 4.4.3, generator Visual Studio 17 2022, x64 |
| vcpkg | VS-bundled, triplet `x64-windows-static-md`, binary cache warm |
| Files over 1k lines | `Upscaler.cpp` 1525, `UpscalerRenderBackend.cpp` 1279, `Streamline.cpp` 1111 |
| Near limit | `DX12SwapChain.cpp` 872, `Overlay.cpp` 658 |
| Log path | `Documents/My Games/Fallout4/F4SE/<Plugin::NAME>.log` (one file per plugin DLL on `main`; the single `CommunityShaders.log` in the review applies to the `community-shaders` branch) |
| `scripts/` | gitignored. Tracked tooling goes to `tools/` |

## Phase 1 — CMake module boundaries (DLL outputs unchanged)

1. [x] Add `cmake/Fo4csTargets.cmake` with
       `fo4cs_configure_target(<target>)` (cxx_std_23, `_WINDOWS`, `_AMD64_`,
       `_UNICODE`, debug defs, MSVC `/Zc:*` options, release opts, PCH),
       `fo4cs_configure_plugin_link`, `fo4cs_mark_third_party_sources` and
       `fo4cs_link_runtime(<target>)`. Rewrote `fo4cs_apply_plugin_defaults` and the
       `Core` block to call it. Verified against the baseline vcxproj snapshot:
       `Core` byte-identical (include order preserved: `extern/Streamline/include`
       before `include/`, both ship `sl.h`); plugins differ only by
       `FO4CS_ENABLE_DEBUG_SETTINGS=1` now also set in Debug/RelWithDebInfo (read only
       by `Upscaler.cpp`) and `src/` moving ahead of the generated `Plugin.h` dir
       (same set, no colliding header names).
2. [x] Split `Core` (OBJECT) into OBJECT libraries via
       `fo4cs_add_object_library()`; all plugins now call `fo4cs_link_runtime()`
       and `Core` is gone (single commit — every consumer lives in this file):
       - `Fo4cs.Render` — `DX11Hooks.*`, `DX12SwapChain.*`, `Buffer.h`.
       - `Fo4cs.Upscaling` — `Upscaler.*`, `UpscalerRenderBackend.cpp`,
         `Streamline.*`, `FidelityFX.*`.
       - `Fo4cs.ImGui` (NO_PCH) — `extern/imgui/*.cpp` core; PUBLIC ImGui include dirs.
       - `Fo4cs.Overlay` — `Overlay.cpp` + ImGui D3D12/Win32 backends, compiled once
         and absorbed by both `NuclearGFX` and `Overlay` (it defines its own export
         macro and includes no per-plugin header).
       `Fo4cs.Platform` / `Fo4cs.Diagnostics` INTERFACE targets are deferred to step
       3 together with the directory moves.
       OBJECT libraries are kept (not STATIC) so link semantics match today's
       behavior exactly; no dead-stripping surprises. vcxproj check: new libraries
       carry Core's flags verbatim (own PCH path), plugins lose only the per-file
       `NotUsing` entries of the moved ImGui backends, include sets unchanged.
3. [x] Directory moves with `git mv`: `src/Render/` (hooks, swap chain, `Buffer.h`),
       `src/Upscaling/` (Upscaler, render backend, Streamline, FidelityFX),
       `src/Platform/` (`PluginCommon.h`, `RE/`), `src/Plugins/` (the five
       `*Plugin.cpp` entry points); `src/Diagnostics/` and `src/Overlay/` unchanged.
       All quoted includes are now root-relative (`"Upscaling/Upscaler.h"`); `src/`
       stays the single PRIVATE include root. `Fo4cs.Platform` and
       `Fo4cs.Diagnostics` exist as header-only INTERFACE targets linked by
       `Fo4cs.Render` / `Fo4cs.Upscaling` to make ownership visible.
4. [x] Runtime variant as a target: `Fo4cs.Runtime` (INTERFACE) carries the
       `FALLOUT_*` macros of the selected variant and every fo4CS target links it from
       `fo4cs_configure_target`; the directory-wide `add_compile_definitions` is gone
       (verified: no CommonLibF4 variant reads the macros, so CommonLibF4 no longer
       compiles with them). One target instead of three because only one variant can
       exist per build tree (the CommonLibF4 subproject is variant-specific).
       `xseplugin_resolve_commonlib_root` now rejects more than one `BUILD_*` flag and
       exports `FO4CS_RUNTIME_VARIANT` / `FO4CS_RUNTIME_DEFINITIONS`. Configure prints
       one summary line. Removed dead variables `FO4CS_BUILD_*`, `GamePath`,
       `FO4CS_RUNTIME_FLAVOR` (no readers anywhere).

## Phase 2 — File decomposition (move-only)

Outlines were produced on 2026-09-08 by reading each file end to end. Target files
are all ≤ ~600 lines.

### 2a `Upscaler.cpp` (1525) → `src/Upscaling/`

| New file | Contents |
|----------|----------|
| `UpscalingSettingsIO.{h,cpp}` | INI/env/debug helpers: `IniSource`, `LoadIniIfExists`, `ClampIntSetting`, `GetEnvironmentValue`, `IsTruthy`, `ParseIntSetting`, `kDebugSettingsSupported`, `LoadSharedDebugSettings`, `ApplyDebugEnvironmentOverrides`, `ConfigureDebugLogging` |
| `UpscalingRuntimeProbe.{h,cpp}` | `IsFrameGenPluginVisible`, `GetModuleDirectory`, `GetCurrentPluginDirectory`, `HasStreamlineInterposer`, `IsPreNGRuntime`, `IsStreamlineRuntimeAvailable`, `GetDLSSUnavailableReason` |
| `UpscalingSettings.cpp` | `LoadSettings`, `LoadFrameGenerationSettings`, `LoadReflexSettings`, `ApplyRuntimeFallbacks`, `GetPreferredUpscaleMethod`, all `Uses*` |
| `UpscalingShaderCompile.{h,cpp}` | `CompileShader`, `CompileFrameGenerationShader` (default args move to the header once) |
| `UpscalingRenderTargetIDs.h` | `enum class RenderTarget`, `enum class DepthStencilTarget` (currently duplicated between `Upscaler.cpp` and `UpscalerRenderBackend.cpp`) |
| `FrameGenResources.cpp` | `CreateFrameGenerationResources`, `Reset` |
| `FrameGenCapture.cpp` | `NextHUDLessFrameID` (single counter), `IsLoadingMenuOpen`, `PreAlpha`, `CaptureHUDLessFrame`, `PostDisplay` |
| `FrameGenComposite.cpp` | `PostAlpha`, `CopyBuffersToSharedResources`, `BuildUIColorAndAlphaResource`, `DenoiseUIAlphaResource` |
| `FramePacing.cpp` | `TimerSleepQPC`, `FrameLimiter`, `GameFrameLimiter`, `GetRefreshRate` + NVIDIA license block |
| `UpscalingHooks.cpp` | `PostPostLoad`, four thunks, `reticleFix`, `InstallHooks` (`FALLOUT_POST_NG` REL::ID block) |

**Done 2026-09-08 (two commits).** Result: `Upscaler.cpp` 1525 → 431 lines and
kept as the settings/probe/policy unit; the planned `UpscalingSettingsIO` /
`UpscalingRuntimeProbe` / `UpscalingSettings` three-way split was dropped because the
remainder is already cohesive and under the 600-line target. New files:
`UpscalingHooks.cpp` (94), `FramePacing.cpp` (115), `UpscalingShaderCompile.{h,cpp}`
(57), `FrameGenResources.cpp` (268), `FrameGenCapture.cpp` (255),
`FrameGenComposite.cpp` (274), `UpscalingRenderTargetIDs.h` (78),
`UpscalingInternal.{h,cpp}` (shared `IsLoadingMenuOpen` / `NextHUDLessFrameID`,
moved from the anonymous namespace to `fo4cs::upscaling`).

Constraints honoured: `IsLoadingMenuOpen` and `NextHUDLessFrameID` have exactly one
definition. `bool reticleFix` keeps external linkage. Raw `new Texture2D` leaks in
`CreateFrameGenerationResources` are preserved (deferred finding).

### 2b `UpscalerRenderBackend.cpp` (1279) → `src/Upscaling/`

| New file | Contents |
|----------|----------|
| `UpscalerRenderBackendInternal.h` | RT/DS enums (shared with 2a), `renderTargetsPatch`, decls of `TraceRenderBackendStage`, `ScaleRenderExtent`, `GetUpscaleRatio`, `GetJitterOffset`, `IsLoadingMenuOpen` |
| `UpscalerSamplerStates.cpp` | `UpdateSamplerStates` (+`previousMipBias`), `OverrideSamplerStates`, `ResetSamplerStates` |
| `UpscalerRenderBackendShaders.cpp` | `CompileShaderAny`, ten `Get*` lazy shader/state accessors, `GetUpscalingCB`, `UpdateAndBindUpscalingCB`, `PatchSSRShader` |
| `UpscalerRenderTargets.cpp` | `UpdateRenderTarget(s)` (+`previousWidthRatio/HeightRatio`), `Override/ResetRenderTarget(s)` |
| `UpscalerDepthBuffer.cpp` | `UpdateDepth`, `OverrideDepth`, `ResetDepth`, `CopyDepth` |
| `UpscalerRenderBackend.cpp` (kept, ~560) | `OnD3D11DeviceCreated`, `GetUpscaleMethod`, `RestoreNativeRenderState`, `UpdateGameSettings`, `UpdateUpscaling`, `Upscale`, `CheckResources`, `Create/DestroyUpscalingResources`, `CopyNativeAABorder` |
| `UpscalerRenderBackendHooks.cpp` | four detour structs + `InstallUpscalerRenderBackendHooks` (`FALLOUT_POST_NG` REL::ID block) — extracted last |

**Done 2026-09-08 (one commit).** `UpscalerRenderBackend.cpp` 1279 → 598 lines. New:
`UpscalerRenderTargets.cpp` (196), `UpscalerDepthBuffer.cpp` (216),
`UpscalerSamplerStates.cpp` (67), `UpscalerRenderBackendShaders.cpp` (187),
`UpscalerRenderBackendHooks.cpp` (92). Instead of a second
`UpscalerRenderBackendInternal.h`, `TraceRenderBackendStage` joined
`UpscalingInternal.{h,cpp}` (single `previousStage` latch) and the local enum copies
were replaced by `UpscalingRenderTargetIDs.h` (value-identical subsets). Helpers used by
one cluster only (`GetUpscaleRatio`, `ScaleRenderExtent`, `GetJitterOffset`,
`CopyNativeAABorder`, the backend's own `IsLoadingMenuOpen`, `RestoreNativeRenderState`)
stayed in the anonymous namespace of the unit that uses them.

Constraints honoured: `CopyDepth` save/restore block and `Upscale`'s `Release()` pairing
not split. `extern bool enbLoaded` in this file is unreferenced (deferred finding).

### 2c `Streamline.cpp` (1111) → `src/Upscaling/Streamline/`

| New file | Contents |
|----------|----------|
| `StreamlineUtil.{h,cpp}` | `WideToUtf8`, `PathToUtf8`, `ResultToString`, `IdentityMatrix`, `EnumToString`, `Trim`, `CleanStreamlineSDKMessage`, `Contains`, `GetLastErrorMessage` |
| `StreamlineDiag.cpp` | `ShouldTraceStreamlineFrame`, `StreamlineLogCallback` (external linkage), `LogD3D12CommandQueueProxyState`, `LogDLSSGPresentState` |
| `StreamlineLoader.cpp` | `GetModuleDirectory`, `GetCurrentPluginDirectory`, `GetStreamlineSearchDirectories`, `FindStreamlineInterposer`, `JoinSearchDirectories`, `LoadAndInit` |
| `StreamlineFeatures.cpp` | `PostDevice`, `UpgradeD3D12DeviceForDLSSG` |
| `StreamlineFrame.cpp` | `EnsureFrameToken`, `AdvanceFrame`, `UpdateConstants` |
| `StreamlineReflex.cpp` | `GetConfiguredReflexMode`, `ConfigureReflex`, `ConfigureReflexForDLSSG`, `SleepReflexFrame`, `SetPCLMarker` |
| `StreamlineDLSSG.cpp` | `DisableDLSSGAfterError`, `ConfigureDLSSG`, `TagResourcesAndConfigure` |
| `StreamlineDLSS.cpp` | `Upscale`, `DestroyDLSSResources` |

Order: Util → DLSS → Loader → Frame → Reflex → Features → DLSSG.

**Done 2026-09-08 (one commit), reduced to four units.** `Streamline.cpp` 1111 → 485
(discovery, `LoadAndInit`, `PostDevice`, log callback, string helpers);
`StreamlineDLSS.cpp` (242: frame token, `Upscale`, `UpdateConstants`, destroy),
`StreamlineReflex.cpp` (126), `StreamlineFrameGeneration.cpp` (279),
`StreamlineInternal.{h,cpp}` (`ResultToString`, `EnumToString<T>`,
`ShouldTraceStreamlineFrame`, `GetConfiguredReflexMode` in `fo4cs::streamline`). The
eight-way split was not needed to get under the limit and would have scattered the
loader/feature-init sequence.

### 2d `DX12SwapChain.cpp` (872) → `src/Render/`

| New file | Contents |
|----------|----------|
| `DXGISwapChainProxy.cpp` | all 20 COM methods of `DXGISwapChainProxy` |
| `WrappedResource.cpp` | `WrappedResource::WrappedResource` |
| `DX12SwapChainOverlay.{h,cpp}` | `ResolveOverlayCallbacks` + its five file statics (must stay one TU) |
| `DX12PresentPolicy.{h,cpp}` | `FormatHRESULT`, trace-phase enum/name, `ShouldTracePresentFrame` (+statics), `ScopedPresentTraceFlag`, FG block enum/struct/menu list/queries |
| `DX12SwapChainCreate.cpp` | `CreateD3D12Device`, `CreateSwapChain`, `CreateInterop`, accessors, `GetBuffer`, `GetDevice` |
| `DX12SwapChainPresent.cpp` | `Present` (+statics), `WaitForCommandAllocator`, `BeginInteropCommandList`, `ExecuteInteropCommandListAndWait` |

Order: Proxy → WrappedResource → Overlay → PresentPolicy → Create → Present.

No `FALLOUT_*` conditionals exist in 2c or 2d.

**Done 2026-09-08 (one commit).** `DX12SwapChain.cpp` 872 → 316 (overlay callback
resolution, creation, accessors, fence helpers); `DX12SwapChainPresent.cpp` (430:
`Present` with its private trace/frame-generation-block policy helpers, which no other
function uses, so no `DX12PresentPolicy.h` was needed); `DXGISwapChainProxy.cpp` (106);
`WrappedResource.cpp` (58); `DX12SwapChainInternal.h` declares the overlay callback
state, now `fo4cs::render` externs instead of anonymous-namespace statics (still one
instance per DLL).

### Phase 2 result

`tools/check-file-sizes.ps1`: no file over 800 lines; only `Overlay.cpp` (658) is
above the 600 soft mark and is left for a later pass.

## Phase 3 — Diagnostics contract and log tooling

1. [x] `src/Diagnostics/LogEvents.h`: `Event` enum + `Code()` + templated
       `LogEvent(Event, fmt, args...)` writing `event=<CODE> <message>` (ERROR at
       error level). BOOT emitted from `InitializeLog`.
2. [x] Emitted: `DEVICE_READY` (`NotifyD3D11DeviceCreated` with interop flag; proxy
       swap chain ready), `HOOK_INSTALL` (IAT hooks, game render hooks, render backend
       hooks), `FEATURE_STATE` (three settings summaries), `RESOURCE_CREATE`
       (frame-gen + upscaling shared resources), `RESOURCE_RESET`
       (`DestroyUpscalingResources`), `ERROR` (proxy fallbacks, Present catch
       handlers, `slInit`/`slSetD3DDevice`/`slUpgradeInterface`, FSR context).
       `Reset()` is per-frame and deliberately not an event. `SHUTDOWN` is reserved:
       F4SE plugins have no unload callback.
3. [x] `Diagnostics/LogPaths.h::GetF4SELogDirectory()` replaces the two identical
       copies; `HangTrace.h` moved from `fo4cs::Diagnostics` to `fo4cs::diagnostics`.
4. [x] `tools/collect-runtime-log.ps1` (Before/After phases, change detection,
       `dist/logs/<stamp>-<variant>/session.json`). Smoke-tested; snapshot file is
       gitignored.
5. [x] `tools/validate-runtime-log.ps1`. Smoke-tested with synthetic PASS and FAIL logs
       (duplicate BOOT, missing events, ERROR all detected).
6. [x] `tools/check-file-sizes.ps1` (counts blank lines like `wc -l`).

## Phase 4 — Duplication and ownership cleanup

1. [x] `src/Upscaling/FeaturePanels.{h,cpp}` owns the three panels and INI writers;
       entry points register `PanelSpec`s. Deliberate behaviour changes: standalone
       registration now works (the old `TryRegister` asked the game EXE for
       `Overlay_RegisterPanel` and never reached `Overlay.dll`); NuclearGFX's Upscaler
       panel gains the DLSS preset combo; every save creates its INI directory.
       Verified with a standalone build (`build/PreNG-standalone`, AIO=OFF).
2. [x] `Platform/ModulePaths.h` (header-only, so `GetCurrentModuleDirectory` resolves
       per DLL) replaces four copies; `IsSiblingPluginAvailable(a_f4se, name)`
       generalises FrameGen's and Reflex's checks.
3. [x] Dead API removed after `rg` verification (no callers outside the cluster):
       `Override/ResetRenderTarget(s)`, `Override/ResetDepth`, `PatchSSRShader`,
       `GetDilateMotionVectorCS`, `GetOverrideLinearDepthCS`, `GetOverrideDepthCS`,
       `GetBSImagespaceShaderSSLRRaytracing` and their members. The four matching
       HLSL files in `package/Upscaler/F4SE/Plugins/Upscaler/` are now unused and
       left for a packaging decision.

## Phase 5 — Documentation

- [x] `docs/architecture.md`: targets, dependency direction (Render↔Upscaling cycle
      recorded as debt), hook ownership, lifecycle with events, threads, variants,
      failure behaviour, log contract.
- [x] `docs/validation-matrix.md`: automated checks, manual matrix, procedure,
      required events.
- [x] `.claude/docs/current-state.md` (in the main checkout, gitignored) updated
      2026-09-08.

## Not done / follow-ups

- Break the `Render` ↔ `Upscaling` include cycle (Render calls `Upscaling`,
  `Streamline`, `FidelityFX` from `DX11Hooks.cpp` and `DX12SwapChainPresent.cpp`).
  Candidate: a small callback/interface owned by Render that Upscaling registers.
- `Overlay.cpp` (658 lines) is above the 600 soft mark.
- `RuntimeContext` / `FeatureRegistry` from the review's P1 was not started; the
  singletons remain. The module boundaries above are the prerequisite.
- Deferred findings below are unfixed by design.
- Manual in-game validation of all variants (see `docs/validation-matrix.md`).

## Deferred findings (do not fix during move-only phases)

- `CreateFrameGenerationResources` allocates `Texture2D` with `new`, never deleted.
- `DXGISwapChainProxy::ResizeBuffers` forwards without rebuilding
  `swapChainBuffers[]` / `swapChainBufferWrapped[]`.
- `extern bool enbLoaded` declared in `UpscalerRenderBackend.cpp` but unused there.
- `GetUpscalingCB` holds the only owner of the constant buffer in a function-local
  static destroyed after D3D teardown.
- `RestoreNativeRenderState` (policy) performs resource work; ordering
  `UpdateSamplerStates(0)` then `UpdateRenderTargets(1,1)` is load-bearing.

## Verification commands

```bash
cmake --preset PreNG
cmake --build build/PreNG --config Release
pwsh -File tools/check-file-sizes.ps1
```

In-game validation (manual, BOSS): launch with the built DLLs, play the requested
scenario, exit, then run `tools/collect-runtime-log.ps1` and
`tools/validate-runtime-log.ps1`. Visual/gameplay observations are recorded
separately from build and log results.
