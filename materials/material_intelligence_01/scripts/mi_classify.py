#!/usr/bin/env python3
"""
MATERIAL INTELLIGENCE 01 - classifier
Consumes _work/inventory.json. Applies keyword-evidence rules from symbol names,
paths, XML formats and known context. Evidence-only; no invented resources.
Outputs: 04, 06, 07, 08, 09, 10 CSVs + 05 JSON + _work/classified.json
"""
import os, re, csv, json
from collections import defaultdict

OUT  = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\visual_workspace\material_intelligence_01"
WORK = os.path.join(OUT, "_work")

data = json.load(open(os.path.join(WORK, "inventory.json"), encoding="utf-8"))
records = data["records"]

# ---------------------------------------------------------------- helpers
def tex_name(r):
    return r["symbol"]

def norm(s):
    return s.lower()

# ---------------------------------------------------------------- keyword rule tables
# Each rule: (compiled regex, category, confidence, evidence phrase)
STONE_RE = re.compile(r"(stone|rock|boulder|marble|granite|bricks?|block(?!buster)|column|pillar|pedestal|altar|temple|tomb|dungeon|cave|crypt|grave(?:stone)?|masonry|cobble|wall|floor|tile|pavement|slab|grotto|monument|obelisk|stela|well)", re.I)
WOOD_RE  = re.compile(r"(wood|tree|trunk|bark|plank|board|log|timber|deku|stick|fence|sign(?:post)?|barrel|crate|box|chest|ladder|beam|shed|cabin|roots?|branch|shop(?!ping)|stall|door(?!way)?|gate|bridge|raft|bow(?!er)|arrow_?shaft|club)", re.I)
METAL_RE = re.compile(r"(metal|iron|steel|silver|golden?|gold(?!en)|bronze|brass|copper|sword|blade|shield|armor|armour|helmet|chain|hook|lock|key(?!board)|nail|axe|hoe|hammer|anvil|shovel|bucket|knight|gauntlet|grate|cage|bars?|fence_?metal|machine|gear|cog|piston|spring|bolt|screw|canon|cannon|bomb(?:ch[uo])?|arrow(?:head)?|boomerang|hookshot|hammer|medal|coin|rupee|vault|treasure(?!chest_game))", re.I)
EARTH_RE = re.compile(r"(dirt|soil|earth|mud|clay|ground|terrain|cliff|canyon|mountain|terrace|landslide|crack|peat)", re.I)
SAND_RE  = re.compile(r"(sand|desert|dune|quicksand|wasteland|beach)", re.I)
GRASS_RE = re.compile(r"(grass|lawn|meadow|field|plains?|turf|steppe)", re.I)
FOLIAGE_RE = re.compile(r"(leaf|leaves|foliage|bush|shrub|fern|ivy|vine|moss|flower|bloom|petal|plant|garden|hedge|canopy|kikiri|kokiri)", re.I)
CLOTH_RE = re.compile(r"(cloth|fabric|banner|flag|curtain|drape|towel|blanket|tunic|cloak|cape|robe|sail|tent|carpet|rug|quilt|linen|sack|bag|pouch|hem|seam|velvet|silk)", re.I)
SKIN_RE  = re.compile(r"(skin|face(?!plate)|head|hand|arm(?!or)|leg|foot|body|torso|neck|ear|nose|mouth|eye(?:s|brow|lid)?|hair|beard|mustache|flesh|cheek|lip|tongue|finger|palm)", re.I)
LEATHER_RE = re.compile(r"(leather|hide|pelt|strap|belt|saddle|boot|shoe|glove|gauntlet_?leather|sheath|holster|whip)", re.I)
BONE_RE  = re.compile(r"(bone|skull|skeleton|rib(?:s)?|femur|jaw|teeth|tooth|tusk|horn|antler|shell(?!fish)|carapace|stal)", re.I)
CERAMIC_RE = re.compile(r"(pot|vase|jar|urn|amphora|porcelain|dishes?|plate|bowl|cup|jug|pitcher|tile_?roof|shard)", re.I)
GLASS_RE = re.compile(r"(glass|crystal|lens|window|bottle(?!_?text)|prism|mirror|pane)", re.I)
ICE_RE   = re.compile(r"(ice|frost|snow|glacier|icicle|frozen|freeze|hail)", re.I)
WATER_RE = re.compile(r"(water|ocean|sea|river|lake|pond|pool|fountain|waterfall|falls|stream|brook|wave|ripple|moat|canal|spring(?!_?board)|hot_spring|aqua|marine|zora_?s?_(domain|river|fountain)|hylia_?lake|warp_?pad)", re.I)
LAVA_RE  = re.compile(r"(lava|magma|fire(?!_?arrow|_?fly|_?proof)|flame(?!_?arrow)|inferno|molten|ember|volcan|crater|fire_?pit|brazier|torch_?flame|flameholder)", re.I)
MAGIC_RE = re.compile(r"(magic|magical|mana|spell|wand|curse|hex|enchant|fairy|fairie|navi|elf_?orb|spirit|soul|wisp|ghost|phantom|aura|glow|warp|portal|warp_?pad|triforce|sage(?:s)?|medallion|spiritual_?stone|zelda_?magic|din|nayru|farore|ocarina(?!_?design)|song_?note|note_?effect)", re.I)
EMISSIVE_RE = re.compile(r"(lava|magma|fire|flame|torch(?!_?wood)|brazier|lantern|lamp|candle|glow|glowing|lum|light(?:ning)?|electric|spark|energy|beam|laser|sun(?:light)?|moon(?:beam)?|star|ray|lens_?flare|fairy|navi|font_?of|blue_?flame|purple_?flame|rupee_?glow|emerald|ruby|sapphire|spiritual|medallion|portal|warp)", re.I)
SMOKE_RE = re.compile(r"(smoke|steam|vapor|mist|fog|cloud_?puff|dust|ash|soot)", re.I)
PARTICLE_RE = re.compile(r"(particle|sparkle|glitter|shard_?effect|debris|confetti|snow_?particle|rain_?drop|drop_?let|bubble|petal_?effect|eff_?|effect)", re.I)
SKY_RE   = re.compile(r"(sky|skybox|cloud|horizon|sunset|sunrise|dusk|dawn|night_?sky|vr_)", re.I)
UI_RE    = re.compile(r"(icon|icon_item|parameter|do_action|message|item_name|map_?48|map_?grand|map_?i|map_?name|title|nintendo_?rogo|font|kanji|nes_?font|gameover|game_?over|bosstitle|boss_?title|place_?title|button|cursor|arrow_?icon|heart|rupee_?icon|key_?icon|counter|digit|gauge|magic_?jar_?icon|compass_?icon|hud|pause|kaleido|file_?choose|file_?select|logo|soh_|ship_?of_?harkinian|dpad|c_?buttons|a_?btn|b_?btn|c_?btn|stick_?icon|calendar|note_?icon|reel|letter_?icon|text_?box|msg)", re.I)
SPRITE_RE = re.compile(r"(sprite|billboard|marker|reticle|target(?:_?reticle)?|crosshair|shadow_?blob|arrow_?marker|z_?target|sparkle_?sprite)", re.I)
TEXT_RE  = re.compile(r"(font|glyph|kanji|letter|text|chara_?digit|numeral|number)", re.I)

