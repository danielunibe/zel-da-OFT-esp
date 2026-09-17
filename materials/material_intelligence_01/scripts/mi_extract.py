#!/usr/bin/env python3
"""
MATERIAL INTELLIGENCE 01 - extractor
READ-ONLY: parses asset headers + GC_NMQ_NTSC_U XML ground truth.
Writes ONLY inside OUTPUT_ROOT.
"""
import os, re, csv, json, glob
from collections import defaultdict

ASSETS = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\source\shipwright\soh\assets"
OUT    = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\visual_workspace\material_intelligence_01"
WORK   = os.path.join(OUT, "_work")
os.makedirs(WORK, exist_ok=True)

DEF_RE   = re.compile(r'#define\s+(d[A-Za-z0-9_]+)\s+"__OTR__([^"]+)"')
SYM_RE   = re.compile(r'static const ALIGN_ASSET\(\d+\)\s+char\s+([A-Za-z0-9_]+)\[\]\s*=\s*(d[A-Za-z0-9_]+);')

SCENE_CATS = ["overworld", "dungeons", "indoors", "misc", "shops", "test_levels"]

# Known scene-id -> display name (source: z_select.c scene select + decomp conventions)
SCENE_NAMES = {
    "spot00": "Hyrule Field", "spot01": "Kakariko Village", "spot02": "Graveyard",
    "spot03": "Zora's River", "spot04": "Kokiri Forest", "spot05": "Sacred Forest Meadow",
    "spot06": "Lake Hylia", "spot07": "Zora's Domain", "spot08": "Zora's Fountain",
    "spot09": "Gerudo Valley", "spot10": "Lost Woods", "spot11": "Desert Colossus",
    "spot12": "Gerudo's Fortress", "spot13": "Haunted Wasteland", "spot15": "Hyrule Castle",
    "spot16": "Lon Lon Ranch", "spot17": "Rainbow Bridge area", "spot18": "Kokiri Forest (outdoor?)",
    "spot20": "Ganon's Castle exterior", "tokinoma": "Temple of Time", "ydan": "Deku Tree",
    "ddan": "Dodongo's Cavern", "bdan": "Bottom of the Well", "HIDAN": "Fire Temple",
    "MIZUsin": "Water Temple", "jyasinzou": "Spirit Temple", "HAKAdan": "Shadow Temple",
    "Bmori1": "Forest Temple", "ice_doukutu": "Ice Cavern", "ganon": "Inside Ganon's Castle",
    "men": "Thieves' Hideout", "entra": "Hyrule Castle Courtyard (entra)", "souko": "Market (Bazaar?)",
    "market_day": "Market (Day)", "market_night": "Market (Night)", "shrine": "Great Fairy Fountain",
    "kokiri_home": "Kokiri House (Link's Tree House)", "link_home": "Link's House",
    "hut": "Kakariko House (Carpenters?)", "takaraya": "Treasure Chest Game",
    "hairal_niwa": "Ganondorf's Castle surroundings (hairal_niwa)",
}

def bucket_for(rel_path_no_ext):
    """classify a header path into (bucket, category hints)."""
    p = rel_path_no_ext.replace("\\", "/")
    if p.startswith("objects/"):
        parts = p.split("/")
        return ("object", parts[1])
    if p.startswith("scenes/"):
        parts = p.split("/")
        cat = parts[1] if len(parts) > 1 else "scenes"
        scene = parts[2] if len(parts) > 2 else "?"
        return ("scene", f"{cat}/{scene}")
    if p.startswith("textures/"):
        parts = p.split("/")
        return ("texture_group", parts[1] if len(parts) > 1 else p)
    if p.startswith("overlays/"):
        parts = p.split("/")
        return ("overlay", parts[1])
    if p.startswith("custom/"):
        return ("custom", p)
    if p.startswith("misc/"):
        return ("misc", parts0(p))
    if p.startswith("code/"):
        return ("code", parts0(p))
    return ("other", p)

def parts0(p):
    return p.split("/")[0] if "/" in p else p

records = {}   # otr_path -> record (dedupe by OTR path)
dups = defaultdict(list)

