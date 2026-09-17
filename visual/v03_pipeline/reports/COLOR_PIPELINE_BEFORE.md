# Color Pipeline — Before V03

## State
- Backbuffer format: `DXGI_FORMAT_R8G8B8A8_UNORM` (NOT sRGB)
- Shader `srgb_mode`: defaults to `false`
- `SetSrgbMode()`: exists but never called
- Fragment shader output: linear values (no sRGB conversion)
- Monitor assumes sRGB transfer function from UNORM backbuffer
- Result: gamma-encoded textures displayed as-is (correct by accident)

## Implications
- ACES tonemapping was applied to gamma-space values (incorrect)
- No proper linear-light rendering pipeline
- ENHANCED profile was non-functional

## No changes to this state
V03 does NOT change the main scene rendering pipeline.
ACES is applied as a post-process that handles its own color space conversion.
