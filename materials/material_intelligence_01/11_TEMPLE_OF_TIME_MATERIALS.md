# 11 - Temple of Time Material Inventory (PILOT)

Scene: `tokinoma` (indoors). SOURCE FACT: 73 scene records
(33 textures, mostly offset-named)
+ object group `object_toki_objects` with 21 records (offset-named).

## Known materials (evidence-backed)
- Stone architecture is the scene prior. INFERENCE (scene identity is certain; the
  individual offset textures are not inspected visually).
- Semantic anchors found in the shared object layer:
- `gLinkAdultLeftHandHoldingMasterSwordFarDL` (DISPLAY_LIST, LOW) -> UNKNOWN: display list: material only via referenced textures
- `gLinkAdultLeftHandHoldingMasterSwordNearDL` (DISPLAY_LIST, LOW) -> UNKNOWN: display list: material only via referenced textures
- `gLinkAdultMasterSwordAndSheathFarDL` (DISPLAY_LIST, LOW) -> UNKNOWN: display list: material only via referenced textures
- `gLinkAdultMasterSwordAndSheathNearDL` (DISPLAY_LIST, LOW) -> UNKNOWN: display list: material only via referenced textures
- `gLinkChildLeftHandHoldingMasterSwordDL` (DISPLAY_LIST, LOW) -> UNKNOWN: display list: material only via referenced textures
- `gLinkChildMasterSwordEmblemTex` (TEXTURE, HIGH) -> METAL: metal keyword; Temple of Time key-object keyword
- `gLinkChildMasterSwordGuardTex` (TEXTURE, HIGH) -> METAL: metal keyword; Temple of Time key-object keyword
- `gLinkChildMasterSwordPommelTex` (TEXTURE, HIGH) -> METAL: metal keyword; Temple of Time key-object keyword
- `gGanonMasterSwordDL` (DISPLAY_LIST, LOW) -> UNKNOWN: display list: material only via referenced textures
- `gGanonMasterSwordGuardTex` (TEXTURE, LOW) -> PARTICLE: effect overlay texture (assumed effect unless named otherwise); Temple of Time key-object keyword

## Special surfaces
- Windows/light shafts: the window actor `ovl_Bg_Toki_Hikari` ("Temple of Time Windows",
  per its own decomp header comment) declares its resources outside the scene headers
  (0 records matched in this dataset). Light shafts are additive/alpha FX in engine ->
  EMISSIVE-class candidate requiring draw-capture validation. INFERENCE.
- Door of Time / pedestal: resources live in `object_toki_objects` but carry offset
  names only -> manual review queue until symbol evidence exists.

## Emissive candidates (Temple-of-Time-related symbols)
- (none found)

## Water
None expected; none discovered. SOURCE FACT: 0 water records in tokinoma/object_toki.

## UI exclusions
No UI_2D records inside the scene/object groups. UI is fully in `textures/*` groups
(protected globally). SOURCE FACT.

## Unknown resources
All 9 offset-named
scene/object textures default to UNKNOWN -> classic fallback. These are the primary
manual-review set (capture draws in-engine to map Tex offsets to surfaces).

## Manual review candidates
- Every offset-named texture in tokinoma + object_toki_objects (needs in-engine capture).
- `gLinkChildMasterSword*Tex` (pommel/guard/emblem) - metal + emissive emblem questions.
