# Lighting audit

## Current model

The N64 lighting contract is carried through `Fast::RSP` in `libultraship/include/fast/types.h`: current lights, ambient light, light coefficients, geometry mode, fog parameters and the model-view/projection matrices. `GfxSpVertex` in `libultraship/src/fast/interpreter.cpp` applies the vertex transform, lighting and fog inputs before triangles are buffered. The generated combiner shader preserves primitive, environment, vertex and texture inputs.

Game-side environment control is distributed through `soh/src/code/z_kankyo.c`, scene/room drawing and actor display lists. This is vertex/combiner lighting, not a modern physically based light list.

## Safe enhancement boundary

An enhanced layer may add a bounded response to the already computed material inputs, but must not replace the N64 light equations by default. Preserve fog, vertex color, primitive/environment colors and two-cycle combiners. Use an explicit material opt-in and a classic fallback.

Recommended order: (1) roughness/specular response, (2) optional ambient/contact contribution, (3) explicitly authored light probes or local lights. Do not add a global tonemap or brighter ambient as a substitute for scene lighting.

## Risks

- Raising ambient or exposure can reveal intentionally hidden geometry and change gameplay readability.
- Applying lighting to UI, billboards, particles or special combiners is unsafe.
- Per-pixel lighting would change the original low-frequency look and adds GPU cost at 3440x1440.

**Feasibility:** constants/specular **MODERATE**; a faithful enhanced lighting layer **HARD**; a general dynamic-light rewrite **VERY_HARD**.
