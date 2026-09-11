cmake_minimum_required(VERSION 3.21)

project(
    fo4CS
    VERSION 1.3.4
    LANGUAGES C CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(XSEPlugin)
include(Fo4csTargets)

find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

fo4cs_validate_plugin_selection()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

xseplugin_resolve_commonlib_root(CommonLibRoot CommonLibName)
xseplugin_resolve_commonlib_project(CommonLibPath "${CommonLibRoot}")

# Runtime variant contract: every fo4CS target links this (fo4cs_configure_target)
# and thereby sees FALLOUT_PRE_NG / FALLOUT_POST_NG / FALLOUT_POST_AE.
add_library(Fo4cs.Runtime INTERFACE)
target_compile_definitions(Fo4cs.Runtime INTERFACE ${FO4CS_RUNTIME_DEFINITIONS})

# FSR runtime: all game runtimes use amd_fidelityfx_dx12.dll through the D3D12 proxy.

if(F4SE_SUPPORT_XBYAK)
  set(COMMONLIBF4_ENABLE_XBYAK ON CACHE BOOL "Enable xbyak support for CommonLibF4" FORCE)
endif()

add_subdirectory(${CommonLibPath} ${CommonLibName} EXCLUDE_FROM_ALL)
xseplugin_resolve_commonlib_target(CommonLibTarget)

# The CommonLibF4 forks branch on the FALLOUT_* macros inside their own headers --
# RE/FO4Runtime.h picks REX::FModule over REL::Module::get() under
# FALLOUT_POST_AE, for one. Fo4cs.Runtime carries those macros to fo4CS targets,
# but commonlibf4 comes in through add_subdirectory and links nothing of ours, so
# its own translation units compiled with the macros absent and took the wrong
# branch. That is why PostAE never got past building CommonLibF4.
# CommonLibTarget is an ALIAS, which target_compile_definitions refuses, so name
# the concrete target the fork actually declares.
foreach(_commonlib_real CommonLibF4 commonlibf4)
    if(TARGET ${_commonlib_real})
        target_compile_definitions(${_commonlib_real} PUBLIC ${FO4CS_RUNTIME_DEFINITIONS})
        break()
    endif()
endforeach()

set(FO4CS_COMMON_DEPENDENCIES
    ${CommonLibTarget}
    spdlog::spdlog
    debug ${CMAKE_CURRENT_SOURCE_DIR}/include/detours/Debug/detours.lib
    optimized ${CMAKE_CURRENT_SOURCE_DIR}/include/detours/Release/detours.lib
    Microsoft::DirectXTK
    Microsoft::DirectXTex
    Microsoft::DirectX-Headers
    d3d12.lib
    dxgi.lib
    advapi32.lib
    magic_enum::magic_enum
    d3dcompiler.lib
)

set(FEATURE_VERSIONS
    "		{\"LightLimitFix\"sv, {0,1,0}}"
    "		{\"ExtendedMaterials\"sv, {0,1,0}}"
    "		{\"ShaderDump\"sv, {0,1,0}}"
    "		{\"Upscaling\"sv, {1,0,0}}"
    "		{\"FrameGeneration\"sv, {1,0,0}}"
    "		{\"Reflex\"sv, {1,0,0}}"
    "		{\"Overlay\"sv, {1,0,0}}"
)
string(REPLACE ";" ",\n" FEATURE_VERSIONS "${FEATURE_VERSIONS}")
set(FO4CS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FeatureVersions.h.in
    ${FO4CS_GENERATED_INCLUDE_DIR}/Core/FeatureVersions.h
    @ONLY
)

# ------------------------------------------------------------------
# Module targets. Every plugin DLL absorbs the objects of the OBJECT libraries in
# FO4CS_RUNTIME_TARGETS via fo4cs_link_runtime(); CommunityShaders.dll adds the
# Community Shaders layers on top. Names shared with `main` are kept identical so
# the two branches converge.
#
# Shared with `main`:
#   Fo4cs.Render      D3D11 hooks, companion D3D12 device/swap chain, interop
#   Fo4cs.Upscaling   Upscaler orchestration, render backend, Streamline, FidelityFX
#   Fo4cs.ImGui       ImGui core
#   Fo4cs.Overlay     overlay host (panel registry, ImGui backends)
# Community Shaders only:
#   Fo4cs.Shaders     shader cache/compiler/DB and the BSShader + D3D11 hooks
#   Fo4cs.Framework   Feature base, registry, globals, menu, state, diagnostics
#   Fo4cs.Passes      deferred/additive/tracker render passes
#   Fo4cs.Features    the concrete features
#
# src/Presentation and src/Plugins stay compiled into the plugin target itself:
# they are the composition root, not a reusable layer.
# ------------------------------------------------------------------

# Header-only modules carry no objects; they make ownership and the dependency
# direction visible (IDE folders, link graph). src/ is already every target's
# private include root, so linking them adds no flags.
add_library(Fo4cs.Diagnostics INTERFACE
    src/Diagnostics/HangTrace.h
    src/Diagnostics/LogEvents.h
    src/Diagnostics/LogPaths.h
)
add_library(Fo4cs.Platform INTERFACE
    src/Platform/ModulePaths.h
    src/Platform/PluginCommon.h
    src/Platform/RE/CameraData.h
    src/Platform/RE/SingletonAccessors.h
)
target_link_libraries(Fo4cs.Platform INTERFACE Fo4cs.Diagnostics)

# Third-party include roots must precede include/ because both extern/Streamline
# and include/ ship an sl.h; the extern copy is the one the build has always used.
set(FO4CS_STREAMLINE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/Streamline/include)
set(FO4CS_IMGUI_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
)

fo4cs_add_object_library(Fo4cs.Render
    SOURCES
        src/Render/Buffer.h
        src/Render/DX11Hooks.cpp
        src/Render/DX11Hooks.h
        src/Render/DX12SwapChain.cpp
        src/Render/DX12SwapChain.h
        src/Render/DX12SwapChainInternal.h
        src/Render/DX12SwapChainColorSpace.cpp
        src/Render/DX12SwapChainPresent.cpp
        src/Render/DXGISwapChainProxy.cpp
        src/Render/WrappedResource.cpp
        src/Render/PresentationMenuPolicy.h
        src/Render/RuntimeAdapter.cpp
        src/Render/RuntimeAdapter.h
)
target_include_directories(Fo4cs.Render BEFORE PUBLIC ${FO4CS_STREAMLINE_INCLUDE_DIR})
target_link_libraries(Fo4cs.Render PUBLIC Fo4cs.Platform)

fo4cs_add_object_library(Fo4cs.Upscaling
    SOURCES
        src/Upscaling/FidelityFX.cpp
        src/Upscaling/FidelityFX.h
        src/Upscaling/Streamline.cpp
        src/Upscaling/Streamline.h
        src/Upscaling/StreamlineInternal.cpp
        src/Upscaling/StreamlineInternal.h
        src/Upscaling/StreamlineDLSS.cpp
        src/Upscaling/StreamlineReflex.cpp
        src/Upscaling/StreamlineFrameGeneration.cpp
        src/Upscaling/Upscaler.cpp
        src/Upscaling/Upscaler.h
        src/Upscaling/UpscalerRenderBackend.cpp
        src/Upscaling/UpscalerRenderTargets.cpp
        src/Upscaling/UpscalerDepthBuffer.cpp
        src/Upscaling/UpscalerSamplerStates.cpp
        src/Upscaling/UpscalerRenderBackendShaders.cpp
        src/Upscaling/UpscalerRenderBackendHooks.cpp
        src/Upscaling/UpscalingHooks.cpp
        src/Upscaling/UpscalingInternal.cpp
        src/Upscaling/UpscalingInternal.h
        src/Upscaling/UpscalingRenderTargetIDs.h
        src/Upscaling/UpscalingShaderCompile.cpp
        src/Upscaling/UpscalingShaderCompile.h
        src/Upscaling/FramePacing.cpp
        src/Upscaling/FrameGenResources.cpp
        src/Upscaling/FrameGenCapture.cpp
        src/Upscaling/FrameGenComposite.cpp
)
target_include_directories(Fo4cs.Upscaling BEFORE PUBLIC ${FO4CS_STREAMLINE_INCLUDE_DIR})
target_link_libraries(Fo4cs.Upscaling PUBLIC Fo4cs.Platform)

# ImGui core — feature panels call ImGui from whichever DLL hosts the overlay.
set(FO4CS_IMGUI_CORE_SOURCES
    extern/imgui/imgui.cpp
    extern/imgui/imgui_draw.cpp
    extern/imgui/imgui_tables.cpp
    extern/imgui/imgui_widgets.cpp
)
fo4cs_add_object_library(Fo4cs.ImGui NO_PCH SOURCES ${FO4CS_IMGUI_CORE_SOURCES})
target_include_directories(Fo4cs.ImGui PUBLIC ${FO4CS_IMGUI_INCLUDE_DIRS})
fo4cs_mark_third_party_sources(${FO4CS_IMGUI_CORE_SOURCES})

set(FO4CS_RUNTIME_TARGETS Fo4cs.Render Fo4cs.Upscaling Fo4cs.ImGui)

if(COMMUNITY_SHADERS)

  # Feature base, registry, global instances, menu, runtime state, diagnostics.
  fo4cs_add_object_library(Fo4cs.Framework
    SOURCES
        src/Core/CommunityShaders.cpp
        src/Core/CommunityShaders.h
        src/Core/DebugSwitches.h
        src/Core/DiagnosticsFormatter.cpp
        src/Core/DiagnosticsFormatter.h
        src/Core/Feature.cpp
        src/Core/Feature.h
        src/Core/FeatureBuffer.cpp
        src/Core/FeatureBuffer.h
        src/Core/FeatureCategories.h
        src/Core/FeatureConstraints.cpp
        src/Core/FeatureConstraints.h
        src/Core/FeatureIssues.h
        src/Core/Globals.cpp
        src/Core/Globals.h
        src/Core/IMenuItem.h
        src/Core/Menu.cpp
        src/Core/Menu.h
        src/Core/MenuRegistry.h
        src/Core/PreNGEnvironment.h
        src/Core/State.cpp
        src/Core/State.h
  )
  target_include_directories(Fo4cs.Framework PUBLIC ${FO4CS_IMGUI_INCLUDE_DIRS})

  # Shader cache/compiler/database plus the engine and D3D11 shader hooks.
  fo4cs_add_object_library(Fo4cs.Shaders
    SOURCES
        src/Core/BSShaderHooks.cpp
        src/Core/ShaderHooks/PreNGDescriptorDiagnostics.cpp
        src/Core/ShaderHooks/PreNGDescriptorDiagnostics.h
        src/Core/ShaderHooks/PreNGDescriptorPredicates.cpp
        src/Core/ShaderHooks/PreNGDescriptorPredicates.h
        src/Core/ShaderHooks/PreNGRuntime.cpp
        src/Core/ShaderHooks/PreNGRuntime.h
        src/Core/ShaderHooks/PreNGShaderHookConstants.h
        src/Core/ShaderHooks/PreNGSwitches.cpp
        src/Core/ShaderHooks/PreNGSwitches.h
        src/Core/ShaderHooks/PreNGVanillaDumps.cpp
        src/Core/ShaderHooks/PreNGVanillaDumps.h
        src/Core/BSShaderHooks.h
        src/Core/D3D11DeviceHooks.cpp
        src/Core/Hooks.cpp
        src/Core/Hooks.h
        src/Core/HooksInternal.h
        src/Core/ShaderCache.cpp
        src/Core/ShaderCache.h
        src/Core/Shaders/ShaderCacheInternal.h
        src/Core/Shaders/ShaderSwitches.cpp
        src/Core/Shaders/ShaderDescriptorGates.cpp
        src/Core/Shaders/ShaderCacheLog.cpp
        src/Core/Shaders/ShaderObservation.cpp
        src/Core/Shaders/ShaderMetadata.cpp
        src/Core/ShaderCompiler.cpp
        src/Core/ShaderCompiler.h
        src/Core/ShaderDB.cpp
        src/Core/ShaderDB.h
  )
  target_link_libraries(Fo4cs.Shaders PUBLIC Fo4cs.Framework)

  # Render passes owned by Community Shaders.
  fo4cs_add_object_library(Fo4cs.Passes
    SOURCES
        src/Core/AdditivePasses.cpp
        src/Core/Deferred.cpp
        src/Core/Deferred.h
        src/Core/Deferred/DeferredGBuffer.cpp
        src/Core/Deferred/DeferredInternal.h
        src/Core/Deferred/DeferredTrace.cpp
        src/Core/DrawStateProof.cpp
        src/Core/LLFPixelTracker.cpp
  )
  target_link_libraries(Fo4cs.Passes PUBLIC Fo4cs.Framework)

  # Overlay host — same target name as `main`.
  fo4cs_add_object_library(Fo4cs.Overlay
    SOURCES
        src/Overlay/Overlay.cpp
        src/Overlay/Overlay.h
        src/Overlay/OverlayWndProc.h
  )
  target_link_libraries(Fo4cs.Overlay PUBLIC Fo4cs.ImGui Fo4cs.Framework)

  # The concrete features.
  fo4cs_add_object_library(Fo4cs.Features
    SOURCES
        src/Features/ExtendedMaterials.cpp
        src/Features/ExtendedMaterials.h
        src/Features/FrameGeneration.cpp
        src/Features/FrameGeneration.h
        src/Features/LightLimitFix.cpp
        src/Features/LightLimitFix.h
        src/Features/Overlay.cpp
        src/Features/Overlay.h
        src/Features/Reflex.cpp
        src/Features/Reflex.h
        src/Features/ShaderDump.cpp
        src/Features/ShaderDump.h
        src/Features/Upscaling.cpp
        src/Features/Upscaling.h
  )
  target_include_directories(Fo4cs.Features BEFORE PUBLIC ${FO4CS_STREAMLINE_INCLUDE_DIR})
  target_link_libraries(Fo4cs.Features PUBLIC Fo4cs.Framework)

  set(FO4CS_COMMUNITY_SHADERS_TARGETS
      Fo4cs.Framework Fo4cs.Shaders Fo4cs.Passes Fo4cs.Features Fo4cs.Overlay)
endif()

# ------------------------------------------------------------------
# 插件目标
# ------------------------------------------------------------------
if(AIO)
  set(FRAMEGEN OFF)
  set(REFLEX OFF)
  set(UPSCALER OFF)
endif()

# community-shaders 分支: 全进 CommunityShaders.dll
if(COMMUNITY_SHADERS)
  set(AIO OFF)
  set(FRAMEGEN OFF)
  set(REFLEX OFF)
  set(UPSCALER OFF)
  set(OVERLAY OFF)
endif()

message(STATUS "fo4CS ${PROJECT_VERSION}: runtime=${FO4CS_RUNTIME_VARIANT} [${FO4CS_RUNTIME_DEFINITIONS}] "
    "plugins: CommunityShaders=${COMMUNITY_SHADERS} NuclearGFX(AIO)=${AIO} FrameGen=${FRAMEGEN} "
    "Reflex=${REFLEX} Upscaler=${UPSCALER} Overlay=${OVERLAY}")

if(FRAMEGEN)
  fo4cs_create_plugin(FrameGen FrameGen "FrameGen" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(FrameGen PRIVATE
        src/Plugins/FrameGenPlugin.cpp
    )
  target_compile_definitions(FrameGen PRIVATE FRAMEGEN=1)
  fo4cs_link_runtime(FrameGen)
endif()

if(REFLEX)
  fo4cs_create_plugin(Reflex Reflex "Reflex" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(Reflex PRIVATE
        src/Plugins/ReflexPlugin.cpp
    )
  target_compile_definitions(Reflex PRIVATE REFLEX=1)
  fo4cs_link_runtime(Reflex)
endif()

if(UPSCALER)
  fo4cs_create_plugin(Upscaler Upscaler "Upscaler" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(Upscaler PRIVATE
        src/Plugins/UpscalerPlugin.cpp
    )
  target_compile_definitions(Upscaler PRIVATE
        UPSCALER=1
        UPSCALER_LDR=1
    )
  fo4cs_link_runtime(Upscaler)
endif()

if(AIO)
  fo4cs_create_plugin(NuclearGFX NuclearGFX "NuclearGFX" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(NuclearGFX PRIVATE
        src/Plugins/AIOPlugin.cpp
        src/Overlay/Overlay.cpp
        extern/imgui/backends/imgui_impl_dx11.cpp
     extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
  target_include_directories(NuclearGFX PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
    )
  fo4cs_add_imgui_sources(NuclearGFX
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
  set_target_properties(NuclearGFX PROPERTIES OUTPUT_NAME "NuclearGFX")
  fo4cs_link_runtime(NuclearGFX)
endif()

if(COMMUNITY_SHADERS)
  fo4cs_create_plugin(CommunityShaders CommunityShaders "CommunityShaders" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(CommunityShaders PRIVATE
        src/Plugins/CommunityShadersPlugin.cpp
        src/Presentation/PresentationBackend.cpp
        src/Presentation/PresentationBackend.h
        extern/imgui/backends/imgui_impl_dx11.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
        extern/imgui/backends/imgui_impl_dx12.cpp
    )
  fo4cs_add_imgui_sources(CommunityShaders
        extern/imgui/backends/imgui_impl_dx11.cpp
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
  target_compile_definitions(CommunityShaders PRIVATE COMMUNITY_SHADERS)
  fo4cs_link_runtime(CommunityShaders)
  target_link_libraries(CommunityShaders PRIVATE ${FO4CS_COMMUNITY_SHADERS_TARGETS})
endif()
if(OVERLAY)
  fo4cs_create_plugin(Overlay Overlay "Overlay" ${FO4CS_COMMON_DEPENDENCIES})
  target_sources(Overlay PRIVATE
        src/Plugins/OverlayPlugin.cpp
        src/Overlay/Overlay.cpp
        src/Overlay/Overlay.h
        src/Overlay/OverlayWndProc.h
    )
  target_include_directories(Overlay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
    )
  fo4cs_link_runtime(Overlay)
  target_link_libraries(Overlay PRIVATE Fo4cs.Overlay)

  # D3D12/Win32 ImGui backends — only Overlay.dll needs these
  target_sources(Overlay PRIVATE
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
  fo4cs_add_imgui_sources(Overlay
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
endif()
