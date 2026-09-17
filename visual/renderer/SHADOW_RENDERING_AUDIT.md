# Shadow Rendering Audit - Ocarina of Time PC

**Branch:** Copper Bravo (4aaad850bd5540cd77c2d83f3ad348d3b38605b2)
**Backend:** Windows / DirectX 11
**Source Root:** C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\source\shipwright
**Render Pipeline Doc:** visual_workspace/architecture/RENDER_PIPELINE.md
**Date:** 2026-09-16

---

## 1. Current Shadow System
### 1.1 Blob Shadows (How They Work)

Blob shadows are the dominant shadow type in the current system. They are flat, circular ground decals rendered as display lists (gCircleShadowDL) projected onto the actor's contact surface.

**Core rendering pipeline** (source/shipwright/soh/src/code/z_actor.c):

| Function | Line | Role |
|----------|------|------|
| ActorShadow_Draw() | 102 | Core projection: computes floor contact, fades alpha by height, applies scale/rotation, renders display list |
| ActorShadow_DrawCircle() | 146 | Wraps ActorShadow_Draw() with gCircleShadowDL |
| ActorShadow_DrawWhiteCircle() | 150 | Same but renders white shadow (used by fairies) |
| ActorShadow_DrawFoot() | 160 | Per-foot blob with per-light contribution |
| ActorShadow_DrawFeet() | 183 | Two-foot system with distance-based fade and per-light shadow casting |

**How ActorShadow_Draw() works (z_actor.c:102-144):**

1. Checks actor->floorPoly != NULL -- only draws if actor is standing on a surface
2. Computes vertical distance temp1 = actor->world.pos.y - actor->floorHeight, clamped to [0, 150]
3. Fades alpha: temp2 = 1.0f - (temp1 * (1.0f / 350)) -- shadows vanish at ~150 units above ground
4. Builds a transformation matrix via func_80038A28() using the floor polygon normal for correct projection
5. Optionally rotates by actor yaw (skipped for gCircleShadowDL)
6. Scales by actor->scale * shadowScale * temp2
7. Draws the display list on POLY_OPA_DISP using G_DPSETPRIMCOLOR(0,0,0,alpha) -- pure black with alpha

**Key structures** (z_actor.c:95-100):

ActorShape_Init(shape, yOffset, shadowDraw, shadowScale)
  - shape->shadowDraw = function pointer
  - shape->shadowScale = f32 (size multiplier)
  - shape->shadowAlpha = 255 (base opacity)
  - shape->yOffset = f32 (model vertical offset)

**Per-actor shadow configuration** is set at actor initialization via ActorShape_Init() calls in ~200+ actor source files under src/overlays/actors/ovl_*/. Each actor specifies its shadow function, scale, and alpha values.
### 1.2 Projected Shadows

Projected shadows are implemented within the blob shadow system itself -- the ActorShadow_Draw() function projects a 2D circle onto the 3D ground surface using:

- **Floor polygon data** (actor->floorPoly): The collision polygon under the actor determines the projection plane
- **Floor normal matrix** (func_80038A28()): Builds a modelview matrix aligned to the floor surface normal
- **Height-based fade**: Shadows become transparent as the actor lifts off the ground

This is a per-actor, per-frame projection -- each shadow is computed independently based on the actor position relative to its foot contact point.

**Additional projected shadows beyond blob system:**
- Boss blob shadows: BossTw (z_boss_tw.c:3459), BossMo (z_boss_mo.c:2683) -- use gCircleShadowDL with custom positioning for large bosses
- EnDekubaba (z_en_dekubaba.c:1269) -- custom projected blob at tentacle base
- EnKarebaba (z_en_karebaba.c:458) -- Deku Baba shadow projected from bound floor
- EnVb_Ball (z_en_vb_ball.c:318) -- Volvagia ball shadow with opacity control
- EnWallmas (z_en_wallmas.c:639) -- Wallmaster shadow with timer-based fade
### 1.4 Enemy Shadows

Most enemies use ActorShadow_DrawCircle(). Notable exceptions:

