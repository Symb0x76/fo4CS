#pragma once

#include <cstddef>
#include <utility>

// Feature Settings → GPU Constant Buffer
// Each Feature that exposes settings to shaders provides a struct (usually its
// GetCommonBufferData() return type). The template serializes all structs into
// a single contiguous buffer uploaded to the GPU each frame.
//
// Usage:  auto [data, size] = GetFeatureBufferData(inWorld);
//         context->PSSetConstantBuffers(slot, 1, &cb);
//
// As Features are ported, add their common-buffer structs to the parameter pack.

std::pair<const unsigned char*, size_t> GetFeatureBufferData(bool a_inWorld);
