#!/usr/bin/env python3
"""
generate_font_analysis.py — Font character audit across Main Game, Randomizer, and SoH UI.
Extracts all characters used, calculates frequencies, and identifies missing/remapping glyphs.
Outputs:
- SPANISH_ROOT/font/SPANISH_CHARACTER_SET.txt
- SPANISH_ROOT/font/SPANISH_CHARACTER_FREQUENCY.csv
- SPANISH_ROOT/font/MISSING_SPANISH_GLYPHS.csv
"""

import json
import csv
from pathlib import Path
from collections import Counter

DEV_ROOT = Path(r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV")
LOCALIZATION_ROOT = DEV_ROOT / "localization_workspace"
SPANISH_ROOT = LOCALIZATION_ROOT / "spanish"
FONT_DIR = SPANISH_ROOT / "font"

MAIN_GAME_JSONL = SPANISH_ROOT / "corpus" / "ocarina_messages_es_419.jsonl"
RANDOMIZER_JSONL = SPANISH_ROOT / "corpus" / "randomizer_strings_es_419.jsonl"
SOH_UI_JSONL = SPANISH_ROOT / "corpus" / "soh_ui_strings_es_419.jsonl"

def run_font_analysis():
    FONT_DIR.mkdir(parents=True, exist_ok=True)

    char_counts = Counter()
    total_chars = 0

    # 1. Main Game
    with open(MAIN_GAME_JSONL, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip():
                d = json.loads(line)
                txt = d.get('plain_text_es_419', '')
                char_counts.update(txt)
                total_chars += len(txt)

    # 2. Randomizer
    with open(RANDOMIZER_JSONL, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip():
                d = json.loads(line)
                txt = d.get('plain_text_es_419', '')
                char_counts.update(txt)
                total_chars += len(txt)

    # 3. SoH UI
    with open(SOH_UI_JSONL, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip():
                d = json.loads(line)
                txt = d.get('spanish', '')
                char_counts.update(txt)
                total_chars += len(txt)

    print(f"Audited {total_chars} total characters across all 3 domains.")
    print(f"Found {len(char_counts)} distinct characters.")

    # Save SPANISH_CHARACTER_SET.txt
    unique_chars = sorted(char_counts.keys(), key=lambda c: (ord(c) >= 128, ord(c)))
    char_set_path = FONT_DIR / "SPANISH_CHARACTER_SET.txt"
    with open(char_set_path, 'w', encoding='utf-8') as f:
        f.write("SPANISH LOCALIZATION CHARACTER SET (es-419)\n")
        f.write("Ship of Harkinian — Couch Edition\n")
        f.write("=" * 60 + "\n\n")
        f.write("ASCII Standard:\n")
        ascii_chars = [c for c in unique_chars if ord(c) < 128 and c not in '\n\r\t']
        f.write("".join(ascii_chars) + "\n\n")
        f.write("Spanish Specific & Extended Characters:\n")
        ext_chars = [c for c in unique_chars if ord(c) >= 128]
        f.write("".join(ext_chars) + "\n\n")
        f.write("Character list with codepoints:\n")
        for c in unique_chars:
            if c in '\n\r\t':
                continue
            f.write(f"U+{ord(c):04X} | '{c}' | count: {char_counts[c]}\n")

    # Save SPANISH_CHARACTER_FREQUENCY.csv
    freq_csv_path = FONT_DIR / "SPANISH_CHARACTER_FREQUENCY.csv"
    with open(freq_csv_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["character", "unicode_hex", "decimal", "frequency", "percentage", "domain_notes"])
        for c, count in char_counts.most_common():
            char_repr = repr(c) if c in '\n\r\t ' else c
            pct = f"{(count / total_chars) * 100:.3f}%"
            note = "Extended Latin / Spanish" if ord(c) >= 128 else "Standard ASCII"
            writer.writerow([char_repr, f"U+{ord(c):04X}", ord(c), count, pct, note])

    # Save MISSING_SPANISH_GLYPHS.csv
    # In OoT font system (z_kanfont.c / CustomMessageManager):
    # Available: ASCII (0x20-0x7E), á (0x91), é (0x96), ü (0x9E), É (0x86), Ü (0x8E)
    # Missing/Problematic in vanilla OoT fontTbl:
    # ñ (0xA1 conflict), Ñ (no slot), ¡ (no fontTbl slot), ¿ (no fontTbl slot), í (maps to á byte), ó (maps to ô), ú (maps to ù)
    missing_glyphs_data = [
        {"char": "ñ", "unicode": "U+00F1", "freq": char_counts.get("ñ", 0), "status": "CRITICAL_MISSING", "vanilla_slot": "0xA1 (Conflict with Button C)", "remapping_recommendation": "Remap unused French slot 0x92 (â -> ñ)"},
        {"char": "Ñ", "unicode": "U+00D1", "freq": char_counts.get("Ñ", 0), "status": "CRITICAL_MISSING", "vanilla_slot": "None", "remapping_recommendation": "Remap unused French slot 0x81 (Î -> Ñ)"},
        {"char": "¡", "unicode": "U+00A1", "freq": char_counts.get("¡", 0), "status": "CRITICAL_MISSING", "vanilla_slot": "None", "remapping_recommendation": "Remap unused slot 0x83 (Ä -> ¡)"},
        {"char": "¿", "unicode": "U+00BF", "freq": char_counts.get("¿", 0), "status": "CRITICAL_MISSING", "vanilla_slot": "None", "remapping_recommendation": "Remap unused slot 0x85 (È -> ¿)"},
        {"char": "í", "unicode": "U+00ED", "freq": char_counts.get("í", 0), "status": "ACCENT_MISMATCH", "vanilla_slot": "0x91 (Mismapped to á)", "remapping_recommendation": "Remap unused slot 0x99 (ï -> í)"},
        {"char": "ó", "unicode": "U+00F3", "freq": char_counts.get("ó", 0), "status": "ACCENT_MISMATCH", "vanilla_slot": "0x9A (Renders as circumflex ô)", "remapping_recommendation": "Remap unused slot 0x9A texture or add dedicated ó glyph"},
        {"char": "ú", "unicode": "U+00FA", "freq": char_counts.get("ú", 0), "status": "ACCENT_MISMATCH", "vanilla_slot": "0x9C (Renders as grave ù)", "remapping_recommendation": "Remap unused slot 0x9D (û -> ú)"},
        {"char": "Á", "unicode": "U+00C1", "freq": char_counts.get("Á", 0), "status": "MISSING_CAPITAL", "vanilla_slot": "0x80 (Renders as À)", "remapping_recommendation": "Remap 0x80 texture (À -> Á)"},
        {"char": "Í", "unicode": "U+00CD", "freq": char_counts.get("Í", 0), "status": "MISSING_CAPITAL", "vanilla_slot": "None", "remapping_recommendation": "Remap unused slot 0x89 (Ï -> Í)"},
        {"char": "Ó", "unicode": "U+00D3", "freq": char_counts.get("Ó", 0), "status": "MISSING_CAPITAL", "vanilla_slot": "None", "remapping_recommendation": "Remap unused slot 0x8A (Ô -> Ó)"},
        {"char": "Ú", "unicode": "U+00DA", "freq": char_counts.get("Ú", 0), "status": "MISSING_CAPITAL", "vanilla_slot": "None", "remapping_recommendation": "Remap unused slot 0x8C (Ù -> Ú)"},
    ]

    missing_csv_path = FONT_DIR / "MISSING_SPANISH_GLYPHS.csv"
    with open(missing_csv_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=["char", "unicode", "freq", "status", "vanilla_slot", "remapping_recommendation"])
        writer.writeheader()
        writer.writerows(missing_glyphs_data)

    print("Font analysis files generated successfully:")
    print(f"  {char_set_path}")
    print(f"  {freq_csv_path}")
    print(f"  {missing_csv_path}")

if __name__ == "__main__":
    run_font_analysis()
