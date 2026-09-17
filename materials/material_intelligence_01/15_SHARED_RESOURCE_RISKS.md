# 15 - Shared Resource Risks

## Structural facts

1. **gameplay_keep / gameplay_field_keep are global pools.** Textures like
   `gTorchFlameTex`, `gEffWaterRippleTex`, `gGrassBladesDL` live in `gameplay_keep`
   and are referenced by actors everywhere. A global rule on these symbols is broad
   by design - acceptable for fire/emissive, dangerous for neutral textures. SOURCE FACT (pool structure) + INFERENCE (risk).

2. **No cross-object symbol duplicates were found in this extraction** (each OTR path
   declared exactly once across the 1080 parsed headers). What IS shared is the
   *pool membership* above, not the symbol. SOURCE FACT (extraction-level).

3. **Offset-named scene textures are structurally anonymous.** The same stone texture
   may appear as `tokinoma_room_0Tex_00xxxx` and again in `HAKAdan_room_7Tex_...`.
   Identifier-level rules cannot know they are visually identical. No visual
   inspection was performed; hashing actual texture bytes requires the extracted
   OTR files (runtime side), which are READ-ONLY here and were not read.

## Conceptual risk cases (documented for the future registry)

- **Shared brown/gray textures**: wood vs stone vs dirt cannot be separated by name
  for offset-named resources. Example risk class, not a confirmed case.
- **Fire resources**: gTorchFlameTex, gFlameWall1Tex, gEffWaterRippleTex, gEffFireCircleDL -
  emissive rules here are LOW-RISK because semantics are inherent to the resource.
- **Water ripples vs actual water surfaces**: `gEffWaterRippleTex` is an effect, not a
  pond. Applying "water = reflective" to it would be wrong. INFERENCE from naming.
- **UI replacement PNGs under custom/objects/** keep object semantics; only
  custom/textures/buttons is UI. Global "custom = UI" rules would corrupt
  world-space replacement textures. SOURCE FACT (path layout).

## Registry guidance implied by risks

1. Never key rules on visually-similar offsets across scenes.
2. Prefer object-scoped rules over global pools for neutral materials.
3. Effect resources (fire/water FX) may take global rules safely.
4. Any rule set must include the UI protection list as a hard veto (07).
