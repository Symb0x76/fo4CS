# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Quick Build (recommended)

```bat
BuildReleasePostAE.bat         # Fallout 4 post-next-gen-update + post-creation-club-AE
BuildReleasePostNG.bat         # Post-next-gen-update only
BuildReleasePreNG.bat          # Pre-next-gen-update
```

Set `FRAMEGEN=OFF` or `UPSCALER=OFF` or `REFLEX=OFF` env var before running to skip a target.

### CMake Direct

```bat
cmake -S . --preset=PostAE -DFRAMEGEN=ON -DUPSCALER=ON -DREFLEX=ON
cmake --build build/PostAE --config Release
```

CMake builds plugin DLLs only. The `BuildRelease*.bat` scripts are the only supported way to produce `dist/`.

### Single Plugin Build

Use the `Framegen`, `Upscaler`, or `Reflex` hidden presets to build individual plugins:

```bat
cmake -S . --preset=PostAE --preset=Framegen
cmake --build build/PostAE --config Release
```

### Development Config

```bat
cmake -S . --preset=PostAE -DFRAMEGEN=ON -DUPSCALER=ON
cmake --build build/PostAE --config RelWithDebInfo
```

Debug builds are compiled with `FO4CS_ENABLE_DEBUG_SETTINGS=1` which gates debug logging code paths.
Set `FO4CS_WAIT_FOR_DEBUGGER=1` env var to spin-lock until a debugger attaches on plugin load.

## Prerequisites

- Visual Studio 2022 with Desktop C++ workload
- vcpkg with `VCPKG_ROOT` env var set
- CMake 3.21+
- `git submodule update --init --recursive` (3 CommonLibF4 variants + Streamline + FidelityFX SDK)

## Architecture

### Plugin Architecture

Three F4SE plugin DLLs share a common `Core` OBJECT library:

- **Upscaler** (`UpscalerPlugin.cpp`) — registers F4SE listener for `kPostPostLoad`, installs D3D11 hooks for upscaling
- **FrameGen** (`FrameGenPlugin.cpp`) — if Upscaler plugin is present, reads settings and defers hook ownership; otherwise installs hooks
- **Reflex** (`ReflexPlugin.cpp`) — if Upscaler or FrameGen are present, defers D3D12 proxy ownership; otherwise installs hooks independently

`Core` compiles shared sources once. Plugin targets consume it via `$<TARGET_OBJECTS:Core>` and add their own plugin entry point.

### Core Classes (all singletons)

| Class | File | Role |
|-------|------|------|
| `Upscaling` | `Upscaler.h/cpp` | Central orchestrator: settings, render target patching, sampler overrides, depth/motion vector hooks, D3D11 compute shaders, frame limiter |
| `Upscaling` render backend | `UpscalerRenderBackend.cpp` | Heavy lifting: render target resize/override/reset, depth override pipeline, motion vector dilation, SSR shader patching, mip bias |
| `DX12SwapChain` | `DX12SwapChain.h/cpp` | D3D12 device/swapchain creation and D3D11→D3D12 interop (shared fences, wrapped resources) |
| `Streamline` | `Streamline.h/cpp` | NVIDIA Streamline SDK: DLSS upscaling, DLSS-G frame gen, Reflex latency reduction |
| `FidelityFX` | `FidelityFX.h/cpp` | AMD FidelityFX SDK: FSR upscaling and FSR frame generation |

### Data Flow

1. `DX11Hooks::Install()` hooks `D3D11CreateDeviceAndSwapChain` and `IDXGIFactory::CreateSwapChain`
2. On swapchain creation: hooks create a D3D12 device/swapchain proxy alongside the game's D3D11 pipeline
3. `DXGISwapChainProxy` intercepts `Present` to inject upscaling/frame-gen work
4. D3D11→D3D12 interop (shared fences + wrapped textures) lets D3D12-based upscalers read D3D11 render targets
5. `UpscalerRenderBackend` patches the game's render targets to upscaled resolutions and overrides depth/samplers

### Three Runtime Flavors

| Flavor | `#define` | CommonLibF4 submodule |
|--------|-----------|----------------------|
| Pre-NG | `FALLOUT_PRE_NG` | `extern/CommonLibF4PreNG` |
| Post-NG | `FALLOUT_POST_NG` | `extern/CommonLibF4PostNG` |
| Post-AE | `FALLOUT_POST_NG` + `FALLOUT_POST_AE` | `extern/CommonLibF4PostAE` |

Code guards with `#if defined(FALLOUT_POST_NG)` / `#if defined(FALLOUT_POST_AE)`. Pre-NG uses the older `F4SE::PluginInfo` struct; Post-NG and Post-AE use `F4SE::PluginVersionData`.

### Key Patterns

- **RE::ID offsets**: Game engine addresses resolved via `REL::ID(N)` — these differ per runtime flavor
- **Detours**: All API hooking uses Microsoft Detours via `stl::` wrappers in `PCH.h`
- **Settings**: MCM-only via `SimpleIni`. `Upscaling::settings` struct loaded at plugin init
- **D3D12 interop**: `DX12SwapChain` wraps D3D11 textures as D3D12 resources using `ID3D11Device5::CreateSharedHandle` + `ID3D12Device::OpenSharedHandleByName`
- **ENB integration**: Conditional compilation blocks in `UpscalerRenderBackend.cpp` handle ENB render target behavioral differences