| Enemy | Shadow Type | Scale | File |
|-------|------------|-------|------|
| Stalfos | ActorShadow_DrawFeet() | 90.0f | z_en_test.c:268 |
| Moblin | ActorShadow_DrawFeet() | 90.0f | z_en_mb.c:312 |
| Dark Link | ActorShadow_DrawFeet() | 90.0f | z_en_zf.c:297 |
| Stinger | ActorShadow_DrawCircle() | 65.0f | z_en_eiyer.c:126 |
| Like Like | ActorShadow_DrawCircle() | 21.0f | z_en_mm.c:165 |
| Wallmaster | ActorShadow_DrawCircle() | 50.0f | z_en_floormas.c:132 |
| Phantom Ganon | ActorShadow_DrawCircle() | 20.0f | z_en_fw.c:200 |
| Volvagia | ActorShadow_DrawCircle() | varies | z_en_vb_ball.c |
| King Dodongo | ActorShadow_DrawCircle() | 250.0f | z_boss_dodongo.c:325 |
| Bongo Bongo | ActorShadow_DrawCircle() | 95.0f | z_boss_sst.c:289,340 |
| Gohma | ActorShadow_DrawCircle() | 150.0f | z_boss_goma.c:328 |
| Darknut | ActorShadow_DrawCircle() | 36.0f+ | Multiple files |
| Iron Knuckle | ActorShadow_DrawCircle() | 30.0f | z_en_ik.c:1456 |

**Enemy shadow overrides** (runtime shadowDraw changes):
- EnAni (z_en_ani.c:224): Switches to ActorShadow_DrawCircle after knockback recovery
- EnZl3 (z_en_zl3.c:2665): Zelda shadow with shadowAlpha = 0 (invisible until scripted)
- EnPo_Desert (z_en_po_desert.c:205,207): Shadow enabled/disabled based on Lens of Truth
- EnTrite (z_en_tr.c:440,442): Shadow toggled during cutscene state
### 1.5 Object Shadows

Objects use blob shadows with varying scales:

| Object | Scale | File |
|--------|-------|------|
| Item drops (rupees, etc.) | 6.0f | z_en_item00.c:492 |
| Signs/Ankou | 6.0f-12.0f | z_en_a_keep.c:117-122 |
| Vase | 6.0f | z_en_vase.c:34 |
| Lightbox | 6.0f | z_en_lightbox.c:54 |
| Bombchu | 12.0f | z_en_bombf.c:104 |
| Block | 8.8f | z_obj_bean.c:503 |
| Bomb | 16.0f | z_en_bom.c:97 |
| Skulltula | 13.0f | z_en_nutsball.c:75 |
| Chest | Varies | Actor-specific |
| Owl (Kaepora Gaebora) | 36.0f | z_en_owl.c:120 |
| Cuccos | 25.0f | z_en_niw.c:158 |
| Bean Plant | 8.8f | z_obj_bean.c:503 |

**Special object shadows:**
- BgJyaGoroiwa (z_bg_jya_goroiwa.c:105): Roller shadow at scale 9.0f, alpha 200
- BgHaka_Ship (Shadow Temple ship): Uses standard actor shadow system
- Bg_Spot18_Basket (z_bg_spot18_basket.c:145): Vase shadow at 15.0f
### 1.6 Environment Shadows

The N64 engine has no dedicated environment shadow system (no terrain self-shadowing, no building shadows, no contact occlusion). Shadow contributions from environment geometry are limited to:

1. Sun/Moon rendering (z_kankyo.c): Skybox sun and moon sprites with lighting effects -- not shadows per se, but contribute to scene lighting atmosphere
2. Light context: Dynamic lights (Lights struct) influence shadow direction/intensity for ActorShadow_DrawFoot() calculations
3. Scene lighting: Each scene defines up to 2 directional lights (lookat vectors) plus ambient light via the RSP light state
4. Room mesh: Environment meshes do not cast or receive shadows -- the floor (actor->floorPoly) is only used to determine the projection plane for actor blob shadows

### 1.7 Where Shadows Are Generated (Source Files, Functions)

