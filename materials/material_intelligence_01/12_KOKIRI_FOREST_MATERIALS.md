# 12 - Kokiri Forest Material Inventory (PILOT)

Scene: `spot04` (overworld). SOURCE FACT: 507 scene records
(59 textures, offset-named).

## Known materials (evidence-backed, from shared object layer)
- `spot04_room_0Tex_00BF08` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)
- `spot04_room_0Tex_00C708` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)
- `spot04_room_0Tex_00CB08` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)
- `spot04_room_0Tex_00CF08` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)
- `spot04_room_0Tex_00D308` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)
- `spot04_room_0Tex_00D408` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)
- `spot04_room_0Tex_00D508` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)
- `spot04_room_0Tex_00D908` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)
- `spot04_room_0Tex_00E108` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)
- `spot04_room_0Tex_00E908` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)
- `spot04_room_0Tex_00F108` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)
- `spot04_room_0Tex_00F508` (TEXTURE, LOW) -> FOLIAGE: scene texture (offset-named) in spot04; Kokiri Forest predominantly organic (scene-level prior)

Semantic anchors found: `gHanaLeaf1-5DL` + `gHanaLeafTex` (gameplay_field_keep),
`gCuttableShrubLeafTFragmentTex`, `gGrassBladesDL`, `gUnusedGrassBladesTex`,
`gWitheredLeafTex` (gameplay_keep), `object_kusa` DLs (grass tufts).
SOURCE FACTS from symbol names.

## Special surfaces
- Grass tufts (`object_kusa`) are alpha-tested billboards/cross-quads in engine ->
  alpha_test=likely. INFERENCE from IA formats and engine convention.
- Water: stream at forest exit is drawn via scene resources; offset-named, not
  semantically identified -> manual review. No semantic water symbols found in scene.

## Emissive candidates
- Navi/fairy effects are actor-level (`gameplay_keep` gFairyWingTex etc.) and are
  already in 08_EMISSIVE_CANDIDATES.csv.
- `gCircleGlowLTex` (TEXTURE, HIGH) -> SKIN: skin/body-part keyword
- `gCircleGlowRTex` (TEXTURE, HIGH) -> SKIN: skin/body-part keyword
- `gCircleGlowSLTex` (TEXTURE, HIGH) -> SKIN: skin/body-part keyword
- `gCircleGlowSRTex` (TEXTURE, HIGH) -> SKIN: skin/body-part keyword
- `gDecorativeFlameMaskTex` (TEXTURE, HIGH) -> LAVA: lava/fire keyword
- `gDecorativeFlameTex` (TEXTURE, HIGH) -> LAVA: lava/fire keyword

## UI exclusions
None in scene scope; global UI groups unaffected. SOURCE FACT.

## Unknown resources
99 offset-named textures
-> UNKNOWN/classic + manual review.

## Manual review candidates
- Scene offset textures (bark vs wood vs dirt ground layering).
- House wall/foliage split for `kokiri_home`-style objects.
