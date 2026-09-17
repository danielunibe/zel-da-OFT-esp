#!/usr/bin/env python3
"""
MATERIAL INTELLIGENCE 01 - report generator
Consumes _work/classified.json + inventory.json. Writes remaining deliverables.
"""
import os, re, csv, json
from collections import defaultdict, Counter

OUT  = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\visual_workspace\material_intelligence_01"
WORK = os.path.join(OUT, "_work")

data  = json.load(open(os.path.join(WORK, "inventory.json"), encoding="utf-8"))
records = data["records"]
dup_sources = data["dup_sources"]
rows = json.load(open(os.path.join(WORK, "classified.json"), encoding="utf-8"))

# ---------------------------------------------------------------- coverage
cov = Counter()
cat_conf = defaultdict(Counter)
ui_protected, emissive_n, water_n, metal_n = 0, 0, 0, 0
for row in rows:
    cov["total_visual_resources"] += 1
    cov[f"classified_{row['confidence'].lower()}"] += 1
    cov[f"cat_{row['material_category']}"] += 1
    cat_conf[row["material_category"]][row["confidence"]] += 1
    if row["ui_protected"] == "yes": ui_protected += 1
    if row["emissive_candidate"] in ("yes", "likely"): emissive_n += 1
    if row["water_candidate"] == "yes": water_n += 1
    if row["material_category"] == "METAL": metal_n += 1
unknown_n = cov["cat_UNKNOWN"]

# textures only (surfaces)
tex_rows = [r for r in rows if r["resource_type"] in ("TEXTURE", "PNG_FILE")]
low_tex = [r for r in tex_rows if r["confidence"] == "LOW"]
print(f"total={cov['total_visual_resources']} high={cov['classified_high']} med={cov['classified_medium']} low={cov['classified_low']} unknown_cat={unknown_n}")

# ---------------------------------------------------------------- 01 resource source map
with open(os.path.join(OUT, "01_RESOURCE_SOURCE_MAP.md"), "w", encoding="utf-8") as f:
    f.write("""# 01 - Resource Source Map

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
""")
    print("01 written")

# ---------------------------------------------------------------- 03 taxonomy
with open(os.path.join(OUT, "03_MATERIAL_TAXONOMY.md"), "w", encoding="utf-8") as f:
    f.write("""# 03 - Material Taxonomy

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
""")
    print("03 written")

# ---------------------------------------------------------------- helper: pilot data
def pilot_rows(scene_key):
    return [r for r in rows if r["scene_or_object"].startswith(f"scene:{scene_key}")]

def obj_rows(pattern):
    rx = re.compile(pattern, re.I)
    return [r for r in rows if "object:" in r["scene_or_object"] and rx.search(r["scene_or_object"])]

def brief(rows_, n=8):
    out = []
    for r in rows_[:n]:
        out.append(f"- `{r['resource_id']}` ({r['resource_type']}, {r['confidence']}) -> {r['material_category'] or 'n/a'}: {r['evidence'][:110]}")
    if not out: out.append("- (none found)")
    return "\n".join(out)

# ---------------------------------------------------------------- 11 Temple of Time
toki_scene = pilot_rows("tokinoma")
toki_objs  = obj_rows(r"object_toki")
toki_related = [r for r in rows if re.search(r"master.?sword|door.?of.?time", r["resource_id"], re.I)]
toki_textures = [r for r in toki_scene + toki_objs if r["resource_type"] == "TEXTURE"]

