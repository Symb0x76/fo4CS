set(AIO ON CACHE BOOL "Build the unified NuclearGFX.dll (all-in-one)")
set(FRAMEGEN ON CACHE BOOL "Build the FrameGen plugin target")
set(REFLEX ON CACHE BOOL "Build the Reflex plugin target")
set(UPSCALER ON CACHE BOOL "Build the Upscaler plugin target")
set(OVERLAY ON CACHE BOOL "Build the Overlay plugin target")

function(fo4cs_validate_plugin_selection)
    if(NOT AIO AND NOT FRAMEGEN AND NOT REFLEX AND NOT UPSCALER AND NOT OVERLAY)
        message(FATAL_ERROR "At least one plugin target must be enabled. Set AIO, FRAMEGEN, REFLEX, UPSCALER, and/or OVERLAY to ON.")
    endif()
endfunction()

# Selects the game runtime variant. Sets in the caller's scope:
#   FO4CS_RUNTIME_VARIANT      PreNG | PostNG | PostAE
#   FO4CS_RUNTIME_DEFINITIONS  the FALLOUT_* macros fo4CS sources test
# The macros are attached to the Fo4cs.Runtime INTERFACE target (see
# CMakeLists.txt) rather than the directory, so CommonLibF4 and other
# subprojects are not compiled with them (none of them reads the macros).
function(xseplugin_resolve_commonlib_root out_root out_name)
    set(_selected 0)
    foreach(_flag BUILD_PRE_NG BUILD_POST_NG BUILD_POST_AE)
        if(${_flag})
            math(EXPR _selected "${_selected} + 1")
        endif()
    endforeach()
    if(_selected GREATER 1)
        message(FATAL_ERROR "Exactly one of BUILD_PRE_NG, BUILD_POST_NG, BUILD_POST_AE may be ON (got ${_selected}).")
    endif()

    if(BUILD_PRE_NG)
        set(_variant "PreNG")
        set(_definitions FALLOUT_PRE_NG)
        set(_commonlib_root "extern/CommonLibF4PreNG")
    elseif(BUILD_POST_NG)
        set(_variant "PostNG")
        set(_definitions FALLOUT_POST_NG)
        set(_commonlib_root "extern/CommonLibF4PostNG")
    elseif(BUILD_POST_AE)
        set(_variant "PostAE")
        set(_definitions FALLOUT_POST_NG FALLOUT_POST_AE)
        set(_commonlib_root "extern/CommonLibF4PostAE")
    else()
        message(FATAL_ERROR "No CommonLibF4 variant selected. Enable one of BUILD_PRE_NG, BUILD_POST_NG, or BUILD_POST_AE.")
    endif()

    set(FO4CS_RUNTIME_VARIANT "${_variant}" PARENT_SCOPE)
    set(FO4CS_RUNTIME_DEFINITIONS "${_definitions}" PARENT_SCOPE)
    set(${out_root} "${_commonlib_root}" PARENT_SCOPE)
    set(${out_name} "CommonLibF4" PARENT_SCOPE)
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

function(fo4cs_apply_plugin_defaults target)
    # The compiler contract shared with the runtime OBJECT libraries lives in
    # Fo4csTargets.cmake. Plugins additionally see their generated Plugin.h /
    # version.rc directory and get the DLL link contract.
    fo4cs_configure_target("${target}")

    target_include_directories(
        "${target}"
        PRIVATE
        ${FO4CS_GENERATED_INCLUDE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/${target}
    )

    fo4cs_configure_plugin_link("${target}")
endfunction()

function(fo4cs_configure_plugin_metadata target plugin_name plugin_display_name)
    set(FO4CS_PLUGIN_NAME "${plugin_name}")
    set(FO4CS_PLUGIN_DISPLAY_NAME "${plugin_display_name}")
    set(_plugin_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/cmake/${target}")
    file(MAKE_DIRECTORY "${_plugin_generated_dir}")

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Plugin.h.in
        ${_plugin_generated_dir}/Plugin.h
        @ONLY
    )

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.rc.in
        ${_plugin_generated_dir}/version.rc
        @ONLY
    )

    target_sources(
        "${target}"
        PRIVATE
        ${_plugin_generated_dir}/Plugin.h
        ${_plugin_generated_dir}/version.rc
    )
endfunction()

function(fo4cs_create_plugin target plugin_name plugin_display_name)
    add_library("${target}" SHARED)
    fo4cs_apply_plugin_defaults("${target}")
    fo4cs_configure_plugin_metadata("${target}" "${plugin_name}" "${plugin_display_name}")

    target_link_libraries(
        "${target}"
        PUBLIC
        ${ARGN}
    )
endfunction()
