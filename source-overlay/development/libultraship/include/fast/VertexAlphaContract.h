#pragma once

#include <cstdint>

namespace Fast {

// DXR coverage consumes the original F3DVtx.cn[3]. The separately captured
// raster color alpha remains untouched and may contain the Fast3D fog factor.
constexpr uint32_t PackDxrVertexColor(uint32_t rasterColor, uint8_t sourceAlpha) {
    return (rasterColor & 0x00FFFFFFu) | (static_cast<uint32_t>(sourceAlpha) << 24u);
}

constexpr uint8_t RasterVertexAlpha(uint32_t rasterColor) {
    return static_cast<uint8_t>(rasterColor >> 24u);
}

// Compile-time contract cases: no fog; fog factor zero; and differing source
// alpha/fog factor. These assertions run in every translation unit including
// this header and make the separation reproducible without a test harness.
static_assert(RasterVertexAlpha(0xFF112233u) == 255u &&
              static_cast<uint8_t>(PackDxrVertexColor(0xFF112233u, 255u) >> 24u) == 255u,
              "No-fog raster and DXR alpha must both preserve source alpha");
static_assert(RasterVertexAlpha(0x00112233u) == 0u &&
              static_cast<uint8_t>(PackDxrVertexColor(0x00112233u, 255u) >> 24u) == 255u,
              "Fog alpha must remain raster-only while DXR uses source alpha");
static_assert(RasterVertexAlpha(0x40112233u) == 64u &&
              static_cast<uint8_t>(PackDxrVertexColor(0x40112233u, 128u) >> 24u) == 128u,
              "Raster fog factor and DXR source alpha must remain independent");

} // namespace Fast