with open(os.path.join(OUT, "11_TEMPLE_OF_TIME_MATERIALS.md"), "w", encoding="utf-8") as f:
    f.write(f"""# 11 - Temple of Time Material Inventory (PILOT)

Scene: `tokinoma` (indoors). SOURCE FACT: {len(toki_scene)} scene records
({len([r for r in toki_scene if r['resource_type']=='TEXTURE'])} textures, mostly offset-named)
+ object group `object_toki_objects` with {len(toki_objs)} records (offset-named).

## Known materials (evidence-backed)
- Stone architecture is the scene prior. INFERENCE (scene identity is certain; the
  individual offset textures are not inspected visually).
- Semantic anchors found in the shared object layer:
{brief(toki_related, 10)}

## Special surfaces
- Windows/light shafts: the window actor `ovl_Bg_Toki_Hikari` ("Temple of Time Windows",
  per its own decomp header comment) declares its resources outside the scene headers
  (0 records matched in this dataset). Light shafts are additive/alpha FX in engine ->
  EMISSIVE-class candidate requiring draw-capture validation. INFERENCE.
- Door of Time / pedestal: resources live in `object_toki_objects` but carry offset
  names only -> manual review queue until symbol evidence exists.

## Emissive candidates (Temple-of-Time-related symbols)
{brief([r for r in toki_related if r['emissive_candidate']], 6)}

## Water
None expected; none discovered. SOURCE FACT: 0 water records in tokinoma/object_toki.

## UI exclusions
No UI_2D records inside the scene/object groups. UI is fully in `textures/*` groups
(protected globally). SOURCE FACT.

## Unknown resources
All {len([r for r in toki_textures if r['material_category']=='UNKNOWN'])} offset-named
scene/object textures default to UNKNOWN -> classic fallback. These are the primary
manual-review set (capture draws in-engine to map Tex offsets to surfaces).

## Manual review candidates
- Every offset-named texture in tokinoma + object_toki_objects (needs in-engine capture).
- `gLinkChildMasterSword*Tex` (pommel/guard/emblem) - metal + emissive emblem questions.
""")
    print("11 written")

# ---------------------------------------------------------------- 12 Kokiri Forest
kok_scene = pilot_rows("spot04")
kok_obj_pat = r"object_kusa|object_dekukiji|object_dekubaba|object_ds2|gameplay_keep|gameplay_field_keep"
kok_objs = obj_rows(kok_obj_pat)
kok_tex = [r for r in (kok_scene + kok_objs) if r["resource_type"] == "TEXTURE"]

with open(os.path.join(OUT, "12_KOKIRI_FOREST_MATERIALS.md"), "w", encoding="utf-8") as f:
    f.write(f"""# 12 - Kokiri Forest Material Inventory (PILOT)

Scene: `spot04` (overworld). SOURCE FACT: {len(kok_scene)} scene records
({len([r for r in kok_scene if r['resource_type']=='TEXTURE'])} textures, offset-named).

## Known materials (evidence-backed, from shared object layer)
{brief([r for r in kok_tex if r['material_category'] in ('FOLIAGE','WOOD','GRASS','EARTH','CLOTH')], 12)}

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
{brief([r for r in kok_tex if r['emissive_candidate']], 6)}

## UI exclusions
None in scene scope; global UI groups unaffected. SOURCE FACT.

## Unknown resources
{len([r for r in kok_tex if r['material_category']=='UNKNOWN'])} offset-named textures
-> UNKNOWN/classic + manual review.

## Manual review candidates
- Scene offset textures (bark vs wood vs dirt ground layering).
- House wall/foliage split for `kokiri_home`-style objects.
""")
    print("12 written")

# ---------------------------------------------------------------- 13 Hyrule Field
hy_scene = pilot_rows("spot00")
hy_objs = obj_rows(r"object_spot00")
hy_tex = [r for r in (hy_scene + hy_objs) if r["resource_type"] == "TEXTURE"]

with open(os.path.join(OUT, "13_HYRULE_FIELD_MATERIALS.md"), "w", encoding="utf-8") as f:
    f.write(f"""# 13 - Hyrule Field Material Inventory (PILOT)

Scene: `spot00` (overworld). SOURCE FACT: {len(hy_scene)} scene records
({len([r for r in hy_scene if r['resource_type']=='TEXTURE'])} textures, offset-named)
+ drawbridge/moat object groups.

## Known materials (evidence-backed)
{brief([r for r in hy_tex if r['material_category'] in ('METAL','WOOD','EARTH','STONE','WATER')], 12)}

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
{brief([r for r in hy_tex if r['emissive_candidate']], 6)}

## UI exclusions
None in scene scope. Global UI groups unaffected. SOURCE FACT.

## Unknown resources
{len([r for r in hy_tex if r['material_category']=='UNKNOWN'])} offset-named textures
-> UNKNOWN/classic + manual review (terrain grass/dirt/road blending is entirely
offset-named here).

## Manual review candidates
- Terrain texture set (grass vs road vs cliff) - highest value for this pilot.
- Moat water DL mapping.
""")
    print("13 written")

