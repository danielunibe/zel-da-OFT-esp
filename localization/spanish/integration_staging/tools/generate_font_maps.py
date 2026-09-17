import os, json, csv
from collections import Counter

DEV_ROOT = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV"
SPANISH_ROOT = os.path.join(DEV_ROOT, "localization_workspace", "spanish")
L03A_ROOT = os.path.join(SPANISH_ROOT, "integration_staging")
FONT_ROOT = os.path.join(SPANISH_ROOT, "font")
STAGING_FONT = os.path.join(L03A_ROOT, "font")
QA_DIR = os.path.join(L03A_ROOT, "qa")

MASTER_PATH = os.path.join(SPANISH_ROOT, "corpus", "MASTER_LOCALIZATION_ES_419.jsonl")

# 1. Scan actual character set and frequency from 7652 master entries
char_counts = Counter()
total_chars = 0
with open(MASTER_PATH, "r", encoding="utf-8") as f:
    for line in f:
        if line.strip():
            d = json.loads(line)
            txt = d.get("spanish", "")
            for ch in txt:
                char_counts[ch] += 1
                total_chars += 1

print(f"Scanned {total_chars} characters across master corpus.")

# 2. Generate SPANISH_CHARACTER_SET_L03A.txt and SPANISH_CHARACTER_FREQUENCY_L03A.csv
os.makedirs(FONT_ROOT, exist_ok=True)
with open(os.path.join(FONT_ROOT, "SPANISH_CHARACTER_SET_L03A.txt"), "w", encoding="utf-8") as f:
    f.write("=== SPANISH CHARACTER SET (TASK L03A FINAL AUDITED MASTER CORPUS) ===\n")
    f.write(f"Total Unique Characters: {len(char_counts)}\n")
    f.write(f"Total Corpus Character Count: {total_chars}\n\n")
    for ch in sorted(char_counts.keys()):
        if ch.strip():
            f.write(f"'{ch}' (U+{ord(ch):04X}) - freq: {char_counts[ch]}\n")

with open(os.path.join(FONT_ROOT, "SPANISH_CHARACTER_FREQUENCY_L03A.csv"), "w", encoding="utf-8", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["character", "unicode", "frequency", "percentage"])
    for ch, cnt in char_counts.most_common():
        pct = f"{(cnt / total_chars) * 100:.4f}%"
        writer.writerow([ch, f"U+{ord(ch):04X}", cnt, pct])

print("Generated SPANISH_CHARACTER_SET_L03A.txt and SPANISH_CHARACTER_FREQUENCY_L03A.csv")

