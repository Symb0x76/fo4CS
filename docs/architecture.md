# fo4CS architecture (`main`)

fo4CS is a set of F4SE plugins that add DLSS / FSR upscaling, DLSS-G / FSR frame
generation and NVIDIA Reflex to Fallout 4 by placing a companion D3D12 device and
swap chain behind the game's D3D11 device. This document describes ownership,
lifecycle and runtime boundaries after the 2026-09 refactor. Keep it current: a
change that adds a hook, a target or a log event must update the matching section.

## Build targets

| Target | Kind | Directory | Owns |
|--------|------|-----------|------|
| `Fo4cs.Runtime` | INTERFACE | — | `FALLOUT_PRE_NG` / `FALLOUT_POST_NG` / `FALLOUT_POST_AE` for the selected variant |
| `Fo4cs.Diagnostics` | headers | `src/Diagnostics/` | log event codes (`LogEvents.h`), F4SE log directory (`LogPaths.h`), opt-in hang trace (`HangTrace.h`) |
| `Fo4cs.Platform` | headers | `src/Platform/` | F4SE plugin boilerplate (`PluginCommon.h`), version-specific game accessors (`RE/`), module/path helpers (`ModulePaths.h`) |
| `Fo4cs.Render` | OBJECT | `src/Render/` | D3D11 IAT/vtable hooks, companion D3D12 device + swap chain, D3D11on12 shared textures, the `IDXGISwapChain` proxy, `Present` |
| `Fo4cs.Upscaling` | OBJECT | `src/Upscaling/` | settings + capability policy, render-backend hooks, proxy render targets / depth / samplers, frame-generation capture + composite, Streamline and FidelityFX integrations, feature panels |
| `Fo4cs.ImGui` | OBJECT | `extern/imgui/` | ImGui core (compiled once, absorbed by every DLL) |
| `Fo4cs.Overlay` | OBJECT | `src/Overlay/` | overlay host: ImGui D3D12/Win32 backends, panel registry, hotkey |
| `FrameGen`, `Reflex`, `Upscaler`, `NuclearGFX`, `Overlay` | SHARED (DLL) | `src/Plugins/` | one `F4SEPlugin_Load` each; composition root only |

Every plugin absorbs `Fo4cs.Render + Fo4cs.Upscaling + Fo4cs.ImGui`
(`fo4cs_link_runtime`); `NuclearGFX` and `Overlay` additionally absorb
`Fo4cs.Overlay`. OBJECT libraries are used deliberately: each DLL carries its own
copy of every object, so singletons (`Upscaling`, `DX12SwapChain`, `Streamline`,
`FidelityFX`) are per-DLL. Cross-DLL coordination happens only through exported C
functions (`OverlayAPI.h`) and F4SE plugin queries.

Compiler flags live in one place, `cmake/Fo4csTargets.cmake`
(`fo4cs_configure_target`). `extern/Streamline/include` must stay ahead of
`include/` in the include order because both ship an `sl.h`.

## Dependency direction

```
Plugins ──▶ Upscaling ──▶ Render ──▶ Platform ──▶ Diagnostics
   │            │  ▲          │
   │            └──┘ (Render calls back into Upscaling: DX11Hooks / Present)
   └──▶ Overlay ──▶ ImGui
```

`Render` and `Upscaling` are mutually dependent today (`DX11Hooks.cpp` and
`DX12SwapChainPresent.cpp` call `Upscaling`, `Streamline`, `FidelityFX`). This is
recorded as debt: the target boundary exists, the include graph is not yet acyclic.

## Hook ownership