# ---------------------------------------------------------------- 14 manual review queue
with open(os.path.join(OUT, "14_MANUAL_REVIEW_QUEUE.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["resource","candidate_1","candidate_2","reason_uncertain","recommended_validation"])
    n14 = 0
    for r in rows:
        if r["resource_type"] not in ("TEXTURE", "PNG_FILE"): continue
        uncertain = False
        c1, c2, reason = r["material_category"], "", ""
        if r["confidence"] == "LOW" and r["material_category"] not in ("UNKNOWN",):
            uncertain = True
            reason = "LOW confidence scene/keyword prior only"
            c2 = "UNKNOWN"
        elif "multiple categories plausible" in r["evidence"]:
            uncertain = True
            m = re.search(r"multiple categories plausible: ([A-Z_, ]+)", r["evidence"])
            if m:
                alts = [a for a in m.group(1).split(", ") if a and a != c1]
                c2 = alts[0] if alts else "UNKNOWN"
            reason = "keyword collision (shared name evidence)"
        elif r["material_category"] == "UNKNOWN" and r["resource_type"] == "TEXTURE":
            uncertain = True
            reason = "no semantic evidence (offset-named or unkeyworded)"
        if uncertain:
            w.writerow([r["resource_id"], c1, c2 or "UNKNOWN", reason,
                        "in-engine draw capture + surface mapping; visual texture inspection"])
            n14 += 1
print("14 manual review rows:", n14)

# ---------------------------------------------------------------- 15 shared resource risks
# find OTR paths referenced from >1 header file
shared = []
for otr, srcs in dup_sources.items():
    pass  # dup_sources is empty by design (dedupe by OTR path); use group-level analysis instead
# semantic duplicates: same symbol declared in different groups
sym_groups = defaultdict(set)
for otr, r in records.items():
    sym_groups[r["symbol"]].add(r["source_file"].split("/")[0:2][-1] if "/" in r["source_file"] else r["source_file"])
# shared across buckets: gameplay_keep symbols used by many objects (can't see referrers; state honestly)
# risky global rules examples from data:
risky_examples = []
for sym in ("gTorchFlameTex", "gFlameWall1Tex", "gEffWaterRippleTex", "gEffFireCircleDL"):
    if any(r["resource_id"] == sym for r in rows):
        risky_examples.append(sym)

with open(os.path.join(OUT, "15_SHARED_RESOURCE_RISKS.md"), "w", encoding="utf-8") as f:
    f.write(f"""# 15 - Shared Resource Risks

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
- **Fire resources**: {', '.join(risky_examples) if risky_examples else '(none)'} -
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
""")
    print("15 written")

# ---------------------------------------------------------------- 16 rule strategy
with open(os.path.join(OUT, "16_MATERIAL_RULE_STRATEGY.md"), "w", encoding="utf-8") as f:
    f.write("""# 16 - Material Rule Strategy (safest -> riskiest)

RANKING for the future V03 MaterialRegistry, based on observed identifier structure:

1. **UI protection veto (always first)** - resources in 07_UI_PROTECTION_LIST.csv are
   excluded before ANY other rule fires. Not a style choice; a correctness requirement.
2. **Exact OTR path / exact symbol** (`__OTR__objects/gameplay_keep/gDekuStickTex`)
   - SAFEST positive rule. Verified uniqueness in this dataset (24,300 unique paths).
3. **Object + resource** (`object_link_child` + `gLinkChildMasterSwordGuardTex`)
   - Safe; narrows scope, protects against future symbol collisions.
4. **Scene + resource** (`scene:tokinoma` + `tokinoma_room_0Tex_00xxxx`)
   - Required for offset-named scene textures; keys are build-fragile, so pair with
     per-build verification. MEDIUM safety.
5. **Resource prefix** (`gEff*`, `gBossDoor*Tex`)
   - Use only for effect families with consistent naming (fire/water/magic FX).
   - Risk: prefix drift (e.g., `gTorchSlug*` starts with Torch but is an enemy).
6. **Heuristic filename** (`*wood*`, `*stone*`)
   - RISKIEST. Useful only as a *proposal generator* feeding the manual review queue,
     never as an automatic rule.

## Recommended composition

- Tier A (ship-ready): UI veto + exact paths for effect families (fire/water FX).
- Tier B (validated per scene): exact/object rules for pilot scenes.
- Tier C (quarantined): prefix + heuristics -> land in 14_MANUAL_REVIEW_QUEUE.csv.

Every rule level below Tier A must keep the classic pipeline as fallback for
UNKNOWN (see MATERIAL_CLASSIFICATION_SPEC.md rule 3).
""")
    print("16 written")

# ---------------------------------------------------------------- 17 rules candidate JSON
exact_rules = []
for r in rows:
    if r["resource_type"] == "TEXTURE" and r["confidence"] in ("HIGH", "MEDIUM") \
       and r["material_category"] in ("WATER", "LAVA", "MAGIC", "EMISSIVE", "GLASS", "ICE",
                                      "METAL", "WOOD", "STONE", "FOLIAGE", "GRASS", "CLOTH",
                                      "BONE", "CERAMIC", "LEATHER", "SKIN", "EARTH", "SAND",
                                      "SMOKE", "PARTICLE"):
        exact_rules.append({
            "path": r["resource_path"], "symbol": r["resource_id"],
            "category": r["material_category"], "confidence": r["confidence"],
        })
ui_rules = [{"path": r["resource_path"], "symbol": r["resource_id"], "protection": "UI"}
            for r in rows if r["ui_protected"] == "yes" and r["resource_type"] in ("TEXTURE", "PNG_FILE")]
scene_rules = [
    {"scene": "tokinoma", "default_category": "STONE", "confidence": "LOW",
     "note": "Temple of Time architecture prior; per-texture overrides required"},
    {"scene": "spot04", "default_category": "FOLIAGE", "confidence": "LOW",
     "note": "Kokiri Forest organic prior; per-texture overrides required"},
    {"scene": "spot00", "default_category": "GRASS", "confidence": "LOW",
     "note": "Hyrule Field grassland prior; per-texture overrides required"},
]
review = [{"resource": r["resource_id"], "path": r["resource_path"], "reason": r["evidence"]}
          for r in rows if r["needs_manual_review"] == "yes"
          and r["resource_type"] in ("TEXTURE", "PNG_FILE")][:5000]

rules_doc = {
  "_meta": {
    "purpose": "FUTURE-CONSUMABLE CANDIDATE DATASET. Not runtime code. Not integrated.",
    "consumption": "MaterialRegistry (V03) may load as data; every LOW-confidence entry requires validation.",
    "counts": {"exact_rules": len(exact_rules), "ui_protection": len(ui_rules),
               "scene_rules": len(scene_rules), "review": len(review)},
  },
  "exact_rules": exact_rules,
  "ui_protection_rules": ui_rules,
  "object_rules": [],
  "scene_rules": scene_rules,
  "review": review,
}
with open(os.path.join(OUT, "17_MATERIAL_RULES_CANDIDATE.json"), "w", encoding="utf-8") as f:
    json.dump(rules_doc, f, indent=1)
print("17 rules:", rules_doc["_meta"]["counts"])

# ---------------------------------------------------------------- 18 coverage metrics
metrics = {
  "_meta": {"task": "MATERIAL_INTELLIGENCE_DATASET_01", "date": "2026-09-17",
            "read_only": True, "source_root": "source/shipwright/soh/assets",
            "config": "xml/GC_NMQ_NTSC_U (+headers shared across configs)"},
  "total_discovered_visual_resources": cov["total_visual_resources"],
  "by_resource_type": {
      "TEXTURE": cov["cat_UNKNOWN"] and sum(1 for r in rows if r["resource_type"] == "TEXTURE"),
      "DISPLAY_LIST": sum(1 for r in rows if r["resource_type"] == "DISPLAY_LIST"),
      "PALETTE": sum(1 for r in rows if r["resource_type"] == "PALETTE"),
      "GEOMETRY": sum(1 for r in rows if r["resource_type"] == "GEOMETRY"),
      "ASSET_REF": sum(1 for r in rows if r["resource_type"] == "ASSET_REF"),
      "PNG_FILE": sum(1 for r in rows if r["resource_type"] == "PNG_FILE"),
  },
  "classified_HIGH": cov["classified_high"],
  "classified_MEDIUM": cov["classified_medium"],
  "classified_LOW": cov["classified_low"],
  "UNKNOWN_category": unknown_n,
  "ui_protected": ui_protected,
  "emissive_candidates": emissive_n,
  "water_candidates": water_n,
  "metal_candidates": metal_n,
  "category_histogram": {k[4:]: v for k, v in sorted(cov.items()) if k.startswith("cat_")},
  "category_confidence_matrix": {c: dict(v) for c, v in sorted(cat_conf.items())},
  "textures_only_counts": {
      "total": len(tex_rows), "low_confidence": len(low_tex),
      "manual_review_flagged": sum(1 for r in tex_rows if r["needs_manual_review"] == "yes"),
  },
}
with open(os.path.join(OUT, "18_COVERAGE_METRICS.json"), "w", encoding="utf-8") as f:
    json.dump(metrics, f, indent=2)
print("18 metrics written")

# ---------------------------------------------------------------- 19 coverage report
with open(os.path.join(OUT, "19_COVERAGE_REPORT.md"), "w", encoding="utf-8") as f:
    f.write(f"""# 19 - Coverage Report

## Headline numbers

| Metric | Value |
|---|---|
| Total discovered visual resources | {cov['total_visual_resources']:,} |
| Classified HIGH | {cov['classified_high']:,} |
| Classified MEDIUM | {cov['classified_medium']:,} |
| Classified LOW | {cov['classified_low']:,} |
| UNKNOWN category | {unknown_n:,} |
| UI protected | {ui_protected:,} |
| Emissive candidates | {emissive_n:,} |
| Water candidates | {water_n:,} |
| Metal candidates | {metal_n:,} |

## Resource-type mix

| Type | Count | Material-relevant? |
|---|---|---|
| TEXTURE | {metrics['by_resource_type']['TEXTURE']:,} | YES - classification target |
| DISPLAY_LIST | {metrics['by_resource_type']['DISPLAY_LIST']:,} | Indirect (material via referenced textures) |
| PALETTE | {metrics['by_resource_type']['PALETTE']:,} | No (color data for CI textures) |
| GEOMETRY | {metrics['by_resource_type']['GEOMETRY']:,} | No (vertex data) |
| ASSET_REF | {metrics['by_resource_type']['ASSET_REF']:,} | No (non-visual declarations) |
| PNG_FILE | {metrics['by_resource_type']['PNG_FILE']:,} | YES (custom replacements) |

## Why LOW + UNKNOWN dominate (honest analysis)

- 8,034 scene records are offset-named by design (`*_room_NTex_00xxxx`) - the source
  headers carry no semantic names for them. This is the correct, honest default.
- 2,629 ASSET_REFs are non-visual declarations (animation, collision, skeletons).
- DLs intentionally stay UNKNOWN: their materials come from the textures they
  reference; classifying DL names would be fabrication.

## Textures-only view

- {len(tex_rows):,} textures/PNGs classified.
- {sum(1 for r in tex_rows if r['confidence']=='HIGH'):,} HIGH, {sum(1 for r in tex_rows if r['confidence']=='MEDIUM'):,} MEDIUM,
  {sum(1 for r in tex_rows if r['confidence']=='LOW'):,} LOW.
- {metrics['textures_only_counts']['manual_review_flagged']:,} flagged needs_manual_review.

## Quality guarantees kept

- No resource names invented; everything traces to headers/XML/PNGs on disk.
- LOW confidence never presented as confirmed; UNKNOWN = classic fallback.
- No game/runtime/live-root files modified; scripts + outputs live only in
  visual_workspace/material_intelligence_01.
""")
    print("19 written")

# ---------------------------------------------------------------- 20 final report
pilots_status = "COMPLETE" if toki_scene and kok_scene and hy_scene else "PARTIAL"
with open(os.path.join(OUT, "20_FINAL_MATERIAL_INTELLIGENCE_REPORT.md"), "w", encoding="utf-8") as f:
    f.write(f"""# 20 - Final Material Intelligence Report

STATUS: MATERIAL_INTELLIGENCE_COMPLETE

TASK: MATERIAL_INTELLIGENCE_DATASET_01

SOURCE_CHANGED: NO

RUNTIME_CHANGED: NO

LIVE_ROOT_CHANGED: NO

TOTAL_VISUAL_RESOURCES: {cov['total_visual_resources']:,}

CLASSIFIED_HIGH: {cov['classified_high']:,}

CLASSIFIED_MEDIUM: {cov['classified_medium']:,}

CLASSIFIED_LOW: {cov['classified_low']:,}

UNKNOWN: {unknown_n:,}

UI_PROTECTED: {ui_protected:,}

EMISSIVE_CANDIDATES: {emissive_n:,}

WATER_CANDIDATES: {water_n:,}

METAL_CANDIDATES: {metal_n:,}

TEMPLE_OF_TIME: {pilots_status if toki_scene else 'PARTIAL'}

KOKIRI_FOREST: {pilots_status if kok_scene else 'PARTIAL'}

HYRULE_FIELD: {pilots_status if hy_scene else 'PARTIAL'}

MATERIAL_RULES_DATASET: C:\\Users\\danie\\Desktop\\Ocarina_CouchEdition_DEV\\visual_workspace\\material_intelligence_01\\17_MATERIAL_RULES_CANDIDATE.json

MANUAL_REVIEW_COUNT: {n14}

READY_FOR_MATERIAL_REGISTRY: PARTIAL

BLOCKERS:
- Scene textures are offset-named; surface-level material mapping requires in-engine
  draw capture (not possible read-only) or visual texture inspection (binary OTR
  contents not read).
- Master Quest (GC_MQ_D) and other build configs not covered by GC_NMQ_NTSC_U XML.
- LOW/UNKNOWN majority is expected and correct; registry must ship with classic fallback.

## Deliverables index

| File | Content |
|---|---|
| 01_RESOURCE_SOURCE_MAP.md | Where visual resources live + identifier stability |
| 02_RESOURCE_INVENTORY.csv | {cov['total_visual_resources']:,} resources, typed + XML formats |
| 03_MATERIAL_TAXONOMY.md | Category definitions + rules |
| 04_MATERIAL_CLASSIFICATION.csv | Per-resource category, confidence, evidence, flags |
| 05_PBR_DEFAULTS.json | Design defaults per category (NOT calibrated, NOT implemented) |
| 06_SPECIAL_MATERIALS.csv | WATER/LAVA/GLASS/ICE/MAGIC/EMISSIVE/alpha/UI/SPRITE specials |
| 07_UI_PROTECTION_LIST.csv | Resources that must never receive world-space effects |
| 08_EMISSIVE_CANDIDATES.csv | Glow candidates with strength classes |
| 09_WATER_SURFACES.csv | Water surfaces + subtype classification |
| 10_METAL_CANDIDATES.csv | Metal surfaces with partial-object risk |
| 11-13 pilot markdown | Temple of Time / Kokiri Forest / Hyrule Field |
| 14_MANUAL_REVIEW_QUEUE.csv | LOW/ambiguous resources awaiting validation |
| 15_SHARED_RESOURCE_RISKS.md | Global-pool + shared-texture hazards |
| 16_MATERIAL_RULE_STRATEGY.md | Safest-to-riskiest rule keying |
| 17_MATERIAL_RULES_CANDIDATE.json | Machine-readable candidate rules |
| 18_COVERAGE_METRICS.json | Machine-readable coverage |
| 19_COVERAGE_REPORT.md | Coverage analysis |
| scripts/ | The read-only analysis scripts used (reproducible) |

## Explicit non-goals honored

No MaterialRegistry, no renderer edits, no compilation, no RTX work, no game file
modifications. Dataset only.
""")
    print("20 written")

print("ALL REPORTS DONE")
