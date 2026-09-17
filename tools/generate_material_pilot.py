import json
import csv
import hashlib
import os

def file_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

data_dir = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\visual_workspace\material_intelligence_01"
master_gen = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\visual_workspace\v03_1_master\generated"
include_dest = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\source\shipwright\libultraship\include\fast"

os.makedirs(master_gen, exist_ok=True)
os.makedirs(include_dest, exist_ok=True)

cand_json = os.path.join(data_dir, "17_MATERIAL_RULES_CANDIDATE.json")
ui_csv = os.path.join(data_dir, "07_UI_PROTECTION_LIST.csv")
emissive_csv = os.path.join(data_dir, "08_EMISSIVE_CANDIDATES.csv")
water_csv = os.path.join(data_dir, "09_WATER_SURFACES.csv")
metal_csv = os.path.join(data_dir, "10_METAL_CANDIDATES.csv")
pbr_json = os.path.join(data_dir, "05_PBR_DEFAULTS.json")

input_hashes = {
    "17_MATERIAL_RULES_CANDIDATE.json": file_sha256(cand_json),
    "07_UI_PROTECTION_LIST.csv": file_sha256(ui_csv),
    "08_EMISSIVE_CANDIDATES.csv": file_sha256(emissive_csv),
    "09_WATER_SURFACES.csv": file_sha256(water_csv),
    "10_METAL_CANDIDATES.csv": file_sha256(metal_csv),
    "05_PBR_DEFAULTS.json": file_sha256(pbr_json)
}

with open(cand_json, "r", encoding="utf-8") as f:
    cand_data = json.load(f)

# Priority 1: UI Protection resources (set of resource names / paths)
ui_protected_patterns = [
    "icon_item_static", "parameter_static", "message_static", "title_static",
    "map_grand_static", "map_i_static", "map_48x85_static", "buttons",
    "gFont", "Font", "Letter", "Text", "Heart", "Rupee", "Key", "Cursor",
    "gTitle", "gFileSelect"
]

accepted_rules = []
rejected_low_rules = []

cat_map = {
    "STONE": ("MaterialType::STONE", 0.85, 0.0, 0.25, 0.0),
    "WOOD": ("MaterialType::WOOD", 0.75, 0.0, 0.15, 0.0),
    "METAL": ("MaterialType::METAL", 0.35, 0.9, 0.8, 0.0),
    "WATER": ("MaterialType::WATER", 0.05, 0.0, 0.5, 0.0),
    "GRASS": ("MaterialType::GRASS", 0.9, 0.0, 0.08, 0.0),
    "FOLIAGE": ("MaterialType::FOLIAGE", 0.8, 0.0, 0.1, 0.0),
    "EARTH": ("MaterialType::EARTH", 0.95, 0.0, 0.05, 0.0),
    "SAND": ("MaterialType::SAND", 0.95, 0.0, 0.05, 0.0),
    "GLASS": ("MaterialType::GLASS", 0.1, 0.0, 0.7, 0.0),
    "ICE": ("MaterialType::ICE", 0.15, 0.0, 0.6, 0.0),
    "LAVA": ("MaterialType::LAVA", 0.3, 0.0, 0.2, 0.85),
    "EMISSIVE": ("MaterialType::EMISSIVE", 0.5, 0.0, 0.1, 0.8),
    "MAGIC": ("MaterialType::MAGIC", 0.4, 0.0, 0.3, 0.5),
    "CLOTH": ("MaterialType::FABRIC", 0.9, 0.0, 0.08, 0.0),
    "LEATHER": ("MaterialType::FABRIC", 0.75, 0.0, 0.15, 0.0),
    "BONE": ("MaterialType::STONE", 0.7, 0.0, 0.25, 0.0),
    "CERAMIC": ("MaterialType::STONE", 0.4, 0.0, 0.45, 0.0),
    "SKIN": ("MaterialType::CHARACTER", 0.65, 0.0, 0.3, 0.0),
    "UI_2D": ("MaterialType::PROTECTED_2D", 0.5, 0.0, 0.0, 0.0)
}

pilot_keywords = ["tokinoma", "spot04", "spot00", "dangeon_keep", "link_boy", "link_child", "zelda", "navi"]

seen_paths = set()
for r in cand_data.get("exact_rules", []):
    path = r.get("path", "")
    cat = r.get("category", "UNKNOWN")
    conf = r.get("confidence", "LOW")
    
    if conf == "LOW":
        rejected_low_rules.append(r)
        continue
        
    is_ui = any(p.lower() in path.lower() for p in ui_protected_patterns)
    if is_ui:
        cat = "UI_2D"
        
    if cat not in cat_map:
        cat = "STONE"
        
    mtype, rough, metal, spec, emiss = cat_map[cat]
    
    is_pilot = any(k in path.lower() for k in pilot_keywords)
    is_special = cat in ["METAL", "WATER", "EMISSIVE", "UI_2D"]
    
    if conf == "HIGH" or is_pilot or is_special:
        if path not in seen_paths:
            seen_paths.add(path)
            accepted_rules.append({
                "path": path,
                "symbol": r.get("symbol", ""),
                "category": cat,
                "confidence": conf,
                "type_enum": mtype,
                "roughness": rough,
                "metallic": metal,
                "specular": spec,
                "emissive": emiss
            })

print(f"Accepted rules count: {len(accepted_rules)}")
print(f"Rejected LOW rules count: {len(rejected_low_rules)}")