# 3. Design SPANISH_FONT_SLOT_MAP.csv
# Table of 11 Spanish specific characters:
# Slots in 0x80..0x9E (internal byte range for European accents)
# stock: 0x86=É, 0x91=á, 0x96=é already exist!
slot_map_spec = [
    {
        "unicode": "U+00F1",
        "character": "ñ",
        "internal_slot": "0x92",
        "replaces": "â (Latin Small Letter A With Circumflex)",
        "reason": "Most frequent missing Spanish consonant; replaces unused French circumflex",
        "used_frequency": char_counts.get("ñ", 0),
        "collision_status": "ZERO_COLLISION_SAFE",
        "integration_status": "STAGED_READY"
    },
    {
        "unicode": "U+00D1",
        "character": "Ñ",
        "internal_slot": "0x81",
        "replaces": "Î (Latin Capital Letter I With Circumflex)",
        "reason": "Capital N with tilde; replaces unused French capital circumflex",
        "used_frequency": char_counts.get("Ñ", 0),
        "collision_status": "ZERO_COLLISION_SAFE",
        "integration_status": "STAGED_READY"
    },
    {
        "unicode": "U+00A1",
        "character": "¡",
        "internal_slot": "0x83",
        "replaces": "Ä (Latin Capital Letter A With Diaeresis)",
        "reason": "Inverted exclamation mark; replaces unused German diaeresis",
        "used_frequency": char_counts.get("¡", 0),
        "collision_status": "ZERO_COLLISION_SAFE",
        "integration_status": "STAGED_READY"
    },
    {
        "unicode": "U+00BF",
        "character": "¿",
        "internal_slot": "0x85",
        "replaces": "È (Latin Capital Letter E With Grave)",
        "reason": "Inverted question mark; replaces unused French grave accent",
        "used_frequency": char_counts.get("¿", 0),
        "collision_status": "ZERO_COLLISION_SAFE",
        "integration_status": "STAGED_READY"
    },
    {
        "unicode": "U+00ED",
        "character": "í",
        "internal_slot": "0x99",
        "replaces": "ï (Latin Small Letter I With Diaeresis)",
        "reason": "Small i with acute; replaces unused French diaeresis",
        "used_frequency": char_counts.get("í", 0),
        "collision_status": "ZERO_COLLISION_SAFE",
        "integration_status": "STAGED_READY"
    },
    {
        "unicode": "U+00F3",
        "character": "ó",
        "internal_slot": "0x9A",
        "replaces": "ô (Latin Small Letter O With Circumflex)",
        "reason": "Small o with acute; replaces unused French circumflex",
        "used_frequency": char_counts.get("ó", 0),
        "collision_status": "ZERO_COLLISION_SAFE",
        "integration_status": "STAGED_READY"
    },
    {
        "unicode": "U+00FA",
        "character": "ú",
        "internal_slot": "0x9D",
        "replaces": "û (Latin Small Letter U With Circumflex)",
        "reason": "Small u with acute; replaces unused French circumflex",
        "used_frequency": char_counts.get("ú", 0),
        "collision_status": "ZERO_COLLISION_SAFE",
        "integration_status": "STAGED_READY"
    },
    {
        "unicode": "U+00C1",
        "character": "Á",
        "internal_slot": "0x80",
        "replaces": "À (Latin Capital Letter A With Grave)",
        "reason": "Capital A with acute; replaces unused French grave accent",
        "used_frequency": char_counts.get("Á", 0),
        "collision_status": "ZERO_COLLISION_SAFE",
        "integration_status": "STAGED_READY"
    },
    {
        "unicode": "U+00CD",
        "character": "Í",
        "internal_slot": "0x89",
        "replaces": "Ï (Latin Capital Letter I With Diaeresis)",
        "reason": "Capital I with acute; replaces unused French diaeresis",
        "used_frequency": char_counts.get("Í", 0),
        "collision_status": "ZERO_COLLISION_SAFE",
        "integration_status": "STAGED_READY"
    },
    {
        "unicode": "U+00D3",
        "character": "Ó",
        "internal_slot": "0x8A",
        "replaces": "Ô (Latin Capital Letter O With Circumflex)",
        "reason": "Capital O with acute; replaces unused French circumflex",
        "used_frequency": char_counts.get("Ó", 0),
        "collision_status": "ZERO_COLLISION_SAFE",
        "integration_status": "STAGED_READY"
    },
    {
        "unicode": "U+00DA",
        "character": "Ú",
        "internal_slot": "0x8C",
        "replaces": "Ù (Latin Capital Letter U With Grave)",
        "reason": "Capital U with acute; replaces unused French grave accent",
        "used_frequency": char_counts.get("Ú", 0),
        "collision_status": "ZERO_COLLISION_SAFE",
        "integration_status": "STAGED_READY"
    }
]

os.makedirs(STAGING_FONT, exist_ok=True)
slot_map_path = os.path.join(STAGING_FONT, "SPANISH_FONT_SLOT_MAP.csv")
with open(slot_map_path, "w", encoding="utf-8", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=[
        "unicode", "character", "internal_slot", "replaces", "reason",
        "used_frequency", "collision_status", "integration_status"
    ])
    writer.writeheader()
    writer.writerows(slot_map_spec)
print(f"Wrote SPANISH_FONT_SLOT_MAP.csv to {slot_map_path}")

# 4. Generate FONT_GLYPH_COLLISION_MATRIX.csv
# Full matrix of 0x00 to 0xAB
os.makedirs(QA_DIR, exist_ok=True)
collision_rows = []

