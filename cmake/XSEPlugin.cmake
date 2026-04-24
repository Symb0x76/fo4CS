add_library("${PROJECT_NAME}" SHARED)

target_compile_features(
    "${PROJECT_NAME}"
    PRIVATE
    cxx_std_23
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(AddCXXFiles)
add_cxx_files("${PROJECT_NAME}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Plugin.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc
    @ONLY
)

target_sources(
    "${PROJECT_NAME}"
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc
)

target_precompile_headers(
    "${PROJECT_NAME}"
    PRIVATE
    include/PCH.h
)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

if(WIN32)
    add_compile_definitions(_WINDOWS)
endif()

add_compile_definitions(_AMD64_)

function(xseplugin_resolve_commonlib_root out_root out_name)
    if(BUILD_PRE_NG)
        add_compile_definitions(FALLOUT_PRE_NG)
        set(_commonlib_name "CommonLibF4")
        set(_commonlib_root "extern/CommonLibF4PreNG")
        set(GamePath ${Fallout4VRPath})
    elseif(BUILD_POST_NG)
        add_compile_definitions(FALLOUT_POST_NG)
        set(_commonlib_name "CommonLibF4")
        set(_commonlib_root "extern/CommonLibF4PostNG")
    elseif(BUILD_POST_AE)
        add_compile_definitions(FALLOUT_POST_NG)
        add_compile_definitions(FALLOUT_POST_AE)
        set(_commonlib_name "CommonLibF4")
        set(_commonlib_root "extern/CommonLibF4PostAE")
    else()
        message(FATAL_ERROR "No CommonLibF4 variant selected. Enable one of BUILD_PRE_NG, BUILD_POST_NG, or BUILD_POST_AE.")
    endif()

    set(${out_root} "${_commonlib_root}" PARENT_SCOPE)
    set(${out_name} "${_commonlib_name}" PARENT_SCOPE)
endfunction()

function(xseplugin_resolve_commonlib_project out_path commonlib_root)
    unset(_resolved_path)
    foreach(_commonlib_candidate
        "${commonlib_root}/CommonLibF4"
        "${commonlib_root}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_commonlib_candidate}/CMakeLists.txt")
            set(_resolved_path "${_commonlib_candidate}")
            break()
        endif()
    endforeach()

    if(NOT _resolved_path)
        message(FATAL_ERROR "Could not locate a CommonLibF4 CMake project under ${commonlib_root}")
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_resolved_path}/.gitmodules"
        AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_resolved_path}/lib/commonlib-shared/CMakeLists.txt")
        message(FATAL_ERROR
            "CommonLibF4 at ${_resolved_path} is missing nested submodules. "
            "Run: git submodule update --init --recursive ${commonlib_root}")
    endif()

    set(${out_path} "${_resolved_path}" PARENT_SCOPE)
endfunction()

function(xseplugin_resolve_commonlib_target out_target)
    if(TARGET CommonLibF4::CommonLibF4)
        set(${out_target} "CommonLibF4::CommonLibF4" PARENT_SCOPE)
    elseif(TARGET CommonLibF4::commonlibf4)
        set(${out_target} "CommonLibF4::commonlibf4" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "CommonLibF4 target not found after adding subdirectory.")
    endif()
endfunction()

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    add_compile_definitions(_UNICODE)

    target_compile_definitions(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:DEBUG>")

    set(SC_RELEASE_OPTS "/Zi;/fp:fast;/GL;/Gy-;/Gm-;/Gw;/sdl-;/GS-;/guard:cf-;/O2;/Ob2;/Oi;/Ot;/Oy;/fp:except-")

    target_compile_options(
        "${PROJECT_NAME}"
        PRIVATE
        /MP
        /W4
        /WX
        /permissive-
        /Zc:alignedNew
        /Zc:auto
        /Zc:__cplusplus
        /Zc:externC
        /Zc:externConstexpr
        /Zc:forScope
        /Zc:hiddenFriend
        /Zc:implicitNoexcept
        /Zc:lambda
        /Zc:noexceptTypes
        /Zc:preprocessor
        /Zc:referenceBinding
        /Zc:rvalueCast
        /Zc:sizedDealloc
        /Zc:strictStrings
        /Zc:ternary
        /Zc:threadSafeInit
        /Zc:trigraphs
        /Zc:wchar_t
        /wd4200 # nonstandard extension used : zero-sized array in struct/union
        /arch:AVX
    )

    target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:DEBUG>:/fp:strict>")
    target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:DEBUG>:/ZI>")
    target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:DEBUG>:/Od>")
    target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:DEBUG>:/Gy>")
    target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:RELEASE>:${SC_RELEASE_OPTS}>")

    target_link_options(
        ${PROJECT_NAME}
        PRIVATE
        /WX
        "$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
        "$<$<CONFIG:RELEASE>:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
    )
endif()

xseplugin_resolve_commonlib_root(CommonLibRoot CommonLibName)
xseplugin_resolve_commonlib_project(CommonLibPath "${CommonLibRoot}")

if(F4SE_SUPPORT_XBYAK)
    set(COMMONLIBF4_ENABLE_XBYAK ON CACHE BOOL "Enable xbyak support for CommonLibF4" FORCE)
endif()

add_subdirectory(${CommonLibPath} ${CommonLibName} EXCLUDE_FROM_ALL)
xseplugin_resolve_commonlib_target(CommonLibTarget)

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC
    ${CommonLibTarget}
)
