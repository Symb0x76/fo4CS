// Minimal local copy of the FidelityFX upscaler API that matches the
// repository's bundled ffx_api headers.

#pragma once

#include "ffx_api.h"
#include "ffx_api_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FFX_API_EFFECT_ID_UPSCALE
#define FFX_API_EFFECT_ID_UPSCALE 0x00010000u
#endif

#ifndef FFX_API_MAKE_EFFECT_SUB_ID
#define FFX_API_MAKE_EFFECT_SUB_ID(effectId, subversion) ((effectId & FFX_API_EFFECT_MASK) | (subversion & ~FFX_API_EFFECT_MASK))
#endif

#define FFX_UPSCALER_VERSION_MAJOR 4
#define FFX_UPSCALER_VERSION_MINOR 1
#define FFX_UPSCALER_VERSION_PATCH 0

#define FFX_UPSCALER_MAKE_VERSION(major, minor, patch) (((major) << 22) | ((minor) << 12) | (patch))
#define FFX_UPSCALER_VERSION                           FFX_UPSCALER_MAKE_VERSION(FFX_UPSCALER_VERSION_MAJOR, FFX_UPSCALER_VERSION_MINOR, FFX_UPSCALER_VERSION_PATCH)

enum FfxApiCreateContextUpscaleFlags
{
	FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE = (1 << 0),
	FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS = (1 << 1),
	FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION = (1 << 2),
	FFX_UPSCALE_ENABLE_DEPTH_INVERTED = (1 << 3),
	FFX_UPSCALE_ENABLE_DEPTH_INFINITE = (1 << 4),
	FFX_UPSCALE_ENABLE_AUTO_EXPOSURE = (1 << 5),
	FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION = (1 << 6),
	FFX_UPSCALE_ENABLE_DEBUG_CHECKING = (1 << 7),
	FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE = (1 << 8),
	FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION = (1 << 9),
};

enum FfxApiDispatchFsrUpscaleFlags
{
	FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW = (1 << 0),
	FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB = (1 << 1),
	FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ = (1 << 2),
};

#define FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE FFX_API_MAKE_EFFECT_SUB_ID(FFX_API_EFFECT_ID_UPSCALE, 0x00)
struct ffxCreateContextDescUpscale
{
	ffxCreateContextDescHeader header;
	uint32_t flags;
	struct FfxApiDimensions2D maxRenderSize;
	struct FfxApiDimensions2D maxUpscaleSize;
	ffxApiMessage fpMessage;
};

#define FFX_API_DISPATCH_DESC_TYPE_UPSCALE FFX_API_MAKE_EFFECT_SUB_ID(FFX_API_EFFECT_ID_UPSCALE, 0x01)
struct ffxDispatchDescUpscale
{
	ffxDispatchDescHeader header;
	void* commandList;
	struct FfxApiResource color;
	struct FfxApiResource depth;
	struct FfxApiResource motionVectors;
	struct FfxApiResource exposure;
	struct FfxApiResource reactive;
	struct FfxApiResource transparencyAndComposition;
	struct FfxApiResource output;
	struct FfxApiFloatCoords2D jitterOffset;
	struct FfxApiFloatCoords2D motionVectorScale;
	struct FfxApiDimensions2D renderSize;
	struct FfxApiDimensions2D upscaleSize;
	bool enableSharpening;
	float sharpness;
	float frameTimeDelta;
	float preExposure;
	bool reset;
	float cameraNear;
	float cameraFar;
	float cameraFovAngleVertical;
	float viewSpaceToMetersFactor;
	uint32_t flags;
};

#define FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION FFX_API_MAKE_EFFECT_SUB_ID(FFX_API_EFFECT_ID_UPSCALE, 0x0b)
struct ffxCreateContextDescUpscaleVersion
{
	ffxCreateContextDescHeader header;
	uint32_t version;
};

#ifdef __cplusplus
}
#endif