header_lines = [
    "#pragma once\n",
    "\n",
    "// AUTO-GENERATED CANONICAL MATERIAL RULES PILOT\n",
    "// Deterministically generated from Workstream B Material Intelligence Dataset\n",
    "// Target: Ocarina of Time PC — Couch Edition V03.1\n",
    "// Strict adherence to UI protection priority and pilot scene boundaries\n",
    "\n",
    "#include \"fast/MaterialRegistry.h\"\n",
    "#include \"ship/utils/StrHash64.h\"\n",
    "#include <vector>\n",
    "#include <unordered_map>\n",
    "#include <cstring>\n",
    "\n",
    "namespace Fast {\n",
    "\n",
    "struct StaticMaterialRule {\n",
    "    const char* path;\n",
    "    MaterialType type;\n",
    "    float roughness;\n",
    "    float metallic;\n",
    "    float specular;\n",
    "    float emissiveStrength;\n",
    "};\n",
    "\n",
    "static const StaticMaterialRule gMaterialRulesPilot[] = {\n"
]

for r in accepted_rules:
    p = r['path']
    te = r['type_enum']
    ro = r['roughness']
    me = r['metallic']
    sp = r['specular']
    em = r['emissive']
    header_lines.append(f'    {{ "{p}", {te}, {ro}f, {me}f, {sp}f, {em}f }},\n')

header_lines.extend([
    "};\n",
    "\n",
    "static constexpr size_t gMaterialRulesPilotCount = sizeof(gMaterialRulesPilot) / sizeof(gMaterialRulesPilot[0]);\n",
    "\n",
    "inline void PopulateMaterialRulesTable(std::unordered_map<uint64_t, MaterialDefinition>& outRules,\n",
    "                                      std::unordered_map<std::string, MaterialDefinition>& outStringRules) {\n",
    "    outRules.reserve(gMaterialRulesPilotCount);\n",
    "    for (size_t i = 0; i < gMaterialRulesPilotCount; ++i) {\n",
    "        const auto& r = gMaterialRulesPilot[i];\n",
    "        MaterialDefinition def;\n",
    "        def.type = r.type;\n",
    "        def.roughness = r.roughness;\n",
    "        def.metallic = r.metallic;\n",
    "        def.specular = r.specular;\n",
    "        def.emissiveStrength = r.emissiveStrength;\n",
    "        \n",
    "        uint64_t hash = CRC64(r.path);\n",
    "        outRules[hash] = def;\n",
    "        outStringRules[r.path] = def;\n",
    "    }\n",
    "}\n",
    "\n",
    "inline bool IsResourcePathUIProtected(const char* path) {\n",
    "    if (!path || path[0] == '\\0') return false;\n",
    "    static const char* uiPrefixes[] = {\n",
    "        \"textures/icon_item_static\",\n",
    "        \"textures/parameter_static\",\n",
    "        \"textures/message_static\",\n",
    "        \"textures/title_static\",\n",
    "        \"textures/map_grand_static\",\n",
    "        \"textures/map_i_static\",\n",
    "        \"textures/map_48x85_static\",\n",
    "        \"custom/textures/buttons\",\n",
    "        \"gFont\",\n",
    "        \"Font\",\n",
    "        \"Letter\",\n",
    "        \"Text\",\n",
    "        \"Heart\",\n",
    "        \"Rupee\",\n",
    "        \"Key\",\n",
    "        \"Cursor\",\n",
    "        \"gTitle\",\n",
    "        \"gFileSelect\"\n",
    "    };\n",
    "    for (const char* prefix : uiPrefixes) {\n",
    "        if (strstr(path, prefix) != nullptr) return true;\n",
    "    }\n",
    "    return false;\n",
    "}\n",
    "\n",
    "} // namespace Fast\n"
])

header_content = "".join(header_lines)

header_path1 = os.path.join(include_dest, "MaterialRulesPilot.h")
header_path2 = os.path.join(master_gen, "MaterialRulesPilot.h")

with open(header_path1, "w", encoding="utf-8") as f:
    f.write(header_content)

with open(header_path2, "w", encoding="utf-8") as f:
    f.write(header_content)

gen_hash = file_sha256(header_path1)

snapshot_data = {
    "_meta": {
        "task": "MASTER_VISUAL_CONSOLIDATION_V03_1",
        "phase": "MATERIAL_DATASET_SNAPSHOT",
        "generator": "tools/generate_material_pilot.py"
    },
    "input_datasets": input_hashes,
    "rules_summary": {
        "total_rules_in_candidate_json": len(cand_data.get("exact_rules", [])),
        "accepted_rules_count": len(accepted_rules),
        "rejected_low_confidence_count": len(rejected_low_rules),
        "ui_protected_prioritized": True,
        "pilot_scenes_covered": ["Temple of Time (tokinoma)", "Kokiri Forest (spot04)", "Hyrule Field (spot00)"],
        "special_materials_included": ["METAL", "WATER", "EMISSIVE", "UI_2D"]
    },
    "generated_header": {
        "file": "MaterialRulesPilot.h",
        "sha256": gen_hash
    }
}

snapshot_path = os.path.join(master_gen, "MATERIAL_INTELLIGENCE_SNAPSHOT.json")
with open(snapshot_path, "w", encoding="utf-8") as f:
    json.dump(snapshot_data, f, indent=2)

print(f"Snapshot written to {snapshot_path}")
print(f"Header generated with SHA256: {gen_hash}")
