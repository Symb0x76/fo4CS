cmake_minimum_required(VERSION 3.21)

project(
    fo4CS
    VERSION 1.3.4
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(XSEPlugin)

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

# ------------------------------------------------------------------
# Core: 共享实现的 OBJECT 库
# ------------------------------------------------------------------
add_library(Core OBJECT
    src/Buffer.h
    src/DX11Hooks.cpp
    src/DX11Hooks.h
    src/DX12SwapChain.cpp
    src/DX12SwapChain.h
    src/FidelityFX.cpp
    src/FidelityFX.h
    src/HDR.cpp
    src/HDRCalibration.cpp
    src/HDRCalibration.h
    src/HDRCalibration.hlsl
    src/HDR.h
    src/Streamline.cpp
    src/Streamline.h
    src/Upscaler.cpp
    src/Upscaler.h
    src/UpscalerRenderBackend.cpp
)

target_compile_features(Core PRIVATE cxx_std_23)

target_include_directories(Core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/Streamline/include
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    target_compile_definitions(Core PRIVATE _WINDOWS)
endif()
target_compile_definitions(Core PRIVATE
    _AMD64_
    "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:FO4CS_ENABLE_DEBUG_SETTINGS=1>"
)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    target_compile_definitions(Core PRIVATE _UNICODE "$<$<CONFIG:DEBUG>:DEBUG>")

    set(_CORE_RELEASE_OPTS "/Zi;/fp:fast;/GL;/Gy-;/Gm-;/Gw;/sdl-;/GS-;/guard:cf-;/O2;/Ob2;/Oi;/Ot;/Oy;/fp:except-")
    target_compile_options(Core PRIVATE
        /MP /W4 /WX /permissive-
        /Zc:alignedNew /Zc:auto /Zc:__cplusplus /Zc:externC /Zc:externConstexpr
        /Zc:forScope /Zc:hiddenFriend /Zc:implicitNoexcept /Zc:lambda
        /Zc:noexceptTypes /Zc:preprocessor /Zc:referenceBinding /Zc:rvalueCast
        /Zc:sizedDealloc /Zc:strictStrings /Zc:ternary /Zc:threadSafeInit
        /Zc:trigraphs /Zc:wchar_t /wd4200 /arch:AVX
    )
    target_compile_options(Core PUBLIC
        "$<$<CONFIG:DEBUG>:/fp:strict>" "$<$<CONFIG:DEBUG>:/ZI>"
        "$<$<CONFIG:DEBUG>:/Od>" "$<$<CONFIG:DEBUG>:/Gy>"
        "$<$<CONFIG:RELEASE>:${_CORE_RELEASE_OPTS}>"
    )
endif()

target_precompile_headers(Core PRIVATE include/PCH.h)
target_link_libraries(Core PUBLIC ${FO4CS_COMMON_DEPENDENCIES})

# ImGui core sources in Core — shared by all consumers for panel rendering.
# Feature DLLs call ImGui functions in their registered panel callbacks.
# Overlay.dll owns the D3D12 backend + context.
target_sources(Core PRIVATE
    extern/imgui/imgui.cpp
    extern/imgui/imgui_draw.cpp
    extern/imgui/imgui_tables.cpp
    extern/imgui/imgui_widgets.cpp
)
set(FO4CS_IMGUI_CORE_SOURCES
    extern/imgui/imgui.cpp
    extern/imgui/imgui_draw.cpp
    extern/imgui/imgui_tables.cpp
    extern/imgui/imgui_widgets.cpp
)
set_source_files_properties(${FO4CS_IMGUI_CORE_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_source_files_properties(${FO4CS_IMGUI_CORE_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
endif()

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
    set(HDR OFF)
endif()

if(FRAMEGEN)
    fo4cs_create_plugin(FrameGen FrameGen "FrameGen" ${FO4CS_COMMON_DEPENDENCIES})
    target_sources(FrameGen PRIVATE
        src/FrameGenPlugin.cpp
    )
    target_compile_definitions(FrameGen PRIVATE FRAMEGEN=1)
    target_link_libraries(FrameGen PRIVATE Core)
endif()

if(REFLEX)
    fo4cs_create_plugin(Reflex Reflex "Reflex" ${FO4CS_COMMON_DEPENDENCIES})
    target_sources(Reflex PRIVATE
        src/ReflexPlugin.cpp
    )
    target_compile_definitions(Reflex PRIVATE REFLEX=1)
    target_link_libraries(Reflex PRIVATE Core)
endif()

if(HDR)
    fo4cs_create_plugin(HDR HDR "HDR" ${FO4CS_COMMON_DEPENDENCIES})
    target_sources(HDR PRIVATE
        src/HDRPlugin.cpp
    )
    target_compile_definitions(HDR PRIVATE FO4CS_HDR=1)
    target_link_libraries(HDR PRIVATE Core)
endif()

if(UPSCALER)
    fo4cs_create_plugin(Upscaler Upscaler "Upscaler" ${FO4CS_COMMON_DEPENDENCIES})
    target_sources(Upscaler PRIVATE
        src/UpscalerPlugin.cpp
    )
    target_compile_definitions(Upscaler PRIVATE
        UPSCALER=1
        UPSCALER_LDR=1
    )
    target_link_libraries(Upscaler PRIVATE Core)
endif()

if(AIO)
    fo4cs_create_plugin(NuclearGFX NuclearGFX "NuclearGFX" ${FO4CS_COMMON_DEPENDENCIES})
    target_sources(NuclearGFX PRIVATE
        src/AIOPlugin.cpp
    )
    set_target_properties(NuclearGFX PROPERTIES OUTPUT_NAME "NuclearGFX")
    target_link_libraries(NuclearGFX PRIVATE Core)
endif()

if(OVERLAY)
    fo4cs_create_plugin(Overlay Overlay "Overlay" ${FO4CS_COMMON_DEPENDENCIES})
    target_sources(Overlay PRIVATE
        src/OverlayPlugin.cpp
        src/Overlay/Overlay.cpp
        src/Overlay/Overlay.h
        src/Overlay/OverlayWndProc.h
    )
    target_include_directories(Overlay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
    )
    target_link_libraries(Overlay PRIVATE Core)

    # D3D12/Win32 ImGui backends — only Overlay.dll needs these
    target_sources(Overlay PRIVATE
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
    set_source_files_properties(
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
    )
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        set_source_files_properties(
            extern/imgui/backends/imgui_impl_dx12.cpp
            extern/imgui/backends/imgui_impl_win32.cpp
            PROPERTIES COMPILE_OPTIONS "/W0"
        )
    endif()
endif()
