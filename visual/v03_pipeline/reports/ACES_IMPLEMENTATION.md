# ACES Implementation — V03

## Shader: `tonemap_ps_source` (gfx_direct3d11.cpp)
- Input: scene texture (t0) + depth texture (t1)
- Linearizes scene via `pow(color, 2.2)` (sRGB → linear)
- Applies ACES filmic curve: `(x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14)`
- Blends atmospheric fog in linear space
- Re-gammas via `pow(color, 1/2.2)` (linear → sRGB)
- Output: tonemapped + fogged result to backbuffer

## State Save/Restore
Saves and restores: RTV, VS, PS, IL, topology, SRVs (all slots), samplers (all slots), rasterizer, blend, depth-stencil, constant buffers.

## Classic Bypass
- `IsEnhancedVisualProfile()` checked in `Interpreter::EndFrame()`
- RunTonemappingPass only called when profile == 1
- Classic: EndFrame() calls only `mContext->Flush()` then `Present()`
- Zero Classic overhead

## Per-Frame Resources
- Tonemap copy texture: resized on window resize (lazy creation)
- Fog constant buffer: updated via Map/Discard each frame when active
- No per-frame state object recreation (rasterizer/blend/depth-stencil created once)

## D3D11 State Integrity
- All previous pipeline state saved before post-process
- All previous pipeline state restored after post-process
- Full PS SRV slots (0-127) saved/restored to prevent stale bindings
