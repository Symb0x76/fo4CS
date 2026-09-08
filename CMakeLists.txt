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

xseplugin_resolve_commonlib_root(CommonLibRoot CommonLibName)
xseplugin_resolve_commonlib_project(CommonLibPath "${CommonLibRoot}")

# FSR runtime: all game runtimes use amd_fidelityfx_dx12.dll through the D3D12 proxy.

if(F4SE_SUPPORT_XBYAK)
    set(COMMONLIBF4_ENABLE_XBYAK ON CACHE BOOL "Enable xbyak support for CommonLibF4" FORCE)
endif()

add_subdirectory(${CommonLibPath} ${CommonLibName} EXCLUDE_FROM_ALL)
xseplugin_resolve_commonlib_target(CommonLibTarget)

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
    magic_enum::magic_enum
    d3dcompiler.lib
)

set(FO4CS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# ------------------------------------------------------------------
# Module targets. Every plugin DLL absorbs the objects of the OBJECT libraries
# in FO4CS_RUNTIME_TARGETS via fo4cs_link_runtime(); the overlay hosts
# additionally absorb Fo4cs.Overlay.
#
# Fo4cs.Diagnostics  (headers) log event codes, hang trace
# Fo4cs.Platform     (headers) F4SE plugin boilerplate, version-specific RE accessors
# Fo4cs.Render       D3D11 hooks, companion D3D12 device/swap chain, D3D11on12 interop
# Fo4cs.Upscaling    Upscaler orchestration, render backend, Streamline, FidelityFX
# Fo4cs.ImGui        ImGui core — feature DLLs call ImGui in their panel callbacks
# Fo4cs.Overlay      Overlay host (ImGui D3D12/Win32 backends + panel registry)
# ------------------------------------------------------------------

# Header-only modules carry no objects; they make ownership and the dependency
# direction visible (IDE folders, link graph). src/ is already every target's
# private include root, so linking them adds no flags.
add_library(Fo4cs.Diagnostics INTERFACE
    src/Diagnostics/HangTrace.h
    src/Diagnostics/LogEvents.h
)
add_library(Fo4cs.Platform INTERFACE
    src/Platform/PluginCommon.h
    src/Platform/RE/CameraData.h
    src/Platform/RE/SingletonAccessors.h
)
target_link_libraries(Fo4cs.Platform INTERFACE Fo4cs.Diagnostics)

# Third-party include roots must precede include/ because both extern/Streamline
# and include/ ship an sl.h; the extern copy is the one the build has always used.
set(FO4CS_STREAMLINE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/Streamline/include)

fo4cs_add_object_library(Fo4cs.Render
    SOURCES
        src/Render/Buffer.h
        src/Render/DX11Hooks.cpp
        src/Render/DX11Hooks.h
        src/Render/DX12SwapChain.cpp
        src/Render/DX12SwapChain.h
)
target_include_directories(Fo4cs.Render BEFORE PUBLIC ${FO4CS_STREAMLINE_INCLUDE_DIR})
target_link_libraries(Fo4cs.Render PUBLIC Fo4cs.Platform)

fo4cs_add_object_library(Fo4cs.Upscaling
    SOURCES
        src/Upscaling/FidelityFX.cpp
        src/Upscaling/FidelityFX.h
        src/Upscaling/Streamline.cpp
        src/Upscaling/Streamline.h
        src/Upscaling/Upscaler.cpp
        src/Upscaling/Upscaler.h
        src/Upscaling/UpscalerRenderBackend.cpp
)
target_include_directories(Fo4cs.Upscaling BEFORE PUBLIC ${FO4CS_STREAMLINE_INCLUDE_DIR})
target_link_libraries(Fo4cs.Upscaling PUBLIC Fo4cs.Platform)

fo4cs_add_object_library(Fo4cs.ImGui NO_PCH
    SOURCES
        extern/imgui/imgui.cpp
        extern/imgui/imgui_draw.cpp
        extern/imgui/imgui_tables.cpp
        extern/imgui/imgui_widgets.cpp
)
target_include_directories(Fo4cs.ImGui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
)
fo4cs_mark_third_party_sources(
    extern/imgui/imgui.cpp
    extern/imgui/imgui_draw.cpp
    extern/imgui/imgui_tables.cpp
    extern/imgui/imgui_widgets.cpp
)

fo4cs_add_object_library(Fo4cs.Overlay
    SOURCES
        src/Overlay/Overlay.cpp
        src/Overlay/Overlay.h
        src/Overlay/OverlayWndProc.h
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
)
target_link_libraries(Fo4cs.Overlay PUBLIC Fo4cs.ImGui)
fo4cs_mark_third_party_sources(
    extern/imgui/backends/imgui_impl_dx12.cpp
    extern/imgui/backends/imgui_impl_win32.cpp
)

set(FO4CS_RUNTIME_TARGETS Fo4cs.Render Fo4cs.Upscaling Fo4cs.ImGui)

if(BUILD_PRE_NG)
    set(FO4CS_RUNTIME_FLAVOR "PreNG")
elseif(BUILD_POST_NG)
    set(FO4CS_RUNTIME_FLAVOR "PostNG")
elseif(BUILD_POST_AE)
    set(FO4CS_RUNTIME_FLAVOR "PostAE")
else()
    set(FO4CS_RUNTIME_FLAVOR "Unknown")
endif()

# ------------------------------------------------------------------
# 插件目标
# ------------------------------------------------------------------
if(AIO)
    set(FRAMEGEN OFF)
    set(REFLEX OFF)
    set(UPSCALER OFF)
endif()

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
    )
    set_target_properties(NuclearGFX PROPERTIES OUTPUT_NAME "NuclearGFX")
    fo4cs_link_runtime(NuclearGFX)
    target_link_libraries(NuclearGFX PRIVATE Fo4cs.Overlay)
endif()

if(OVERLAY)
    fo4cs_create_plugin(Overlay Overlay "Overlay" ${FO4CS_COMMON_DEPENDENCIES})
    target_sources(Overlay PRIVATE
        src/Plugins/OverlayPlugin.cpp
    )
    # Overlay renders through DX12SwapChain (Fo4cs.Render) and hosts the ImGui
    # D3D12/Win32 backends (Fo4cs.Overlay).
    fo4cs_link_runtime(Overlay)
    target_link_libraries(Overlay PRIVATE Fo4cs.Overlay)
endif()