# stronger, dedicated rules
NAV_FAIRY_RE = re.compile(r"(navi|fairy|fairie|tatl|tael|elf_?orb)", re.I)
SPIRIT_RE = re.compile(r"(spiritual_?stone|spirit_?stone|emerald|ruby|sapphire|kokiri_?emerald|goron_?ruby|zora_?sapphire)", re.I)
SWORD_RE  = re.compile(r"(sword|blade|knife|dagger|master_?sword|biggoron|giant_?knife)", re.I)
SHIELD_RE = re.compile(r"(shield|hylian_?shield|deku_?shield|mirror_?shield|shield_?design)", re.I)
TORCH_RE  = re.compile(r"(torch|brazier|lamp|lantern|candle)", re.I)
MASTER_RE = re.compile(r"(master_?sword|pedestal_?of_?time|pedestal_?time|door_?of_?time|altar|triforce)", re.I)

# UI texture group names that are always UI
UI_GROUPS = {
    "icon_item_24_static", "icon_item_dungeon_static", "icon_item_field_static",
    "icon_item_fra_static", "icon_item_gameover_static", "icon_item_ger_static",
    "icon_item_jpn_static", "icon_item_nes_static", "icon_item_static",
    "item_name_static", "kanji", "map_48x85_static", "map_grand_static", "map_i_static",
    "map_name_static", "message_static", "message_texture_static", "nes_font_static",
    "nintendo_rogo_static", "parameter_static", "title_static", "do_action_static",
    "boss_title_cards", "place_title_cards",
}

