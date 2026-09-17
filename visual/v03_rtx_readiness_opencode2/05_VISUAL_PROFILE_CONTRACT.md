# 05 — VISUAL PROFILE CONTRACT

## Strict Behavioral Rules for CLASSIC vs ENHANCED

---

### 1. Design Principle

The visual profile system provides two rendering modes that differ **only in visual presentation**. No gameplay, physics, input, audio, or UI behavior changes between profiles.

---

### 2. CLASSIC Profile Contract

**Promise: Identical to Ship of Harkinian v9.1.1 default renderer.**

| Aspect | Rule |
|---|---|
| Geometry | Original N64 polygon count and silhouette |
| Textures | Original textures, no filtering changes beyond N64 bilinear/trilinear |
| Lighting | N64 vertex lighting + color combiner emulation |
| Shadows | Original blob/projected shadows as implemented by game code |
| Fog | Original N64 fog pipeline (fog_mul, fog_offset, fog_color) |
| Tonemapping | NONE — linear/sRGB passthrough |
| PBR | NONE — all materials use N64 response |
| Post-process | NONE — no bloom, no SSAO, no sharpening |
| SSAO | OFF |
| Draw calls | Original N64 batching |
| Resolution | User-selected, but internal rendering matches SoH defaults |
| UI | Unchanged — HUD, menus, text all original |

**Verification**: CLASSIC profile must produce pixel-identical output to unmodified SoH (within floating-point tolerance).

---

### 3. ENHANCED Profile Contract

**Promise: Modern visual transformations applied to original geometry.**

| Aspect | Rule |
|---|---|
| Geometry | SAME as Classic — no polygon additions |
| Textures | SAME as Classic — no replacement |
| Lighting | Same base lighting, optional enhanced response via material properties |
| Shadows | Same as Classic — no new shadow systems |
| Fog | Same N64 fog + optional atmospheric depth enhancement (configurable) |
| Tonemapping | ACES filmic tonemapping applied as post-process |
| PBR | Material-dependent roughness/metallic/emissive metadata overlays |
| Post-process | Optional: tonemapping, SSAO, atmospheric enhancement |
| SSAO | Optional — configurable strength |
| Draw calls | Same count — MaterialRegistry is metadata-only |
| Resolution | Same as Classic |
| UI | UNCHANGED — PROTECTED_2D exclusion applies |
| Color pipeline | Linear intermediate target for correct tonemapping |

---

### 4. Strict Boundaries

#### What MUST NOT differ between profiles:

| System | Rule |
|---|---|
| Gameplay logic | Identical |
| Physics/collision | Identical |
| Camera behavior | Identical |
| Enemy AI | Identical |
| Save/load | Identical |
| UI text/position | Identical |
| HUD icons | Identical |
| Input handling | Identical |
| Audio | Identical |
| Cutscene triggers | Identical |
| Scene transitions | Identical |
| Visibility/draw distance | Identical |
| Frame rate target | Identical |

---

### 5. Performance Contract

| Metric | CLASSIC | ENHANCED |
|---|---|---|
| Frame time delta | Baseline | < +2ms at 3440×1440 |
| Draw call count | Baseline | SAME (no new geometry) |
| Texture memory | Baseline | SAME (no new textures) |
| VRAM delta | Baseline | < +100MB |
| Shader complexity | Baseline | +1 PBR lighting pass |

---

### 6. Safety Rules

| # | Rule |
|---|---|
| 1 | No gameplay advantage in Enhanced mode (no brighter enemies, no hidden details revealed) |
| 2 | No visibility advantage (fog must not be reduced below N64 intent) |
| 3 | No UI corruption (PROTECTED_2D always excluded) |
| 4 | No physics changes (material properties are visual only) |
| 5 | No audio changes |
| 6 | No save format changes |
| 7 | No network protocol changes |
| 8 | No input mapping changes |
| 9 | No cutscene camera changes |
| 10 | Profile switch is instant and safe at any point |

---

### 7. Feature Ownership Matrix

| Feature | CLASSIC | ENHANCED | Owner |
|---|---|---|---|
| N64 vertex lighting | YES | YES | Original SoH |
| Color combiner emulation | YES | YES | Original SoH |
| N64 fog | YES | YES | Original SoH |
| Atmospheric depth | NO | OPTIONAL | V03 new |
| ACES tonemapping | NO | YES | V03 new |
| PBR material response | NO | YES | V03 new |
| SSAO | NO | OPTIONAL | V03 new |
| Material classification | NO | YES | V03 new |
| Shadow enhancement | NO | NO (future) | — |
| Reflection enhancement | NO | NO (future) | — |

---

### 8. Fallback Requirement

If any ENHANCED feature fails or causes issues:
1. System must fall back to CLASSIC rendering
2. No partial rendering states allowed
3. CVar toggle must be responsive (<100ms to take effect)
4. No resource leaks on profile switch

---

### 9. Testing Contract

| Test | CLASSIC | ENHANCED |
|---|---|---|
| Visual parity with SoH v9.1.1 | REQUIRED | N/A |
| No gameplay change | REQUIRED | REQUIRED |
| No UI corruption | REQUIRED | REQUIRED |
| Frame time within budget | REQUIRED | REQUIRED |
| Profile switch safety | REQUIRED | REQUIRED |
| No memory leaks | REQUIRED | REQUIRED |