def add_record(sym, macro, otr_path, src):
    if otr_path in records:
        dups[otr_path].append(src)
        return
    # resource type from symbol suffix
    s = sym
    if s.endswith("TLUT") or "TLUT_" in s or s.endswith("Pal") or "_pal_" in s:
        rtype = "PALETTE"
    elif s.endswith("DL") or re.search(r"DL_[0-9A-Fa-f]{6}$", s):
        rtype = "DISPLAY_LIST"
    elif s.endswith("Vtx") or re.search(r"Vtx(_[0-9A-Fa-f]{6})?$", s):
        rtype = "GEOMETRY"
    elif "Tex" in s or s.endswith("Texture") or s.endswith("TEX"):
        rtype = "TEXTURE"
    else:
        rtype = "ASSET_REF"
    bucket, group = bucket_for(src)
    records[otr_path] = {
        "symbol": sym, "macro": macro, "otr_path": otr_path,
        "source_file": src.replace("\\", "/"),
        "resource_type": rtype, "bucket": bucket, "group": group,
        "scene": None, "room": None, "object": None,
        "format": "", "width": 0, "height": 0, "xml_hit": False,
    }

# ---- 1. parse all asset headers ----
headers = glob.glob(os.path.join(ASSETS, "**", "*.h"), recursive=True)
n_hdr = 0
for h in headers:
    rel = os.path.relpath(h, ASSETS).replace("\\", "/")
    # skip non-asset headers
    if rel.startswith(("xml", "extractor", "sources", "code", "custom/helpers", "custom/accessibility")):
        if not rel.startswith("custom/"):
            continue
    if rel == "soh_assets.h" or rel.endswith("align_asset_macro.h"):
        continue
    n_hdr += 1
    try:
        txt = open(h, "r", encoding="utf-8", errors="replace").read()
    except OSError:
        continue
    macros = dict(DEF_RE.findall(txt))
    for sym, mac in SYM_RE.findall(txt):
        otr = macros.get(mac)
        if otr:
            add_record(sym, mac, otr, rel)

print(f"headers parsed: {n_hdr}, unique OTR records: {len(records)}, dup refs: {len(dups)}")

# ---- 2. scene annotations ----
for r in records.values():
    src = r["source_file"]
    m = re.match(r"scenes/([^/]+)/([^/]+)/([^/]+?)_room_(\d+)\.h$", src)
    if m:
        r["bucket"] = "scene"; r["group"] = f"{m.group(1)}/{m.group(2)}"
        r["scene"] = m.group(2); r["room"] = int(m.group(4)); r["object"] = None
        continue
    m = re.match(r"scenes/([^/]+)/([^/]+)/([^/]+?)_scene\.h$", src)
    if m:
        r["bucket"] = "scene"; r["group"] = f"{m.group(1)}/{m.group(2)}"
        r["scene"] = m.group(2); r["room"] = None; r["object"] = None
        continue
    m = re.match(r"objects/([^/]+)/", src)
    if m:
        r["bucket"] = "object"; r["object"] = m.group(1)

# ---- 3. XML ground truth (GC_NMQ_NTSC_U = game default config) ----
xml_root = os.path.join(ASSETS, "xml", "GC_NMQ_NTSC_U")
xml_tex = {}   # symbol Name -> dict
for xf in glob.glob(os.path.join(xml_root, "**", "*.xml"), recursive=True):
    txt = open(xf, "r", encoding="utf-8", errors="replace").read()
    for mm in re.finditer(r'<Texture\s+Name="([^"]+)"\s+OutName="([^"]+)"\s+Format="([a-z0-9]+)"(?:\s+Width="(\d+)")?(?:\s+Height="(\d+)")?', txt):
        name, outname, fmt, w, hgt = mm.group(1), mm.group(2), mm.group(3), mm.group(4), mm.group(5)
        xml_tex[name] = {"format": fmt, "width": int(w) if w else 0,
                         "height": int(hgt) if hgt else 0,
                         "xml_rel": os.path.relpath(xf, xml_root).replace("\\", "/")}
xml_hits = 0
for r in records.values():
    key = r["symbol"]
    if key in xml_tex:
        x = xml_tex[key]
        r["format"] = x["format"]; r["width"] = x["width"]; r["height"] = x["height"]
        r["xml_hit"] = True; r["xml_rel"] = x["xml_rel"]; xml_hits += 1