# skyboxes / backgrounds are SKY
SKY_GROUPS = {"skyboxes", "backgrounds"}

# buttons (custom) are UI
UI_PNG_DIRS = ("custom/textures/buttons/",)

# ---------------------------------------------------------------- classification pass
rows = []   # classification rows for every record
for otr, r in sorted(records.items()):
    sym = r["symbol"]
    sym_l = norm(sym)
    src = r["source_file"]
    bucket = r["bucket"]
    grp = r["group"]
    rtype = r["resource_type"]

    category = "UNKNOWN"
    confidence = "LOW"
    evidence = []
    flags = {
        "transparent": "", "alpha_test": "", "emissive_candidate": "",
        "water_candidate": "", "ui_protected": "", "needs_manual_review": "",
    }

    is_texture = rtype in ("TEXTURE", "PNG_FILE")
    is_palette = rtype == "PALETTE"
    is_dl = rtype == "DISPLAY_LIST"
    is_geo = rtype == "GEOMETRY"

    # ---- hard bucket rules first ----
    if bucket == "custom_png" and (src.startswith(UI_PNG_DIRS) or "/buttons/" in src):
        category = "UI_2D"; confidence = "HIGH"
        evidence.append("controller button art (custom/textures/buttons) = screen UI")
        ui_protected = True
    elif bucket == "texture_group" and grp in UI_GROUPS:
        category = "UI_2D"; confidence = "HIGH"
        evidence.append(f"dedicated UI texture group '{grp}'")
        ui_protected = True
    elif bucket == "texture_group" and grp in SKY_GROUPS:
        category = "SKY"; confidence = "HIGH"
        evidence.append(f"skybox/background group '{grp}'")
    elif is_palette:
        category = "UNKNOWN"  # palettes are not surfaces; keep out of PBR
        confidence = "MEDIUM"
        evidence.append("TLUT palette data; not a surface material")
        needs_review = False
    elif is_dl:
        category = "UNKNOWN"; confidence = "LOW"
        evidence.append("display list: material only via referenced textures")
        needs_review = True
    elif is_geo:
        category = "UNKNOWN"; confidence = "LOW"
        evidence.append("vertex buffer: no material identity")
    elif bucket == "scene" and rtype == "TEXTURE":
        # scene textures are offset-named; classify only via scene identity + format
        scene = r["scene"] or ""
        evidence.append(f"scene texture (offset-named) in {scene}")
        category = "UNKNOWN"; confidence = "LOW"
        needs_review = True
        # scene-level hints (strong scene identity only raises to MEDIUM guesses)
        if scene in ("tokinoma",):
            category = "STONE"; confidence = "LOW"
            evidence.append("Temple of Time interior is predominantly stone architecture (scene-level prior)")
        elif scene in ("spot04",):
            category = "FOLIAGE"; confidence = "LOW"
            evidence.append("Kokiri Forest predominantly organic (scene-level prior)")
        elif scene in ("spot00",):
            category = "GRASS"; confidence = "LOW"
            evidence.append("Hyrule Field predominantly grassland (scene-level prior)")
        elif scene in ("ice_doukutu",):
            category = "ICE"; confidence = "LOW"
            evidence.append("Ice Cavern predominantly ice (scene-level prior)")
        elif scene in ("MIZUsin",):
            category = "WATER"; confidence = "LOW"
            evidence.append("Water Temple predominantly water (scene-level prior)")
            flags["water_candidate"] = "yes"
        elif scene in ("HIDAN", "ddan", "ddan_boss", "ganon"):
            category = "STONE"; confidence = "LOW"
            evidence.append("cavern/dungeon scene-level prior")
    elif bucket == "overlay" and is_texture:
        # overlay effect textures: particles/beam/magic
        if PARTICLE_RE.search(sym): category, confidence = "PARTICLE", "MEDIUM"; evidence.append("effect overlay particle name")
        elif EMISSIVE_RE.search(sym): category, confidence = "EMISSIVE", "MEDIUM"; evidence.append("effect overlay glow name")
        elif MAGIC_RE.search(sym): category, confidence = "MAGIC", "MEDIUM"; evidence.append("effect overlay magic name")
        else:
            category, confidence = "PARTICLE", "LOW"
            evidence.append("effect overlay texture (assumed effect unless named otherwise)")
            needs_review = True
    elif bucket == "misc":
        category = "SKIN" if "link" in grp else "UNKNOWN"
        confidence = "LOW"; evidence.append("misc asset")
    else:
        # ---- object / custom objects: name-evidence classification ----
        if is_texture:
            matches = []
            # ordered priority: specific before generic
            if WATER_RE.search(sym): matches.append(("WATER", "water keyword"))
            if LAVA_RE.search(sym): matches.append(("LAVA", "lava/fire keyword"))
            if ICE_RE.search(sym): matches.append(("ICE", "ice keyword"))
            if GLASS_RE.search(sym): matches.append(("GLASS", "glass keyword"))
            if LEATHER_RE.search(sym): matches.append(("LEATHER", "leather keyword"))
            if BONE_RE.search(sym): matches.append(("BONE", "bone keyword"))
            if CERAMIC_RE.search(sym): matches.append(("CERAMIC", "ceramic keyword"))
            if SMOKE_RE.search(sym): matches.append(("SMOKE", "smoke keyword"))
            if SAND_RE.search(sym): matches.append(("SAND", "sand keyword"))
            if FOLIAGE_RE.search(sym): matches.append(("FOLIAGE", "foliage keyword"))
            if GRASS_RE.search(sym): matches.append(("GRASS", "grass keyword"))
            if EARTH_RE.search(sym): matches.append(("EARTH", "earth keyword"))
            if CLOTH_RE.search(sym): matches.append(("CLOTH", "cloth keyword"))
            if SKIN_RE.search(sym): matches.append(("SKIN", "skin/body-part keyword"))
            if METAL_RE.search(sym): matches.append(("METAL", "metal keyword"))
            if WOOD_RE.search(sym): matches.append(("WOOD", "wood keyword"))
            if STONE_RE.search(sym): matches.append(("STONE", "stone keyword"))

            # disambiguation: prefer the FIRST of an ordered list
            if matches:
                # multiple hits -> MEDIUM with all evidence; single strong hit -> HIGH
                cats = [m[0] for m in matches]
                ev = [m[1] for m in matches]
                category = cats[0]
                confidence = "HIGH" if len(set(cats)) == 1 else "MEDIUM"
                evidence.extend(ev)
                if len(set(cats)) > 1:
                    evidence.append(f"multiple categories plausible: {', '.join(sorted(set(cats)))}")
            else:
                category, confidence = "UNKNOWN", "LOW"
                needs_review = True
        else:
            category, confidence = "UNKNOWN", "LOW"
            needs_review = True

    # ---- cross-cutting refinements ----
    fmt = r.get("format", "")
    if is_texture:
        if fmt in ("ia4", "ia8", "ia16"):
            flags["transparent"] = "likely"
            flags["alpha_test"] = "likely"
            evidence.append(f"IA format ({fmt}) carries alpha -> transparency/alpha-test likely")
        if fmt in ("rgba16", "rgba32") and not r.get("xml_hit"):
            pass
        # UI keyword on a texture anywhere -> flag (not classify)
        if UI_RE.search(sym) and category not in ("UI_2D",):
            flags["ui_protected"] = "check"
            evidence.append("UI-style keyword present but resource is world-grouped: manual check")
        # Navi / fairy
        if NAV_FAIRY_RE.search(sym):
            if category in ("UNKNOWN",):
                category, confidence = "MAGIC", "MEDIUM"
            evidence.append("fairy/Navi effect keyword")
            flags["emissive_candidate"] = "yes"
        # spiritual stones
        if SPIRIT_RE.search(sym):
            flags["emissive_candidate"] = "likely"
            if category == "UNKNOWN":
                category, confidence = "MAGIC", "MEDIUM"
            evidence.append("spiritual stone keyword (glowing iconography)")
        if MASTER_RE.search(sym):
            evidence.append("Temple of Time key-object keyword")
            if category == "UNKNOWN":
                category, confidence = "STONE", "MEDIUM"
        if TORCH_RE.search(sym):
            flags["emissive_candidate"] = "likely"
        if EMISSIVE_RE.search(sym) and category not in ("UI_2D",):
            flags["emissive_candidate"] = "likely"
        if WATER_RE.search(sym) and category == "WATER":
            flags["water_candidate"] = "yes"
        if category == "UNKNOWN" and rtype == "TEXTURE":
            flags["needs_manual_review"] = "yes"
        if confidence == "MEDIUM" and len(evidence) >= 3:
            flags["needs_manual_review"] = "yes"

    # UI protection decision
    if category == "UI_2D" or bucket == "custom_png" or (bucket == "texture_group" and grp in UI_GROUPS):
        flags["ui_protected"] = "yes"
    if category == "SPRITE":
        flags["ui_protected"] = "check"

    rows.append({
        "resource_id": r["symbol"],
        "resource_path": otr,
        "resource_type": rtype,
        "scene_or_object": (f"scene:{r['scene']}" + (f"/room{r['room']}" if r['room'] is not None else "")) if r["scene"]
                            else (f"object:{r['object']}" if r["object"] else f"group:{r['group']}"),
        "material_category": category,
        "confidence": confidence,
        "evidence": "; ".join(evidence) if evidence else "no keyword evidence",
        "transparent": flags["transparent"],
        "alpha_test": flags["alpha_test"],
        "emissive_candidate": flags["emissive_candidate"],
        "water_candidate": flags["water_candidate"],
        "ui_protected": flags["ui_protected"],
        "needs_manual_review": "yes" if (flags["needs_manual_review"] or (category == "UNKNOWN" and is_texture)) else "",
    })