**Core shadow system:**
| File | Function | Purpose |
|--------|----------|---------|
| z_actor.c | ActorShadow_Draw() (102) | Core projection renderer |
| z_actor.c | ActorShadow_DrawCircle() (146) | Standard blob shadow |
| z_actor.c | ActorShadow_DrawWhiteCircle() (150) | White blob (fairies) |
| z_actor.c | ActorShadow_DrawHorse() (156) | Horse-shaped blob |
| z_actor.c | ActorShadow_DrawFoot() (160) | Per-foot per-light blob |
| z_actor.c | ActorShadow_DrawFeet() (183) | Two-foot system |
| z_actor.c | ActorShape_Init() (95) | Initialize shadow state |
| z_actor.c | func_80038A28() | Floor normal matrix builder |
| z_player.c | Player draw path | Player shadow integration |
| z_boss_tw.c | Boss shadow draw | Large boss blob |
| z_boss_mo.c | Boss shadow draw | Large boss blob |
| z_player_lib.c:1437 | Invisible shadow suppression | Disable shadow when invisible |
| z_en_fish.c | Fish shadow (randomizer) | Fishsanity custom shadow
| File | Function | Purpose |
|--------|----------|---------|
| z_actor.c | ActorShadow_Draw() (102) | Core projection renderer |
| z_actor.c | ActorShadow_DrawCircle() (146) | Standard blob shadow |
| z_actor.c | ActorShadow_DrawWhiteCircle() (150) | White blob (fairies) |
| z_actor.c | ActorShadow_DrawHorse() (156) | Horse-shaped blob |
| z_actor.c | ActorShadow_DrawFoot() (160) | Per-foot per-light blob |
| z_actor.c | ActorShadow_DrawFeet() (183) | Two-foot system |
| z_actor.c | ActorShape_Init() (95) | Initialize shadow state |
| z_actor.c | func_80038A28() | Floor normal matrix builder |
| z_player.c | Player draw path | Player shadow integration |
| z_boss_tw.c | Boss shadow draw | Large boss blob |
| z_boss_mo.c | Boss shadow draw | Large boss blob |
| z_player_lib.c:1437 | Invisible shadow suppression | Disable shadow when invisible |
| z_en_fish.c | Fish shadow (randomizer) | Fishsanity custom shadow

**Shadow display list data (geometry definitions):**
| Display List | Shape | Used By |
|-------------|-------|---------|
| gCircleShadowDL | Circle/blob | ~95% of all actors |
| gFootShadowDL | Foot-shaped | Player, Stalfos, Moblin, Dark Link (via ActorShadow_DrawFoot) |
| gHorseShadowDL | Horse silhouette | Epona, Horse Link, Horse Zelda, Horse Ganon |
| sShadowMaterialDL | Shadow material | Referenced in OTRExporter (En_Jsjutan context) |

**Display lists are compiled geometry data** embedded in object/actor binary assets (not defined in source .c files -- they are part of ROM asset data loaded via gSegments).

### 1.8 What Shaders/Textures Are Used

**Shadow rendering uses the standard rendering pipeline -- no dedicated shadow shaders exist:**

1. Pipeline path: N64 Display List -> Fast3D Interpreter -> RSP/RDP state -> ColorCombiner -> DX11 Shader -> Draw Call
2. Shader: default.shader.hlsl (332 lines, libultraship/src/fast/shaders/directx/default.shader.hlsl) -- generated from N64 combiner settings via Prism processor
3. Combiner settings for shadows (from ActorShadow_Draw() at z_actor.c:115-116): gDPSetCombineLERP(0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, COMBINED, 0, 0, 0, COMBINED) -- selects PRIMITIVE x TEXEL0 (black x texture alpha) -- produces pure black modulated by texture/alpha
4. No dedicated shadow textures: Blob shadows use gCircleShadowDL which is textured geometry -- the display list itself contains vertex data and likely references a shadow texture (black with alpha gradient) from the ROM
5. PrimColor: Set to black (0,0,0) with alpha based on height fade -- GDPSETPRIMCOLOR(0, 0, 0, 0, 0, alpha)
6. Render pipeline stage: Shadows are drawn on POLY_OPA_DISP (opaque polygon pipeline) after regular actor geometry
7. DX11 shader: gfx_direct3d_common_build_shader() -> default.shader.hlsl (same pipeline as all geometry, combiner-determined)
8. Constant buffers: PerFrameCB (noise params), PerDrawCB (texture dimensions) -- no shadow-specific uniforms

**Key render states for shadows:**
- Depth test: Enabled (ZBUFFER geometry mode)
- Depth write: Standard (not decal mode)
- Blending: Standard alpha blend (based on combiner + G_BL_CLR_PRIM)
- Culling: Standard (per display list)

---

## 2. Future Upgrade Options

### 2.1 Better Blob Shadows

**Description:** Enhance the existing gCircleShadowDL blob shadows with softer edges, gradient alpha falloff, and higher resolution.

**Current limitation:** The current blob is a flat, hard-edged circle with uniform alpha falloff based purely on height distance. No soft penumbra, no noise/dither for edge smoothing.

**Upgrade path:**
- Replace gCircleShadowDL with a higher-resolution geometry (e.g., 32-segment circle instead of current low-poly)
- Add alpha gradient in the shadow texture (radial soft falloff from center to edge)
- Add optional noise/dither for edge anti-aliasing at lower resolutions
- Maintain backward compatibility via CVAR toggle

