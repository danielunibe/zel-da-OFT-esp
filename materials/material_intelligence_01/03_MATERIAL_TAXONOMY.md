# 03 - Material Taxonomy

Primary categories used by dataset 04. Assignment = keyword evidence from semantic
symbols, path context, XML formats; scene-level priors only where scene identity is
certain (Temple of Time, Kokiri Forest, Hyrule Field, Ice Cavern, Water Temple).
Confidence levels HIGH / MEDIUM / LOW are recorded per resource in 04.

| Category | Meaning | Typical evidence |
|---|---|---|
| STONE | architecture, rock, walls, tombs | stone/rock/pillar/tomb keywords |
| WOOD | wood, trees, planks, chests, signs | wood/tree/plank/chest keywords |
| METAL | metal weapons, armor, chains, machines | metal/sword/chain/gear keywords |
| EARTH | dirt, soil, cliffs | dirt/soil/cliff keywords |
| SAND | deserts, beaches | sand/desert keywords |
| GRASS | grasslands, lawns | grass/meadow keywords |
| FOLIAGE | leaves, bushes, plants | leaf/bush/flower keywords |
| CLOTH | fabric, banners, clothing fabric | cloth/banner/tunic keywords |
| SKIN | character/creature skin & body parts | skin/face/hand keywords |
| LEATHER | straps, belts, boots | leather/belt/strap keywords |
| BONE | bones, skulls, shells | bone/skull/shell keywords |
| CERAMIC | pots, jars, dishes | pot/jar/vase keywords |
| GLASS | bottles, windows, crystals | glass/window/crystal keywords |
| ICE | ice, frost, snow | ice/frost/snow keywords |
| WATER | water surfaces (special path) | water/river/fountain keywords |
| LAVA | lava, magma, flames (special path) | lava/magma/fire keywords |
| MAGIC | magical effects, fairies, portals | magic/fairy/portal keywords |
| EMISSIVE | self-lit surfaces | glow/light/energy keywords |
| SMOKE | smoke, steam, dust | smoke/steam/dust keywords |
| PARTICLE | effect textures, sparks | particle/effect keywords |
| SKY | skyboxes, backgrounds | skybox groups, vr_ files |
| UI_2D | HUD, menus, icons (PROTECTED) | dedicated UI texture groups |
| SPRITE | screen-space sprites, markers | sprite/marker keywords |
| TEXT | fonts, glyphs | font/kanji groups |
| UNKNOWN | no reliable evidence | default for offset-named & unkeyworded |

## Additional category considered

No additional primary category was added. Two design notes instead:
- **VEGETATION_DEFORM** (wind-sway grass) would be a *behavior* flag, not a material;
  represented here as FOLIAGE + future special flag.
- **SHADOW_BLOB** exists in-engine as decal rendering; classified SPRITE/UNKNOWN.

## RULES

1. UNKNOWN never receives PBR defaults (see visual_workspace/materials/MATERIAL_CLASSIFICATION_SPEC.md).
2. WATER/LAVA/MAGIC/GLASS/ICE are SPECIAL paths, not simple PBR constants.
3. UI_2D / SPRITE / TEXT are protected zones (see 07_UI_PROTECTION_LIST.csv).
4. LOW confidence is never presented as confirmed (per-resource in dataset 04).
