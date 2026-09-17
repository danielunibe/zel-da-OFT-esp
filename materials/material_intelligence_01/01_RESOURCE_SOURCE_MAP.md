# 01 - Resource Source Map

STATUS: READ-ONLY analysis of `source/shipwright/soh/assets`. Nothing outside
`visual_workspace/material_intelligence_01` was written. No game/runtime/live files touched.

Every entry below is a SOURCE FACT (files/declarations observed on disk). Interpretive
statements are marked INFERENCE.

## A. Header asset-declaration layer (1080 headers parsed)

Decomp-style asset headers declare OTR resources twice:
```c
#define d<Symbol> "__OTR__<otr/path>"
static const ALIGN_ASSET(2) char <Symbol>[] = d<Symbol>;
```
- `source_file` = the header path under `assets/`.
- Identifiers = C symbols, stable across builds as long as headers are regenerated
  from the same XML. Stable enough for MaterialRegistry exact-path rules. INFERENCE:
  stability is high but only guaranteed per build-config.

## B. OTR path layer (24,300 unique `__OTR__` paths extracted)

| Bucket | Count | Examples |
|---|---|---|
| objects | 9,752 | `__OTR__objects/gameplay_keep/gBottleGlassTex` |
| scenes | 8,034 | `__OTR__scenes/nonmq/bdan_scene/bdan_room_0Tex_002DB8` |
| texture groups (UI/sky) | 6,226 | `__OTR__textures/parameter_static/gHeartFullTex` |
| overlays (effects) | 288 | `__OTR__overlays/ovl_Arrow_Fire/gFireArrowEffectTex` |
| misc | (in misc bucket) | link animetion |
| custom PNG | 72 | `custom/textures/buttons/ABtn.png` |

Recorded types: TEXTURE 12,872, DISPLAY_LIST 8,304, PALETTE 421, GEOMETRY 74,
ASSET_REF 2,629, PNG_FILE 72.

## C. XML ground-truth layer (`xml/GC_NMQ_NTSC_U`)

- 12,595 `<Texture>` entries parsed; 12,549 matched symbol-exactly to header records (99.6%).
- Provides: pixel format (rgba16/ia8/i4/ci...), width, height, rom offset.
- GC_NMQ_NTSC_U is the default (non-Master-Quest, GameCube) configuration. Other
  config dirs exist (GC_MQ_D, N64_*, PAL/J variants) and may define additional MQ assets
  not covered by this dataset. SOURCE FACT + gap.

## D. Resource families and how they are identified

1. **Scene assets** (`scenes/{overworld,dungeons,indoors,misc,shops,test_levels}/<scene>/`)
   - `*_scene.h` + `*_room_N.h` headers; textures are offset-named
     (`spot00_sceneTex_013D98`, `tokinoma_room_0Tex_...`).
   - Identifiers are NOT semantic. MaterialRegistry must treat scene textures as
     scene-scoped (rule type: scene+resource), never semantic-name rules.
2. **Object assets** (`objects/object_*/`, `gameplay_keep`, `gameplay_field_keep`, ...)
   - 381 object dirs; semantic symbols (`gDekuStickTex`, `gHylianShieldDesignTex`).
   - Best candidates for exact-symbol material rules.
3. **UI texture groups** (`textures/parameter_static`, `icon_item_*`, `message_*`, ...)
   - 22 dedicated groups, ~6k textures. UI-PROTECTED zone.
4. **Skyboxes / backgrounds** (`textures/skyboxes/vr_*`, `textures/backgrounds/vr_*`)
   - Semantic (`gSunriseOvercastSkybox1Tex`). SKY category.
5. **Overlays** (`overlays/ovl_*`) - actor effect resources (fire/ice/light arrows, boss FX).
6. **Custom** (`custom/**`) - Couch Edition additions; 72 PNG replacement textures on disk
   (buttons = UI; object replacements keep object semantics).
7. **Misc** (`misc/link_animetion`) - Link animation library (no material surfaces).
8. **soh_assets.h** - manually maintained aligned-symbol list; a natural hook point for
   future MaterialRegistry lookup (NOTED ONLY - no runtime work performed).

## E. Identifier stability assessment (for MaterialRegistry rules)

- Object/UI symbols: HIGH stability (semantic names, XML-regenerated).
- Scene offset symbols: MEDIUM-LOW stability (rebuild can shift offsets).
- `__OTR__` path strings: the safest rule key (they ARE the runtime resource names).
- Custom PNG files: stable file paths but replacement-layer, not original assets.