| Hook | Installed by | Where | Variant-specific |
|------|--------------|-------|------------------|
| `D3D11CreateDeviceAndSwapChain`, `D3D11CreateDevice` IAT | `DX11Hooks::Install` | `Render/DX11Hooks.cpp` | no |
| `IDXGIFactory::CreateSwapChain(ForHwnd)` vtable | `DX11Hooks` (device path) | `Render/DX11Hooks.cpp` | no |
| `IDXGISwapChain::Present` (D3D11 fallback when no proxy) | `DX11Hooks` | `Render/DX11Hooks.cpp` | no |
| Renderer `WindowSizeChanged`, dynamic-resolution viewport, `DrawWorld` forward/reticle | `Upscaling::InstallHooks` | `Upscaling/UpscalingHooks.cpp` | `REL::ID` per variant |
| `BSGraphics::State::UpdateDynamicResolution`, `ImageSpaceEffectTemporalAA::IsActive`, `DrawWorld` pre-UI passes | `InstallUpscalerRenderBackendHooks` (Upscaler mode only) | `Upscaling/UpscalerRenderBackendHooks.cpp` | `REL::ID` per variant |
| Overlay `WndProc` | `Overlay::Initialize` | `Overlay/Overlay.cpp` | no |

Raw `REL::ID` literals appear only in the two hooks files and in
`Platform/RE/*.h`. New game-address access goes through `Platform/RE` accessors.

## Lifecycle

1. `F4SEPlugin_Load` (game main thread): `InitializeLog` → `BOOT` event;
   `Upscaling::Load*Settings` → `FEATURE_STATE`; `DX11Hooks::Install` →
   Streamline `LoadAndInit` (PostNG/PostAE only), FidelityFX `LoadFFX`, IAT hooks →
   `HOOK_INSTALL`; panel registration.
2. Game creates its D3D11 device/swap chain (render thread): the IAT hook builds
   the D3D12 device, swap chain and interop (`DX12SwapChain::Create*`) → `DEVICE_READY`,
   or falls back to plain D3D11 → `ERROR` with the reason. `NotifyD3D11DeviceCreated`
   → `DEVICE_READY` with the interop flag.
3. F4SE `kPostPostLoad`: `Upscaling::PostPostLoad` decides `renderBackendEnabled`
   and installs the game render hooks → `HOOK_INSTALL`.
4. Per frame (render thread): render-backend hooks override proxy render targets,
   depth and samplers; `DrawWorld` hooks capture HUD-less / UI / depth / motion
   vectors into shared textures; `DXGISwapChainProxy::Present` →
   `DX12SwapChain::Present` runs upscaling/frame generation on D3D12, draws the
   overlay and presents. Shared resources are created lazily → `RESOURCE_CREATE`.
5. Resolution or method change: `DestroyUpscalingResources` → `RESOURCE_RESET`,
   then lazy recreation.

There is no unload path; F4SE plugins live for the process. `SHUTDOWN` is reserved.

## Threads

Hooks and `Present` run on the game's render thread; `F4SEPlugin_Load` and
`kPostPostLoad` run on the main thread before rendering starts. The code assumes
the game serialises rendering (single render thread) and takes no locks of its own
except the overlay panel registry.

## Runtime variants

| | PreNG | PostNG | PostAE |
|--|-------|--------|--------|
| Macros | `FALLOUT_PRE_NG` | `FALLOUT_POST_NG` | `FALLOUT_POST_NG` + `FALLOUT_POST_AE` |
| CommonLibF4 | `extern/CommonLibF4PreNG` | `extern/CommonLibF4PostNG` | `extern/CommonLibF4PostAE` |
| Streamline (DLSS/DLSS-G/Reflex) | skipped (blocks `slInit`) | loaded | loaded |
| `REL::ID` set | old | NG | NG |
| Plugin metadata | `F4SEPlugin_Query` | `F4SEPlugin_Version` | `F4SEPlugin_Version` + compatible version list |

## Failure behaviour

Every optional dependency degrades instead of failing the load: missing
`sl.interposer.dll` → FSR selected and DLSS/Reflex reported unavailable in the
panel; D3D12 proxy creation failure → plain D3D11 with an `ERROR` event; Present
exceptions → `ERROR`, command list reset, `DXGI_ERROR_DEVICE_REMOVED` or the HRESULT.

## Log contract

Each plugin writes `Documents\My Games\Fallout4\F4SE\<Plugin>.log`. Structured
lines have the form `event=<CODE> <text>`; codes and their meaning are in
`src/Diagnostics/LogEvents.h`. `tools/validate-runtime-log.ps1` checks them; see
`docs/validation-matrix.md` for the manual procedure.