print(f"xml texture entries: {len(xml_tex)}, matched to records: {xml_hits}")

# ---- 4. custom PNG / font assets (real files on disk) ----
png_records = []
for pf in glob.glob(os.path.join(ASSETS, "custom", "**", "*.png"), recursive=True):
    rel = os.path.relpath(pf, ASSETS).replace("\\", "/")
    base = os.path.basename(rel)
    fmt = ""
    m = re.search(r"\.(rgba16|rgba32|rgb5a1|ia8|ia4|ia16|i4|i8|ci4|ci8)\.png$", base)
    if m: fmt = m.group(1)
    png_records.append({
        "symbol": base, "macro": "", "otr_path": "__PNG__/" + rel,
        "source_file": rel, "resource_type": "PNG_FILE", "bucket": "custom_png",
        "group": rel.split("/")[1] if "/" in rel else rel,
        "scene": None, "room": None, "object": None,
        "format": fmt, "width": 0, "height": 0, "xml_hit": False,
    })
print(f"custom PNG files: {len(png_records)}")
for p in png_records:
    records[p["otr_path"]] = p

# ---- 5. write inventory CSV ----
inv_csv = os.path.join(OUT, "02_RESOURCE_INVENTORY.csv")
with open(inv_csv, "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["resource_id", "resource_path", "filename", "source_file", "resource_type",
                "scene_or_object", "likely_usage", "notes"])
    for otr in sorted(records):
        r = records[otr]
        rid = r["symbol"]
        scene_or_object = ""
        if r["scene"]: scene_or_object = f"scene:{r['scene']}" + (f"/room{r['room']}" if r["room"] is not None else "")
        elif r["object"]: scene_or_object = f"object:{r['object']}"
        elif r["bucket"] == "texture_group": scene_or_object = f"texture_group:{r['group']}"
        elif r["bucket"] == "overlay": scene_or_object = f"overlay:{r['group']}"
        usage = ""
        notes = []
        if r["resource_type"] == "PALETTE": usage = "color palette (TLUT) for CI textures"
        elif r["resource_type"] == "DISPLAY_LIST": usage = "display list (draw commands; references materials)"
        elif r["resource_type"] == "GEOMETRY": usage = "vertex buffer (geometry)"
        elif r["resource_type"] == "TEXTURE": usage = "texture"
        elif r["resource_type"] == "PNG_FILE": usage = "custom texture file (actual file on disk)"
        if r["xml_hit"]: notes.append(f"format={r['format']}" + (f" {r['width']}x{r['height']}" if r['width'] else ""))
        if not r["xml_hit"] and r["resource_type"] == "TEXTURE": notes.append("format not in NMQ xml (scene-internal or non-NMQ)")
        if r["format"] in ("ia4", "ia8", "ia16"): notes.append("IA format: intensity+alpha capable")
        if r["format"] in ("ci4", "ci8"): notes.append("CI format: palette-based")
        if otr in dups: notes.append(f"symbol also declared in: {'; '.join(sorted(set(dups[otr])))}")
        w.writerow([rid, otr, os.path.basename(otr), r["source_file"], r["resource_type"],
                    scene_or_object, usage, "; ".join(notes)])

# ---- 6. dump work json ----
with open(os.path.join(WORK, "inventory.json"), "w", encoding="utf-8") as f:
    json.dump({"records": records, "dup_sources": {k: sorted(set(v)) for k, v in dups.items()}}, f)

# ---- 7. quick stats ----
stats = defaultdict(int)
for r in records.values():
    stats[f"bucket:{r['bucket']}"] += 1
    stats[f"type:{r['resource_type']}"] += 1
    stats[f"scene:{r['scene'] or '-'}"] += 1
print(json.dumps({k: v for k, v in sorted(stats.items()) if not k.startswith("scene:")}, indent=2))
sc = [(k.split(":",1)[1], v) for k, v in stats.items() if k.startswith("scene:") and not k.endswith(":-")]
print("scenes with records:", len(sc))
print("PILOTS:", {s: dict(sc).get(s, 0) for s in ("tokinoma", "spot04", "spot00")})
