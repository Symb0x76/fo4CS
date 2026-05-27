cmake_minimum_required(VERSION 3.21)

project(
    fo4CS
    VERSION 1.3.4
    LANGUAGES C CXX
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

# FSR runtime: DX12 proxy (PostNG/PostAE) uses amd_fidelityfx_dx12.dll dynamically.
# PreNG uses Fidelity-SDK-DX11-PreNG (native D3D11 backend, static link).

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

# DX11-native FSR FrameGen: sources are compiled directly into Core (see target_sources below)
# No static libs needed — direct compilation ensures consistent struct layouts and CRT linkage.

set(FEATURE_VERSIONS
    "		{\"LightLimitFix\"sv, {0,1,0}}"
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
# ------------------------------------------------------------------
# Core: D3D12 upscaling interop 共享 OBJECT 库
#   - Upscaler / FrameGen / Reflex / NuclearGFX 共享
#   - 不含 CommunityShaders Feature 框架
#   - 插件目标通过 $<TARGET_OBJECTS:Core> 引入编译产物
# ------------------------------------------------------------------
add_library(Core OBJECT
    src/Buffer.h
    src/DX11Hooks.cpp
    src/DX11Hooks.h
    src/DX12SwapChain.cpp
    src/DX12SwapChain.h
    src/FidelityFX.cpp
    src/FidelityFX.h
    src/FidelityFX_DX11.h
    src/Streamline.cpp
    src/Streamline.h
    src/Upscaler.cpp
    src/Upscaler.h
    src/UpscalerRenderBackend.cpp
)

# DX11-native FSR backend: compile directly into Core to avoid static-lib compatibility issues.
# Shader blobs are pre-compiled in the SDK's blob_accessors directory.
if(BUILD_PRE_NG)
	target_sources(Core PRIVATE
		# DX11 native backend
		src/FidelityFX_DX11.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/dx11/ffx_dx11.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/dx11/DXBCChecksum.c
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/dx11/md5.c
		# Shared backend code + shader blob accessors
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/ffx_shader_blobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/ffx_shader_blobs.h
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_frameinterpolation_shaderblobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_frameinterpolation_shaderblobs.h
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_opticalflow_shaderblobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_opticalflow_shaderblobs.h
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.h
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_fsr2_shaderblobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_fsr2_shaderblobs.h
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_fsr1_shaderblobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_fsr1_shaderblobs.h
		# FrameInterpolation effect
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/components/frameinterpolation/ffx_frameinterpolation.cpp
		# FSR3 Upscaler effect
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
		# OpticalFlow effect (dependency of FrameInterpolation)
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/components/opticalflow/ffx_opticalflow.cpp
		# Shared utilities
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/shared/ffx_assert.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/shared/ffx_object_management.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/shared/ffx_object_management.h
	)
	target_compile_definitions(Core PRIVATE FFX_FSR FFX_FSR3)
	# Suppress warnings in SDK-generated and SDK source files
	set(FFX_DX11_SOURCES
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/dx11/ffx_dx11.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/dx11/DXBCChecksum.c
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/dx11/md5.c
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/ffx_shader_blobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_frameinterpolation_shaderblobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_opticalflow_shaderblobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_fsr2_shaderblobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared/blob_accessors/ffx_fsr1_shaderblobs.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/components/frameinterpolation/ffx_frameinterpolation.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/components/opticalflow/ffx_opticalflow.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/shared/ffx_assert.cpp
		extern/Fidelity-SDK-DX11-PreNG/sdk/src/shared/ffx_object_management.cpp
	)
	set_source_files_properties(${FFX_DX11_SOURCES} PROPERTIES
		SKIP_PRECOMPILE_HEADERS ON
		COMPILE_OPTIONS "/W0"
	)
	target_include_directories(Core PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/extern/Fidelity-SDK-DX11-PreNG/sdk/src/backends/shared
		${CMAKE_CURRENT_SOURCE_DIR}/extern/Fidelity-SDK-DX11-PreNG/sdk/src/shared
		${CMAKE_CURRENT_SOURCE_DIR}/extern/Fidelity-SDK-DX11-PreNG/sdk/src/components
		# Generated shader permutation headers from the DX11 SDK shader compiler
		${CMAKE_CURRENT_SOURCE_DIR}/build/ffx-dx11-sdk/src/backends/shaders/dx11
	)
endif()

# ------------------------------------------------------------------
# DX11 FSR SDK shader compilation — auto-built as a pre-build step
# ------------------------------------------------------------------
if(BUILD_PRE_NG)
	set(FFX_SDK_SRC "${CMAKE_CURRENT_SOURCE_DIR}/extern/Fidelity-SDK-DX11-PreNG/sdk")
	set(FFX_SDK_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/build/ffx-dx11-sdk")
	set(FFX_SHADER_STAMP "${FFX_SDK_BUILD}/shaders_generated.stamp")

	if(CMAKE_GENERATOR_PLATFORM)
		set(FFX_SDK_CONFIGURE_ARGS -A "${CMAKE_GENERATOR_PLATFORM}")
	else()
		set(FFX_SDK_CONFIGURE_ARGS "")
	endif()

	add_custom_command(
		OUTPUT ${FFX_SHADER_STAMP}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${FFX_SDK_BUILD}"
		COMMAND ${CMAKE_COMMAND}
			-S "${FFX_SDK_SRC}"
			-B "${FFX_SDK_BUILD}"
			-G "${CMAKE_GENERATOR}"
			${FFX_SDK_CONFIGURE_ARGS}
			-DFFX_API_DX11=ON
			-DFFX_API_DX12=OFF
			-DFFX_API_VK=OFF
			-DFFX_BUILD_AS_DLL=OFF
			-DFFX_FSR3=ON
			-DFFX_FSR=ON
			-DFFX_AUTO_COMPILE_SHADERS=ON
		COMMAND ${CMAKE_COMMAND}
			--build "${FFX_SDK_BUILD}"
			--target ffx_backend_dx11_x64
			--config "$<CONFIG>"
		COMMAND ${CMAKE_COMMAND} -E touch "${FFX_SHADER_STAMP}"
		COMMENT "Building DX11 FSR SDK shader permutation headers..."
		WORKING_DIRECTORY "${FFX_SDK_SRC}"
	)

	add_custom_target(ffx_dx11_shaders DEPENDS ${FFX_SHADER_STAMP})
	add_dependencies(Core ffx_dx11_shaders)
endif()

target_compile_features(Core PRIVATE cxx_std_23)

target_include_directories(Core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/Streamline/include
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/Fidelity-SDK-DX11-PreNG/sdk/include
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

if(COMMUNITY_SHADERS)
# ------------------------------------------------------------------
# CoreCS: CommunityShaders Feature 框架 OBJECT 库
#   - 仅 community-shaders.dll 链接
#   - Feature 基类 + 生命周期 + ShaderCache + Deferred + Features
# ------------------------------------------------------------------
add_library(CoreCS OBJECT
    src/Core/BSShaderHooks.cpp
    src/Core/BSShaderHooks.h
    src/Core/CommunityShaders.cpp
    src/Core/CommunityShaders.h
    src/Core/Deferred.cpp
    src/Core/Deferred.h
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
    src/Core/Hooks.cpp
    src/Core/Hooks.h
    src/Core/IMenuItem.h
    src/Core/Menu.cpp
    src/Core/Menu.h
    src/Core/MenuRegistry.h
    src/Core/ShaderCache.cpp
    src/Core/ShaderCache.h
    src/Core/ShaderDB.cpp
    src/Core/ShaderDB.h
    src/Core/ShaderCompiler.cpp
    src/Core/ShaderCompiler.h
    src/Core/State.cpp
    src/Core/State.h
    src/Features/ShaderDump.cpp
    src/Features/ShaderDump.h
    src/Features/LightLimitFix.cpp
    src/Features/LightLimitFix.h
)

if(COMMUNITY_SHADERS)
    target_sources(CoreCS PRIVATE
        src/Features/Upscaling.cpp
        src/Features/FrameGeneration.cpp
        src/Features/Reflex.cpp
        src/Features/Overlay.cpp
        src/Overlay/Overlay.cpp src/Overlay/Overlay.h
    )
    target_include_directories(CoreCS PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/Streamline/include
    )
endif()

target_compile_features(CoreCS PRIVATE cxx_std_23)

target_include_directories(CoreCS
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
    ${FO4CS_GENERATED_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(WIN32)
    target_compile_definitions(CoreCS PRIVATE _WINDOWS)
endif()
target_compile_definitions(CoreCS PRIVATE
    _AMD64_
    "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:FO4CS_ENABLE_DEBUG_SETTINGS=1>"
)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    target_compile_definitions(CoreCS PRIVATE _UNICODE "$<$<CONFIG:DEBUG>:DEBUG>")

    target_compile_options(CoreCS PRIVATE
        /MP /W4 /WX /permissive-
        /Zc:alignedNew /Zc:auto /Zc:__cplusplus /Zc:externC /Zc:externConstexpr
        /Zc:forScope /Zc:hiddenFriend /Zc:implicitNoexcept /Zc:lambda
        /Zc:noexceptTypes /Zc:preprocessor /Zc:referenceBinding /Zc:rvalueCast
        /Zc:sizedDealloc /Zc:strictStrings /Zc:ternary /Zc:threadSafeInit
        /Zc:trigraphs /Zc:wchar_t /wd4200 /arch:AVX
    )
    target_compile_options(CoreCS PUBLIC
        "$<$<CONFIG:DEBUG>:/fp:strict>" "$<$<CONFIG:DEBUG>:/ZI>"
        "$<$<CONFIG:DEBUG>:/Od>" "$<$<CONFIG:DEBUG>:/Gy>"
        "$<$<CONFIG:RELEASE>:${_CORE_RELEASE_OPTS}>"
    )
endif()

target_precompile_headers(CoreCS PRIVATE include/PCH.h)
target_link_libraries(CoreCS PUBLIC ${FO4CS_COMMON_DEPENDENCIES})
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
endif()

# community-shaders 分支: 全进 CommunityShaders.dll
if(COMMUNITY_SHADERS)
    set(AIO OFF)
    set(FRAMEGEN OFF)
    set(REFLEX OFF)
    set(UPSCALER OFF)
    set(OVERLAY OFF)
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
        src/Overlay/Overlay.cpp
        extern/imgui/backends/imgui_impl_dx11.cpp
	    extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
    )
    target_include_directories(NuclearGFX PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui/backends
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
    set_target_properties(NuclearGFX PROPERTIES OUTPUT_NAME "NuclearGFX")
    target_link_libraries(NuclearGFX PRIVATE Core)
endif()

if(COMMUNITY_SHADERS)
    fo4cs_create_plugin(CommunityShaders CommunityShaders "CommunityShaders" ${FO4CS_COMMON_DEPENDENCIES})
    target_sources(CommunityShaders PRIVATE
        src/CommunityShadersPlugin.cpp
        extern/imgui/backends/imgui_impl_dx11.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
        extern/imgui/backends/imgui_impl_dx12.cpp
    )
    set_source_files_properties(
        extern/imgui/backends/imgui_impl_dx11.cpp
        extern/imgui/backends/imgui_impl_dx12.cpp
        extern/imgui/backends/imgui_impl_win32.cpp
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON
    )
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        set_source_files_properties(
        extern/imgui/backends/imgui_impl_dx11.cpp
        extern/imgui/backends/imgui_impl_dx12.cpp
            extern/imgui/backends/imgui_impl_win32.cpp
            PROPERTIES COMPILE_OPTIONS "/W0"
        )
    endif()
    target_compile_definitions(CommunityShaders PRIVATE COMMUNITY_SHADERS)
    target_link_libraries(CommunityShaders PRIVATE Core CoreCS)
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