# ---------------------------------------------------------------- write 04
with open(os.path.join(OUT, "04_MATERIAL_CLASSIFICATION.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.DictWriter(f, fieldnames=["resource_id","resource_path","material_category","confidence","evidence",
                                      "scene_or_object","transparent","alpha_test","emissive_candidate",
                                      "water_candidate","ui_protected","needs_manual_review"],
                       extrasaction="ignore")
    w.writeheader(); w.writerows(rows)

# ---------------------------------------------------------------- write 05 PBR defaults
pbr_defaults = {
  "_meta": {
    "purpose": "DESIGN DEFAULT PROPOSALS ONLY. Not physically calibrated. Not implemented in runtime.",
    "version": "material_intelligence_01",
    "normalization": "0..1 normalized where practical; WATER/LAVA/MAGIC are special material paths.",
    "warning": "Every value here must be validated per-resource before any runtime use. UNKNOWN categories must never receive these defaults (see MATERIAL_CLASSIFICATION_SPEC.md rule 3)."
  },
  "categories": {
    "STONE":    {"roughness": 0.85, "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.25, "normal_strength": 0.8,
                 "notes": "High roughness; subtle specular. Temple/dungeon architecture."},
    "WOOD":     {"roughness": 0.75, "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.15, "normal_strength": 0.6,
                 "notes": "Medium-high roughness; grain normals modest."},
    "METAL":    {"roughness": 0.35, "metallic": 0.9,  "emissive_strength": 0.0, "specular_strength": 0.8,  "normal_strength": 0.5,
                 "notes": "Variable roughness; 0.35 default suits weathered N64-era metals."},
    "EARTH":    {"roughness": 0.95, "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.05, "normal_strength": 0.7},
    "SAND":     {"roughness": 0.95, "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.05, "normal_strength": 0.4,
                 "notes": "Fine-grain; low normal strength to avoid tiling artifacts."},
    "GRASS":    {"roughness": 0.9,  "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.08, "normal_strength": 0.5},
    "FOLIAGE":  {"roughness": 0.8,  "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.1,  "normal_strength": 0.5,
                 "notes": "Candidate for subsurface/translucency later; do NOT enable in V03."},
    "CLOTH":    {"roughness": 0.9,  "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.08, "normal_strength": 0.4},
    "SKIN":     {"roughness": 0.65, "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.3,  "normal_strength": 0.3,
                 "notes": "Character skin; strongest candidate for 'do not touch' until lighting validated."},
    "LEATHER":  {"roughness": 0.75, "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.15, "normal_strength": 0.4},
    "BONE":     {"roughness": 0.7,  "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.25, "normal_strength": 0.5},
    "CERAMIC":  {"roughness": 0.4,  "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.45, "normal_strength": 0.3},
    "GLASS":    {"roughness": 0.1,  "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.9,  "normal_strength": 0.2,
                 "notes": "Special material path (transmission/blend), not simple PBR-Lite."},
    "ICE":      {"roughness": 0.2,  "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.8,  "normal_strength": 0.3,
                 "notes": "Special material path candidate (shiny + semi-transparent)."},
    "WATER":    {"roughness": 0.05, "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 1.0,  "normal_strength": 0.6,
                 "notes": "SPECIAL MATERIAL PATH. Reflection/RTX candidate; never standard albedo PBR."},
    "LAVA":     {"roughness": 0.8,  "metallic": 0.0,  "emissive_strength": 1.0, "specular_strength": 0.2,  "normal_strength": 0.7,
                 "notes": "SPECIAL MATERIAL PATH. Animated UV scroll + emissive."},
    "MAGIC":    {"roughness": 0.5,  "metallic": 0.0,  "emissive_strength": 0.9, "specular_strength": 0.3,  "normal_strength": 0.0,
                 "notes": "SPECIAL MATERIAL PATH. Emissive-primary; often alpha-blended billboards."},
    "EMISSIVE": {"roughness": 0.6,  "metallic": 0.0,  "emissive_strength": 0.8, "specular_strength": 0.1,  "normal_strength": 0.0},
    "SMOKE":    {"roughness": 1.0,  "metallic": 0.0,  "emissive_strength": 0.1, "specular_strength": 0.0,  "normal_strength": 0.0,
                 "notes": "Billboard/alpha-blend path; PBR constants mostly irrelevant."},
    "PARTICLE": {"roughness": 1.0,  "metallic": 0.0,  "emissive_strength": 0.3, "specular_strength": 0.0,  "normal_strength": 0.0,
                 "notes": "Effect path; leave out of MaterialRegistry scope."},
    "SKY":      {"roughness": 1.0,  "metallic": 0.0,  "emissive_strength": 0.15, "specular_strength": 0.0, "normal_strength": 0.0,
                 "notes": "Skyboxes: unlit/atmosphere; exclude from world lighting; usable as IBL source later."},
    "UI_2D":    {"roughness": 0.0,  "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.0,  "normal_strength": 0.0,
                 "notes": "PROTECTED. No PBR, no fog, no world lighting, no RTX. Values listed only to make exclusion explicit."},
    "SPRITE":   {"roughness": 0.0,  "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.0,  "normal_strength": 0.0,
                 "notes": "PROTECTED like UI; world-anchored billboards may keep fog, per-case."},
    "TEXT":     {"roughness": 0.0,  "metallic": 0.0,  "emissive_strength": 0.0, "specular_strength": 0.0,  "normal_strength": 0.0,
                 "notes": "PROTECTED. Font rendering must never receive world-space effects."},
    "UNKNOWN":  {"roughness": None, "metallic": None, "emissive_strength": None, "specular_strength": None, "normal_strength": None,
                 "notes": "NO DEFAULTS BY RULE. UNKNOWN = classic pipeline fallback (MATERIAL_CLASSIFICATION_SPEC.md rule 3)."}
  }
}
with open(os.path.join(OUT, "05_PBR_DEFAULTS.json"), "w", encoding="utf-8") as f:
    json.dump(pbr_defaults, f, indent=2)

# ---------------------------------------------------------------- write 06 special materials
with open(os.path.join(OUT, "06_SPECIAL_MATERIALS.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["resource","special_type","reason","confidence","recommended_future_path"])
    n6 = 0
    for row in rows:
        if row["resource_type"] not in ("TEXTURE","PNG_FILE"): continue
        cat = row["material_category"]
        special = None
        if cat == "WATER": special = "WATER"
        elif cat == "LAVA": special = "LAVA"
        elif cat == "GLASS": special = "GLASS"
        elif cat == "ICE": special = "ICE"
        elif cat == "MAGIC": special = "MAGIC"
        elif cat == "EMISSIVE": special = "EMISSIVE"
        elif row["transparent"] == "likely": special = "TRANSPARENT/ALPHA_TEST"
        elif cat == "UI_2D": special = "UI_2D"
        elif cat == "SPRITE": special = "SPRITE"
        if special:
            path_rec = {
                "WATER": "special water shader path (reflection/scroll); RTX reflection candidate; preserve original combiner",
                "LAVA": "animated UV-scroll emissive path; preserve scroll speed as authored",
                "GLASS": "transmission/blend path; preserve alpha ordering",
                "ICE": "high-specular translucent path; validate against original IA alpha",
                "MAGIC": "emissive billboard/effect path; classic-pipeline first",
                "EMISSIVE": "emissive boost after per-resource validation",
                "TRANSPARENT/ALPHA_TEST": "alpha-test threshold mapping (0.5) to original AC_THRESHOLD behavior",
                "UI_2D": "UI-protected 2D path: no fog/PBR/world-lighting/RTX ever",
                "SPRITE": "protected sprite path; fog policy per-case",
            }[special]
            w.writerow([row["resource_id"], special, row["evidence"], row["confidence"], path_rec])
            n6 += 1
print("special materials rows:", n6)

# ---------------------------------------------------------------- write 07 UI protection
with open(os.path.join(OUT, "07_UI_PROTECTION_LIST.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["resource","category","reason","confidence"])
    n7 = 0
    for row in rows:
        if row["ui_protected"] in ("yes","check") and row["resource_type"] in ("TEXTURE","PNG_FILE"):
            w.writerow([row["resource_id"], row["material_category"], row["evidence"],
                        "HIGH" if row["ui_protected"] == "yes" else "LOW"])
            n7 += 1
print("ui protected rows:", n7)

# ---------------------------------------------------------------- write 08 emissive candidates
strength_class = {
    "WATER": "SPECIAL", "LAVA": "STRONG", "MAGIC": "STRONG", "EMISSIVE": "MEDIUM",
}
def emis_class(sym, cat):
    s = norm(sym)
    if TORCH_RE.search(sym) or "brazier" in s: return "STRONG"
    if LAVA_RE.search(sym): return "STRONG"
    if NAV_FAIRY_RE.search(sym): return "STRONG"
    if SPIRIT_RE.search(sym): return "MEDIUM"
    if MAGIC_RE.search(sym): return "MEDIUM"
    if EMISSIVE_RE.search(sym): return "MEDIUM"
    if re.search(r"(sun|moon|star)", s): return "SUBTLE"
    return {"LAVA":"STRONG","MAGIC":"MEDIUM","EMISSIVE":"MEDIUM"}.get(cat, "SUBTLE")

with open(os.path.join(OUT, "08_EMISSIVE_CANDIDATES.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["resource","object_or_scene","confidence","reason","suggested_emissive_strength_class"])
    n8 = 0
    for row in rows:
        if row["emissive_candidate"] in ("yes","likely") and row["resource_type"] in ("TEXTURE","PNG_FILE"):
            w.writerow([row["resource_id"], row["scene_or_object"], row["confidence"],
                        row["evidence"], emis_class(row["resource_id"], row["material_category"])])
            n8 += 1
print("emissive candidates:", n8)

# ---------------------------------------------------------------- write 09 water surfaces
water_type_hints = [
    (re.compile(r"(waterfall|falls)", re.I), "waterfall"),
    (re.compile(r"(river|stream|brook)", re.I), "river"),
    (re.compile(r"(fountain)", re.I), "fountain"),
    (re.compile(r"(lake)", re.I), "lake"),
    (re.compile(r"(pool|pond|moat|warp_?pad)", re.I), "pool"),
    (re.compile(r"(ocean|sea|wave)", re.I), "unknown"),
    (re.compile(r"(ice|frozen)", re.I), "unknown"),
]
def water_subtype(sym):
    s = norm(sym)
    for rx, name in water_type_hints:
        if rx.search(s): return name
    return "unknown"

with open(os.path.join(OUT, "09_WATER_SURFACES.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["resource","water_type","object_or_scene","confidence","reason"])
    n9 = 0
    for row in rows:
        if row["material_category"] == "WATER" and row["resource_type"] in ("TEXTURE","PNG_FILE"):
            w.writerow([row["resource_id"], water_subtype(row["resource_id"]), row["scene_or_object"],
                        row["confidence"], row["evidence"]])
            n9 += 1
print("water surfaces:", n9)

# ---------------------------------------------------------------- write 10 metal candidates
with open(os.path.join(OUT, "10_METAL_CANDIDATES.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["resource","object_or_scene","confidence","reason","partial_metal_risk"])
    n10 = 0
    for row in rows:
        if row["material_category"] == "METAL" and row["resource_type"] in ("TEXTURE","PNG_FILE"):
            sym = row["resource_id"]
            partial = "yes" if re.search(r"(shield|sword|door|chest|gate|grate|armor|machine)", sym, re.I) else "check"
            w.writerow([row["resource_id"], row["scene_or_object"], row["confidence"],
                        row["evidence"], partial])
            n10 += 1
print("metal candidates:", n10)

# ---------------------------------------------------------------- persist classified for later steps
with open(os.path.join(WORK, "classified.json"), "w", encoding="utf-8") as f:
    json.dump(rows, f)

# ---------------------------------------------------------------- quick coverage preview
cov = defaultdict(int)
for row in rows:
    cov["total"] += 1
    cov[f"conf:{row['confidence']}"] += 1
    cov[f"cat:{row['material_category']}"] += 1
    if row["ui_protected"] == "yes": cov["ui_protected"] += 1
    if row["emissive_candidate"]: cov["emissive"] += 1
    if row["water_candidate"]: cov["water"] += 1
print(json.dumps({k: v for k, v in sorted(cov.items()) if k.startswith(("conf","cat","total","ui","emis","water"))}, indent=2))
