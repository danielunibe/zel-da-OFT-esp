# Water rendering audit

**Scope:** Shipwright 9.1.1 / Copper Bravo, read-only source audit. This is an architectural proposal, not an implementation.

## Evidence found

- Water is represented by ordinary Fast3D display lists and actor/scene resources, not by a renderer-wide water material. Relevant actor seams include `soh/src/overlays/actors/ovl_Bg_Mizu_Water`, `ovl_Bg_Mizu_Uzu`, `ovl_Bg_Mori_Idomizu`, `ovl_Bg_Spot03_Taki` and `ovl_Bg_Spot07_Taki`.
- The rendering path remains `Interpreter::GfxDpSetTile` / `GfxDpSetCombineMode` / `GfxDpSetOtherMode` -> the generated color combiner -> `libultraship/src/fast/backends/gfx_direct3d11.cpp` and `src/fast/shaders/directx/default.shader.hlsl`.
- Water-specific scrolling, alpha and combiner semantics are encoded in display lists/resources and must be identified per draw call. A global texture-name rule is insufficient.
- Framebuffer/depth infrastructure exists (`soh/soh/framebuffer_effects.c`, interpreter framebuffers), but the audit did not establish a reusable scene color/depth history or a general refraction pass.

## Current feature status

| Concern | Result | Confidence |
|---|---|---|
| Animated UV/scroll | Possible in existing Fast3D texture state; exact water instances require draw capture | PARTIAL |
| Alpha/blend | Controlled by original combiner and other-mode state | CONFIRMED |
| Depth interaction | Depth buffer exists; water-specific depth policy is draw-call dependent | PARTIAL |
| Reflection/refraction | No general water pass found | UNKNOWN / NOT PRESENT AS A GENERAL SYSTEM |
| Fog | Available through RSP/RDP fog state and shader inputs | CONFIRMED |

## Safe upgrade path

1. Instrument/capture water draw calls without changing output: resource path, `ColorCombinerKey`, other-mode bits, tile state, geometry mode, framebuffer target and draw order.
2. Add a per-material opt-in water classification. Unknown and 2D draws remain classic.
3. Start with a constant/animated normal-like perturbation only where the original pass is already translucent; preserve UV addressing, alpha and ordering.
4. Add Fresnel/specular as a separate opt-in shader variant. Refraction and reflection require scene color/depth contracts and should be deferred.
5. Keep the original path as a runtime fallback and compare screenshots and gameplay visibility at native and 3440x1440 output.

## Difficulty

- Classification and constant specular: **MEDIUM**.
- Animated normal/Fresnel: **MEDIUM-HIGH** because the combiner shader contract must remain compatible.
- Refraction: **HIGH**; needs stable scene color/depth sampling and ordering rules.
- Reflection: **HIGH** for planar/SSR; **VERY_HIGH** for RT.

## Guardrails

No gameplay waterbox, collision, actor timing, alpha test, texture wrap, or draw order changes. Do not infer that every blue/translucent texture is water.