**Estimated effort:** 2-4 days (geometry + texture + shader mod)

### 2.2 Projected Raster Shadows

**Description:** Instead of blob decals, project a per-actor silhouette texture onto the ground from the light direction, computed via a render-to-texture pass or screen-space projection.

**Approach:**
- Render actor geometry to a shadow atlas from the light direction
- Project the resulting depth/visibility map as a textured decal on the ground
- Could use the existing ActorShadow_Draw() infrastructure but swap the display list for a projected RTT texture

**Upgrade path:**
- Create a shadow render target (e.g., 1024x1024 per light or atlas)
- Render visible actor geometry from light direction into depth buffer
- Sample depth buffer to determine shadowed pixels
- Project result as a texture onto ground contacts instead of gCircleShadowDL

**Estimated effort:** 2-3 weeks (RTT infrastructure, projection math, integration)

### 2.3 Shadow Mapping

**Description:** Implement standard shadow mapping (render scene depth from light perspective, sample in main pass).

**Challenge:** The current Fast3D pipeline is forward-rendered with N64 display lists. Shadow mapping requires: a depth-only pre-pass from each light perspective, a shadow map texture sampled in the main shader, and the current shader system (default.shader.hlsl) would need modification to support shadow map sampling.

**Upgrade path:**
1. Create depth pre-pass render targets per shadow-casting light
2. Modify default.shader.hlsl to accept shadow map + light-space matrix uniforms
3. Add shadow map sampling in pixel shader with PCF or basic comparison
4. Integrate into the draw call path (GfxSpTri1 or post-process)
5. CVAR toggle for enable/disable

**Estimated effort:** 4-6 weeks (pipeline architecture, shader changes, integration, testing)

### 2.4 Contact Shadows (Screen-Space)

**Description:** Approximate soft contact shadows by sampling the depth buffer near contact points, creating a darkening effect at object-ground and object-object junctions.

**Approach:**
- After main render, perform a screen-space pass that samples depth around contact points
- Darken pixels where geometry occludes the ground plane nearby
- Can use existing MSAA-resolved framebuffer or a separate depth-aware pass

**Upgrade path:**
1. After main scene render, resolve depth buffer (already available via mGameFbMsaaResolved when MSAA > 1)
2. Implement a screen-space compute/quad pass that: for each pixel, samples nearby depth values, computes horizon/occlusion angle relative to ground normal, darkens pixel based on occlusion factor
3. Composite onto final image via additive blend or lerp
4. CVAR toggle with intensity control

**Estimated effort:** 1-2 weeks (screen-space pass, depth sampling, compositing)

### 2.5 RT Shadows (DXR)

**Description:** Full hardware raytraced shadows using DirectX Raytracing (DXR) API.

**Challenge:** The current engine uses DX11 with a custom N64 interpretation layer. DXR requires: DX12 (DXR is not available in DX11), acceleration structure construction from game geometry, ray generation/shadow ray shaders, significant pipeline architecture overhaul.

**Upgrade path:**
1. Major: Port rendering backend from DX11 to DX12 (or add DX12 alongside DX11)
2. Build bottom-level acceleration structures (BLAS) from actor meshes
3. Build top-level acceleration structure (TLAS) per frame from actor positions
4. Create ray generation shader for primary rays + shadow rays
5. Integrate RT shadow passes into the render loop
6. Fallback to current blob shadows for non-RT mode
7. CVAR toggle: gRTShadows (0=off, 1=RT shadows, 2=hybrid RT + blob)

**Estimated effort:** 3-6 months (DX12 port, RT pipeline, performance optimization, testing)

---

## 3. Priority Ranking: Maximum Visual Impact with Minimum Complexity

| Priority | Upgrade | Visual Impact | Complexity | Effort | Impact/Complexity Ratio |
|----------|---------|--------------|------------|--------|------------------------|
| 1 | Contact Shadows (SS) | **** | ** | 1-2 wks | **Highest** -- immediate, noticeable improvement with moderate work |
| 2 | Better Blob Shadows | *** | * | 2-4 days | **Excellent** -- low-hanging fruit, easy to implement and test |
| 3 | Projected Raster Shadows | **** | *** | 2-3 wks | **Strong** -- significant quality jump per unit effort |
| 4 | Shadow Mapping | ***** | **** | 4-6 wks | **High impact** but substantial architecture changes |
| 5 | RT Shadows (DXR) | ***** | ***** | 3-6 months | **Highest quality** but requires DX12 port -- long-term goal |

