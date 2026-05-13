# FSR 3.0 — D3D11-native combined upscale + frame generation for PreNG only.
# PostNG uses FSR 3.1 with D3D12 interop from extern/FidelityFX-SDK.
#
# extern/Fidelity-SDK-DX11-PreNG (MapleHinata fork) provides:
#   - ffx_fsr3.h: combined ffxFsr3ContextDispatchUpscale API (upscale + FG)
#   - ffx_dx11.h: D3D11 backend (ffxGetResourceDX11, ffxGetCommandListDX11)
#   - Static libs: ffx_backend_dx11_x64, ffx_fsr3_x64

set(FFX_30_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/extern/Fidelity-SDK-DX11-PreNG")

if(NOT EXISTS "${FFX_30_ROOT}/sdk/CMakeLists.txt")
    message(WARNING "[FSR30] SDK not found at ${FFX_30_ROOT}. Run: git submodule update --init extern/Fidelity-SDK-DX11-PreNG")
    return()
endif()

# Only build the D3D11 backend — no D3D12 or Vulkan needed for FSR 3.0 on PreNG
set(FFX_API_VK OFF CACHE BOOL "" FORCE)
set(FFX_API_DX12 OFF CACHE BOOL "" FORCE)
set(FFX_API_DX11 ON CACHE BOOL "" FORCE)
set(FFX_FSR3 ON CACHE BOOL "" FORCE)
set(FFX_FSR ON CACHE BOOL "" FORCE)
set(FFX_AUTO_COMPILE_SHADERS ON CACHE BOOL "" FORCE)

# Add FSR 3.0 SDK as build subdirectory
add_subdirectory("${FFX_30_ROOT}/sdk" "${CMAKE_CURRENT_BINARY_DIR}/ffx30" EXCLUDE_FROM_ALL)

# Export include paths for consumption by Core target
set(FFX_30_INCLUDE_DIR "${FFX_30_ROOT}/sdk/include" PARENT_SCOPE)
set(FFX_30_LIBRARIES
    ffx_backend_dx11_x64
    ffx_fsr3_x64
    PARENT_SCOPE
)
message(STATUS "[FSR30] D3D11-native FSR 3.0 enabled for PreNG (${FFX_30_ROOT})")
