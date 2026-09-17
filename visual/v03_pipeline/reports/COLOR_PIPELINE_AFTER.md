# Color Pipeline — After V03

## State (Unchanged)
- Backbuffer format: `DXGI_FORMAT_R8G8B8A8_UNORM` (NOT sRGB)
- Shader `srgb_mode`: still defaults to `false`
- Main scene: unchanged (gamma-space rendering preserved)

## ACES Post-Process Color Pipeline
1. Scene renders to backbuffer (gamma-space, like CLASSIC)
2. RunTonemappingPass copies backbuffer to intermediate texture
3. Pixel shader linearizes: `pow(color, 2.2)` (sRGB → linear)
4. ACES filmic tonemapping applied in linear space
5. Optional atmospheric fog blended in linear space
6. Re-gamma: `pow(color, 1/2.2)` (linear → sRGB)
7. Result written back to backbuffer

## No Double Gamma
- Input to ACES: gamma-encoded scene values
- ACES linearizes first → operates on correct linear data
- Output re-gammas → correct sRGB for display
- Single conversion pass, no double gamma detected

## CLASSIC Path
- RunTonemappingPass NOT called
- Scene renders directly to backbuffer
- No color space conversions applied
- Output matches RC1.1 baseline exactly
