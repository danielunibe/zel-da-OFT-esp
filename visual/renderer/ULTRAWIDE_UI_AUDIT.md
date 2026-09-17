# Ultrawide UI audit

## Evidence

The renderer exposes aspect-aware viewport/scissor handling in `libultraship/src/fast/interpreter.cpp`; `soh/soh/ShipUtils.cpp` exposes aspect-ratio helpers. Game framebuffer helpers explicitly preserve a 4:3 slice when copying the N64-resolution buffer (`soh/soh/framebuffer_effects.c`). HUD and pause assets are drawn through screen-space rectangles and dedicated display lists, including `soh/src/overlays/misc/ovl_kaleido_scope` and the static texture headers under `soh/assets/textures`.

## Classification

- World rendering may use the selected widescreen/aspect path, subject to culling and camera rules.
- HUD, message boxes, fonts, item icons, maps and pause screens are screen-space systems and require separate safe-area validation.
- 4:3 framebuffer copies and wipe/cutscene effects can expose aspect assumptions even when ordinary gameplay looks correct.

## Recommendation

Keep UI in a virtual 4:3 coordinate space, then apply a deliberate anchor/safe-area transform. Validate center, edge, pause, text, maps, message boxes, file select and transitions. No PBR, tonemap, bloom or sharpening should touch fonts and pixel-perfect glyphs by default.

**Global correction:** viewport/safe-area policy and center anchoring. **Per-scene/per-effect exceptions:** framebuffer copies, cutscene overlays, wipe effects and any actor using screen-space coordinates.