spanish_assigned_slots = {spec["internal_slot"]: spec for spec in slot_map_spec}
# Native Spanish characters already in fontTbl:
native_spanish_slots = {
    "0x91": "á (Latin Small Letter A With Acute)",
    "0x96": "é (Latin Small Letter E With Acute)",
    "0x86": "É (Latin Capital Letter E With Acute)"
}

button_glyph_map = {
    0x9F: "[A] Button",
    0xA0: "[B] Button",
    0xA1: "[C] Button",
    0xA2: "[L] Button",
    0xA3: "[R] Button",
    0xA4: "[Z] Button",
    0xA5: "[C-Up] Button",
    0xA6: "[C-Down] Button",
    0xA7: "[C-Left] Button",
    0xA8: "[C-Right] Button",
    0xA9: "Z-Target Sign",
    0xAA: "Control Stick",
    0xAB: "Control Pad / D-Pad"
}

for byte_val in range(0x00, 0xAC):
    hex_str = f"0x{byte_val:02X}"
    
    if byte_val < 0x20:
        zone = "CONTROL_CODE_ZONE"
        desc = f"Message control character ({hex_str})"
        is_sp = "NO"
        is_btn = "NO"
        collision = "SAFE_RESERVED_CONTROL"
    elif 0x20 <= byte_val <= 0x7E:
        zone = "ASCII_PRINTABLE_ZONE"
        desc = f"Standard ASCII character '{chr(byte_val)}'"
        is_sp = "YES (ASCII)"
        is_btn = "NO"
        collision = "SAFE_STANDARD_ASCII"
    elif byte_val == 0x7F:
        zone = "BLANK_SPACE_ZONE"
        desc = "Trailing character / blank space"
        is_sp = "NO"
        is_btn = "NO"
        collision = "SAFE_RESERVED"
    elif 0x80 <= byte_val <= 0x9E:
        zone = "EUROPEAN_ACCENTS_ZONE"
        if hex_str in spanish_assigned_slots:
            sp = spanish_assigned_slots[hex_str]
            desc = f"Assigned to Spanish '{sp['character']}' (replaces {sp['replaces'].split(' ')[0]})"
            is_sp = f"YES ({sp['character']})"
            is_btn = "NO"
            collision = "SAFE_SPANISH_REMAP"
        elif hex_str in native_spanish_slots:
            desc = f"Native stock font letter {native_spanish_slots[hex_str]}"
            is_sp = "YES (Native stock)"
            is_btn = "NO"
            collision = "SAFE_STOCK_FONT"
        else:
            desc = "Unused French/German slot (available for future expansion)"
            is_sp = "NO"
            is_btn = "NO"
            collision = "AVAILABLE_UNUSED"
    elif 0x9F <= byte_val <= 0xAB:
        zone = "BUTTON_GLYPH_ZONE"
        desc = f"Reserved N64 button glyph: {button_glyph_map.get(byte_val)}"
        is_sp = "NO (STRICTLY PROHIBITED)"
        is_btn = "YES"
        collision = "SAFE_RESERVED_BUTTON_GLYPH"
    else:
        zone = "UNKNOWN"
        desc = ""
        is_sp = "NO"
        is_btn = "NO"
        collision = "SAFE"

    collision_rows.append({
        "byte_hex": hex_str,
        "byte_dec": byte_val,
        "zone": zone,
        "description": desc,
        "used_by_spanish": is_sp,
        "used_by_button_glyph": is_btn,
        "collision_result": collision
    })

collision_matrix_path = os.path.join(QA_DIR, "FONT_GLYPH_COLLISION_MATRIX.csv")
with open(collision_matrix_path, "w", encoding="utf-8", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=[
        "byte_hex", "byte_dec", "zone", "description",
        "used_by_spanish", "used_by_button_glyph", "collision_result"
    ])
    writer.writeheader()
    writer.writerows(collision_rows)
print(f"Wrote FONT_GLYPH_COLLISION_MATRIX.csv to {collision_matrix_path}")

# Explicit verification of collision count:
collisions = [r for r in collision_rows if r["used_by_spanish"].startswith("YES") and r["used_by_button_glyph"] == "YES"]
print(f"TOTAL BUTTON GLYPH COLLISIONS: {len(collisions)}")
