# Post-processing audit

## Confirmed framebuffer seams

`libultraship/src/fast/backends/gfx_direct3d11.cpp` owns the DirectX 11 render targets and draw submission. `libultraship/src/fast/backends/gfx_dxgi.cpp` owns swap-chain/presentation concerns. The game has explicit framebuffer effect helpers in `soh/soh/framebuffer_effects.c`, including pause, blur, reusable and N64-resolution framebuffers. These effects are not evidence of a universal final-image postprocess chain.

## Current/unknown

| Feature | Audit result |
|---|---|
| Final tonemap | No general modern tonemap contract established |
| Gamma/color correction | Backend/game behavior requires runtime capture; do not assume a linear HDR pipeline |
| Bloom | Existing blur framebuffer is used for game effects; a global bloom pass is not established |
| Sharpen | No general final sharpen contract established |
| Motion blur | A framebuffer comment identifies effects affected by interpolation, but no general velocity-buffer pipeline was found |
| AA | MSAA is an existing window/renderer setting; it is not DLAA |

## Low-risk route

Introduce one optional final-pass contract after the scene/UI separation is understood. Keep HUD/fonts/UI out of bloom and sharpening unless explicitly intended. Start with exposure/contrast controls at neutral defaults, then subtle bloom on an authored emissive mask. Motion blur stays off by default and requires camera/object velocity or a documented approximation.

**Feasibility:** neutral postprocess pass **MODERATE**; safe bloom **MODERATE-HARD**; motion blur **HARD**.
