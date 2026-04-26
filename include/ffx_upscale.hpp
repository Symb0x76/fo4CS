#pragma once

#include "ffx_api.hpp"
#include "ffx_upscale.h"

namespace ffx
{

template<>
struct struct_type<ffxCreateContextDescUpscale> : std::integral_constant<uint64_t, FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE>
{};

struct CreateContextDescUpscale : public InitHelper<ffxCreateContextDescUpscale>
{};

template<>
struct struct_type<ffxDispatchDescUpscale> : std::integral_constant<uint64_t, FFX_API_DISPATCH_DESC_TYPE_UPSCALE>
{};

struct DispatchDescUpscale : public InitHelper<ffxDispatchDescUpscale>
{};

template<>
struct struct_type<ffxCreateContextDescUpscaleVersion> : std::integral_constant<uint64_t, FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION>
{};

struct CreateContextDescUpscaleVersion : public InitHelper<ffxCreateContextDescUpscaleVersion>
{};

}
