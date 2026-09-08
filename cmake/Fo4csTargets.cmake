# Fo4csTargets.cmake — shared target configuration for fo4CS.
#
# fo4cs_configure_target(<target> [NO_PCH])
#     Applies the compiler contract every fo4CS C++ target shares: C++23, the
#     Win32 / AMD64 definitions, the MSVC conformance switches, per-config
#     optimisation options and the shared precompiled header. Include roots are
#     `include/` (PUBLIC, stable headers) and `src/` (PRIVATE). NO_PCH is for
#     targets made only of third-party sources.
#
# fo4cs_add_object_library(<name> [NO_PCH] SOURCES <files...>)
#     Declares one internal runtime OBJECT library: configured with
#     fo4cs_configure_target and linked PUBLIC to FO4CS_COMMON_DEPENDENCIES so its
#     compile environment matches the plugins that absorb its objects.
#
# fo4cs_configure_plugin_link(<target>)
#     Linker contract for the SHARED plugin DLLs (warnings as errors, LTCG in
#     Release, incremental in Debug).
#
# fo4cs_link_runtime(<target>)
#     Links a plugin against the runtime OBJECT libraries listed in
#     FO4CS_RUNTIME_TARGETS. OBJECT libraries only contribute their object files
#     to *direct* dependents, so every plugin must call this itself instead of
#     relying on an aggregate target.

set(FO4CS_MSVC_RELEASE_OPTS
    "/Zi;/fp:fast;/GL;/Gy-;/Gm-;/Gw;/sdl-;/GS-;/guard:cf-;/O2;/Ob2;/Oi;/Ot;/Oy;/fp:except-")

function(fo4cs_configure_target target)
    cmake_parse_arguments(_fo4cs "NO_PCH" "" "" ${ARGN})

    target_compile_features("${target}" PRIVATE cxx_std_23)

    target_include_directories("${target}"
        PUBLIC
            ${PROJECT_SOURCE_DIR}/include
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
    )

    if(WIN32)
        target_compile_definitions("${target}" PRIVATE _WINDOWS)
    endif()
    target_compile_definitions("${target}" PRIVATE
        _AMD64_
        "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:FO4CS_ENABLE_DEBUG_SETTINGS=1>"
    )
    # Game runtime variant macros (FALLOUT_PRE_NG / FALLOUT_POST_NG / FALLOUT_POST_AE).
    target_link_libraries("${target}" PRIVATE Fo4cs.Runtime)

    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        target_compile_definitions("${target}" PRIVATE _UNICODE "$<$<CONFIG:DEBUG>:DEBUG>")

        target_compile_options("${target}" PRIVATE
            /MP /W4 /WX /permissive-
            /Zc:alignedNew /Zc:auto /Zc:__cplusplus /Zc:externC /Zc:externConstexpr
            /Zc:forScope /Zc:hiddenFriend /Zc:implicitNoexcept /Zc:lambda
            /Zc:noexceptTypes /Zc:preprocessor /Zc:referenceBinding /Zc:rvalueCast
            /Zc:sizedDealloc /Zc:strictStrings /Zc:ternary /Zc:threadSafeInit
            /Zc:trigraphs /Zc:wchar_t /wd4200 /arch:AVX
        )
        target_compile_options("${target}" PUBLIC
            "$<$<CONFIG:DEBUG>:/fp:strict>" "$<$<CONFIG:DEBUG>:/ZI>"
            "$<$<CONFIG:DEBUG>:/Od>" "$<$<CONFIG:DEBUG>:/Gy>"
            "$<$<CONFIG:RELEASE>:${FO4CS_MSVC_RELEASE_OPTS}>"
        )
    endif()

    if(NOT _fo4cs_NO_PCH)
        target_precompile_headers("${target}" PRIVATE ${PROJECT_SOURCE_DIR}/include/PCH.h)
    endif()
endfunction()

function(fo4cs_add_object_library name)
    cmake_parse_arguments(_fo4cs "NO_PCH" "" "SOURCES" ${ARGN})
    add_library("${name}" OBJECT ${_fo4cs_SOURCES})
    if(_fo4cs_NO_PCH)
        fo4cs_configure_target("${name}" NO_PCH)
    else()
        fo4cs_configure_target("${name}")
    endif()
    target_link_libraries("${name}" PUBLIC ${FO4CS_COMMON_DEPENDENCIES})
endfunction()

function(fo4cs_configure_plugin_link target)
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        target_link_options("${target}" PRIVATE
            /WX
            "$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
            "$<$<CONFIG:RELEASE>:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
        )
    endif()
endfunction()

# Third-party sources compiled inside our targets (ImGui) are not held to /W4 /WX
# and do not use the project PCH.
function(fo4cs_mark_third_party_sources)
    set_source_files_properties(${ARGN} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "/W0")
    endif()
endfunction()

function(fo4cs_link_runtime target)
    target_link_libraries("${target}" PRIVATE ${FO4CS_RUNTIME_TARGETS})
endfunction()
