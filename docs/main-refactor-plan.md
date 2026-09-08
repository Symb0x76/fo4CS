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
2. [ ] Split `Core` (OBJECT) into OBJECT libraries, still all under `src/`:
       - `Fo4cs.ImGui` — `extern/imgui/*.cpp` core, `SKIP_PRECOMPILE_HEADERS`, `/W0`.
       - `Fo4cs.Render` — `DX11Hooks.*`, `DX12SwapChain.*`, `Buffer.h`.
       - `Fo4cs.Upscaling` — `Upscaler.*`, `UpscalerRenderBackend.cpp`,
         `Streamline.*`, `FidelityFX.*`.
       - `Fo4cs.Platform` (INTERFACE) — `PluginCommon.h`, `RE/*.h`, `include/PCH.h`.
       - `Fo4cs.Diagnostics` (INTERFACE) — `Diagnostics/*.h`.
       Keep `Core` as an INTERFACE aggregate for one commit, then switch every plugin
       to `fo4cs_link_runtime()` and delete `Core`.
       OBJECT libraries are kept (not STATIC) so link semantics match today's
       `$<TARGET_OBJECTS>` behavior exactly; no dead-stripping surprises.
3. [ ] Directory moves with `git mv`: `src/Render/`, `src/Upscaling/`,
       `src/Platform/`, `src/Diagnostics/`, `src/Plugins/` (the five `*Plugin.cpp`
       entry points). Update includes. `src/` stays the single PRIVATE include root.
4. [ ] Represent runtime variants as INTERFACE targets `Fo4cs.PreNG` /
       `Fo4cs.PostNG` / `Fo4cs.PostAE` carrying the `FALLOUT_*` definitions instead of
       `add_compile_definitions` in `XSEPlugin.cmake`. Print a one-line configure
       summary (variant, enabled plugins).

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

Extraction order: FramePacing → ShaderCompile → RuntimeProbe → SettingsIO+Settings →
RenderTargetIDs.h → Hooks → FrameGenResources → FrameGenCapture+Composite (one commit).

Constraints: `IsLoadingMenuOpen` and `NextHUDLessFrameID` become shared internal
helpers, not duplicated. `bool reticleFix` keeps external linkage. Raw `new Texture2D`
leaks in `CreateFrameGenerationResources` are preserved (deferred finding).

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

Constraints: `TraceRenderBackendStage`'s `previousStage` static lives in exactly one
`.cpp`. `CopyDepth` save/restore block and `Upscale`'s `Release()` pairing are not
split. `extern bool enbLoaded` in this file is unreferenced (deferred finding).

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

## Phase 3 — Diagnostics contract and log tooling

1. [ ] `src/Diagnostics/LogEvents.h`: keep Codex's `Event` enum + `Code()`; add
       `fo4cs::diagnostics::LogEvent(Event, fmt, args...)` that writes
       `event=<CODE> <message>`. Replace the ad-hoc `[Logger] Initialized` line with
       `BOOT` (done by Codex, uncommitted).
2. [ ] Emit `DEVICE_READY` (DX11Hooks device-created path), `HOOK_INSTALL`
       (`InstallHooks`, `InstallUpscalerRenderBackendHooks`, `DX11Hooks::Install`),
       `FEATURE_STATE` (settings summary after `ApplyRuntimeFallbacks`),
       `RESOURCE_CREATE` / `RESOURCE_RESET` (`CreateFrameGenerationResources`,
       `CreateUpscalingResources`, `DestroyUpscalingResources`, `Reset`), `ERROR`
       (the three catch handlers in `Present`, Streamline/FFX failures).
       Existing log text stays; the event token is prepended so grep-based checks
       and human reading both work.
3. [ ] Dedupe `GetLogDirectory` (`PluginCommon.h`) vs `GetHangTraceDirectory`
       (`HangTrace.h`) into `Diagnostics/LogPaths.h`.
4. [ ] `tools/collect-runtime-log.ps1`: snapshot the per-plugin logs before a manual
       session, then copy the post-session logs to `dist/logs/<timestamp>-<variant>/`.
5. [ ] `tools/validate-runtime-log.ps1`: assert required markers (`BOOT`,
       `DEVICE_READY`, `HOOK_INSTALL`, `FEATURE_STATE`), flag `ERROR`, flag duplicate
       `BOOT` from the same plugin, report counts. Exit code non-zero on failure.
6. [ ] `tools/check-file-sizes.ps1`: fail on any `src/**/*.cpp` over 800 lines
       (soft warning at 600).

## Phase 4 — Duplication and ownership cleanup

1. [ ] Feature settings panels are copy-pasted four times
       (`UpscalerPlugin.cpp`, `FrameGenPlugin.cpp`, `ReflexPlugin.cpp`,
       `AIOPlugin.cpp`: `DrawDLSSRuntimeNotice`, `DrawFrameGenerationBackendCombo`,
       `RenderPanel`, `SavePanel`, INI paths). Move into
       `src/Upscaling/FeaturePanels.{h,cpp}` with one `SaveUpscalerIni/SaveFrameGenIni/
       SaveReflexIni`; entry points only register.
2. [ ] `IsUpscalerPluginAvailable` + `GetCurrentPluginDirectory` duplicated in
       `FrameGenPlugin.cpp` and `ReflexPlugin.cpp`, and again in `Upscaler.cpp` and
       `Streamline.cpp` → `Platform/ModulePaths.{h,cpp}`.
3. [ ] Verify then delete dead `Upscaling` API surface reported by the outline
       (`OverrideRenderTargets/ResetRenderTargets/OverrideRenderTarget/ResetRenderTarget/
       OverrideDepth/ResetDepth/PatchSSRShader/GetDilateMotionVectorCS/
       GetOverrideLinearDepthCS/GetOverrideDepthCS/GetBSImagespaceShaderSSLRRaytracing`).
       Requires `rg` over `src/` and `package/` HLSL references before removal.

## Phase 5 — Documentation

- [ ] `docs/architecture.md`: target ownership, hook ownership, init order, thread
      assumptions, PreNG/PostNG/PostAE differences.
- [ ] `docs/validation-matrix.md`: variant × plugin × scenario table; manual
      checklist for BOSS; how to run the collector/validator.
- [ ] `.claude/docs/current-state.md` updated at every milestone.

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