**Recommended sequencing:**
1. Now: Better Blob Shadows (quick win, immediate visual improvement)
2. Next: Contact Shadows (significant perceptual improvement, builds on depth buffer infrastructure that already exists in MSAA-resolved path)
3. Then: Projected Raster Shadows (moderate complexity, bridges toward full shadow mapping)
4. Future: Shadow Mapping (major feature, requires shader pipeline changes)
5. Long-term: RT Shadows (DX12 migration prerequisite, multi-quarter project)

---

## 4. Risk Assessment

| Upgrade | Technical Risk | Compatibility Risk | Performance Risk | Mitigation |
|---------|---------------|-------------------|-----------------|------------|
| Better Blob Shadows | Low | Low | Negligible | CVAR toggle; default-off until stable |
| Contact Shadows | Medium | Low | Low-Medium (extra screen-space pass) | CVAR toggle; skip if MSAA not enabled; use existing resolved depth buffer |
| Projected Raster Shadows | Medium | Medium | Medium | CVAR toggle; fallback to blob shadows; shadow atlas size CVAR |
| Shadow Mapping | High | High | Medium (extra depth pre-pass per light) | CVAR toggle; require shader modification via default.shader.hlsl prism system; fallback to projected raster |
| RT Shadows (DXR) | Very High | Very High | Low (GPU-dependent) | Requires DX12 port first; CVAR toggle; full blob shadow fallback; hybrid mode (RT for key lights, blob for others) |

**Key risk factors:**
- **Shadow mapping shader integration:** The default.shader.hlsl uses the Prism combiner system. Adding shadow map sampling requires either modifying the Prism template or creating a separate shadow-aware shader path. Risk of regression in existing material rendering.
- **DX12 port:** Required for DXR. Would affect all rendering, not just shadows. Estimated to be a foundational architectural change.
- **Depth buffer precision:** Contact shadows and shadow mapping both depend on depth buffer quality. MSAA and internal resolution CVARs affect correctness.
- **Performance with many actors:** Current blob shadows are cheap (single display list per actor). Any upgrade that adds per-actor cost (RTT, shadow map rendering) must be carefully budgeted.
- **N64 accuracy:** Any shadow changes that alter visual output may break N64 accuracy mode expectations. Must preserve exact blob shadow behavior when CVARs are at defaults.

---

## Source Code Structure Reference


source/shipwright/
  soh/
    src/code/
      z_actor.c              # Core shadow system (ActorShadow_Draw, ActorShape_Init)
      z_actor.h              # ActorShape struct definition
      z_player.c             # Player shadow (ActorShadow_DrawFeet)
      z_player_lib.c         # Player shadow suppression (invisible link)
      z_kankyo.c             # Environment (sun/moon) lighting
      z_room.c               # Room drawing dispatch
    src/overlays/actors/
      ovl_*/                 # 200+ actor files, each calling ActorShape_Init()
    soh/Enhancements/
      game-interactor/
        GameInteractor_RawAction.cpp  # shadowDraw override (SetLinkInvisibility)
      randomizer/
        fishsanity.cpp     # Fishsanity_DrawEffShadow override
      TimeSavers/
        FasterShadowShip.cpp # Shadow Temple ship speed
    include/
      z64actor.h             # ActorShape struct (shadowDraw, shadowScale, shadowAlpha)
      functions.h            # ActorShadow function declarations
  libultraship/
    src/fast/
      interpreter.cpp        # N64 DL interpretation (GfxSpTri1 at line 1356)
      backends/
        gfx_direct3d11.cpp # DX11 backend (DrawTriangles, shader build)
        gfx_dxgi.cpp       # Swap chain, frame limiting
    include/fast/
      interpreter.h          # Interpreter state, ColorCombiner, TextureCache
      types.h               # RSP/RDP structs, F3DLight, LoadedVertex
    src/fast/shaders/directx/default.shader.hlsl  # Main HLSL shader template (332 lines)
  OTRExporter/
    DisplayListExporter.cpp    # References sShadowMaterialDL

visual_workspace/
  architecture/
    RENDER_PIPELINE.md         # Full render pipeline documentation (550 lines)
    MATERIAL_REGISTRY_PROPOSAL.md  # Future material system design
  materials/
    MATERIAL_CLASSIFICATION_SPEC.md
    UPSCALE_CANDIDATES.csv
  rtx/
    AI_ASSET_PIPELINE.md       # Offline AI texture pipeline (no runtime RT)

```
