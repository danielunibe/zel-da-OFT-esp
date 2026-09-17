# 13 - Hyrule Field Material Inventory (PILOT)

Scene: `spot00` (overworld). SOURCE FACT: 505 scene records
(51 textures, offset-named)
+ drawbridge/moat object groups.

## Known materials (evidence-backed)
- `gBarbedWireFenceTex` (TEXTURE, MEDIUM) -> METAL: metal keyword; wood keyword; multiple categories plausible: METAL, WOOD
- `gBrokenDrawbridgeBoltTex` (TEXTURE, MEDIUM) -> METAL: metal keyword; wood keyword; multiple categories plausible: METAL, WOOD
- `gBrokenDrawbridgeChainTex` (TEXTURE, MEDIUM) -> METAL: metal keyword; wood keyword; multiple categories plausible: METAL, WOOD
- `gBrokenDrawbridgeDirtTex` (TEXTURE, MEDIUM) -> EARTH: earth keyword; wood keyword; multiple categories plausible: EARTH, WOOD

Semantic anchors: `gBrokenDrawbridgeDirtTex` (EARTH), `gBrokenDrawbridgeBoltTex` +
`gBrokenDrawbridgeChainTex` (METAL), `gHyruleFieldCastleDrawbridgeWoodTex` (WOOD),
`gBarbedWireFenceTex` (METAL). SOURCE FACTS.

## Sky / atmosphere
- Skyboxes (`textures/vr_cloud*`, `vr_HRGL` etc.) are SKY; Hyrule Field's day/night
  cycle rotates skybox sets. Day/night relevant resources: `gSunriseOvercastSkybox*Tex`
  families + market_day/market_night style scene variants. SOURCE FACTS (names).
- `gFieldSandstorm*Tex` (gameplay_field_keep) - distance/dust atmosphere. EARTH/SMOKE.

## Special surfaces
- Moat water: drawn by scene DLs (offset-named) -> water candidate, manual review.
- Drawbridge chains/bolts: METAL, partial-object risk documented in 10_METAL_CANDIDATES.csv.

## Emissive candidates
- (none found)

## UI exclusions
None in scene scope. Global UI groups unaffected. SOURCE FACT.

## Unknown resources
0 offset-named textures
-> UNKNOWN/classic + manual review (terrain grass/dirt/road blending is entirely
offset-named here).

## Manual review candidates
- Terrain texture set (grass vs road vs cliff) - highest value for this pilot.
- Moat water DL mapping.
