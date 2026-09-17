# ATMOSPHERIC FOG FINAL REPORT — V03.1 MASTER CONSOLIDATION
**Date:** 2026-09-17  
**Project:** Ocarina of Time PC — Couch Edition  
**Owner:** Gemini 3.8 High / High Flash  
**Atmospheric Fog Status:** `IMPLEMENTED_AND_ACTIVE`  
**Color Source:** `N64_SCENE_DATA`  
**Global Blue Haze:** `NO`  
**Original N64 Fog Preserved:** `PASS`  

---

## 1. Resolution of the Hardcoded Fog Contradiction

The previous V03 draft utilized a hardcoded blue-grey constant `(0.45, 0.55, 0.70)` with a fixed strength of `0.15` across all scenes. This contradicted the core Couch Edition art design principle of preserving the original scene artistic intent.

In V03.1, this contradiction is resolved:
1. **Dynamic N64 Scene Fog Source:** `Interpreter` monitors `mRdp->fog_color` and `mRsp->geometry_mode & G_FOG`.
2. **Supplemental Depth Haze:** The postprocess derives its base color directly from the active scene's fog color rather than an arbitrary blue constant.
3. **Indoor / Outdoor Discrimination:**
   - When scene fog is enabled by the game (`mSceneHasFog == true`), such as the Kokiri morning atmosphere or Hyrule Field horizons, the supplemental depth pass strengthens that authentic color (`strength = 0.12`).
   - When scene fog is inactive (e.g. indoor chambers, Temple of Time), the supplemental pass scales down to near-zero (`strength = 0.02`) and blends with a soft neutral ambient tone `(0.40, 0.42, 0.45)`, preventing an unmotivated blue wash over dark indoor stone.
4. **Zero Global Blue Haze:** `GLOBAL_BLUE_HAZE: NO`.

---

## 2. Preservation of N64 Fog Invariants

The underlying N64 vertex-fog implementation remains 100% unaltered:
- `mRsp->fog_mul` and `mRsp->fog_offset` continue to govern vertex alpha when `G_FOG` is active.
- `mRdp->fog_color` continues to provide the rasterizer fog color.
- No gameplay visibility advantage is created.
- The enhanced atmospheric pass operates *on top* of the native rendered image using the depth buffer, providing realistic atmospheric perspective without weakening original depth cues.

---

## 3. Pilot Scene Behavior Matrix

| Pilot Scene | N64 Fog State | Enhanced Fog Base Color | Enhanced Fog Strength | Artistic Response |
|---|---|---|---|---|
| **Temple of Time** | Minimal / Neutral | Warm Ambient Neutral `(0.40, 0.42, 0.45)` | `0.02` (Subtle) | Preserves sacred cathedral stone mood; no blue cast |
| **Kokiri Forest** | Active Forest Haze | Game Fog Color (`mRdp->fog_color`) | `0.12` (Organic) | Enhances natural forest canopy depth; foliage stays vibrant |
| **Hyrule Field** | Active Distance Fog | Game Fog Color (`mRdp->fog_color`) | `0.12` (Atmospheric) | Enhances distant horizon perspective while keeping sky relationship |
| **CLASSIC Profile** | Native N64 | N/A | `0.00` (Bypassed) | Exactly matches RC1.1 baseline |
