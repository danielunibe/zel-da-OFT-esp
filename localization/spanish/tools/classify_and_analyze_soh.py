#!/usr/bin/env python3
"""
classify_and_analyze_soh.py — Deep static analysis & classification of soh_ui_strings_eng.jsonl.
Reads C++ sources directly to distinguish player-facing text from internal IDs, CVars, paths, etc.
"""

import json
import os
import re
from pathlib import Path
from collections import Counter

DEV_ROOT = Path(r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV")
SOURCE_ROOT = DEV_ROOT / "source" / "shipwright"
LOCALIZATION_ROOT = DEV_ROOT / "localization_workspace"
SPANISH_ROOT = LOCALIZATION_ROOT / "spanish"

ENG_JSONL = LOCALIZATION_ROOT / "corpus" / "soh_ui_strings_eng.jsonl"

def analyze():
    with open(ENG_JSONL, 'r', encoding='utf-8') as f:
        entries = [json.loads(line) for line in f if line.strip()]

    print(f"Total raw UI entries: {len(entries)}")

    # Cache source files
    src_cache = {}
    for sf in set(e['source_file'] for e in entries):
        if sf.endswith('.cpp') or sf.endswith('.h'):
            fp = SOURCE_ROOT / sf
            if fp.exists():
                with open(fp, 'r', encoding='utf-8', errors='ignore') as sfp:
                    src_cache[sf] = sfp.readlines()

    classifications = []
    
    for e in entries:
        sf = e.get('source_file', '')
        line_no = e.get('source_line_start')
        text = e.get('plain_text', '')
        raw_text = e.get('raw_text', '')

        cls = "UNKNOWN"
        reason = ""

        # 1. Non-English accessibility
        if "accessibility" in sf and ("_fra.json" in sf or "_ger.json" in sf):
            cls = "FALSE_POSITIVE"
            reason = "Foreign language accessibility file in English corpus"

        # 2. English accessibility
        elif "accessibility" in sf and "_eng.json" in sf:
            cls = "PLAYER_FACING"
            reason = "English accessibility TTS/screen-reader string"

        # 3. DevTools / Debug
        elif "SohMenuDevTools" in sf or "debug" in sf.lower():
            cls = "DEBUG_ONLY"
            reason = "DevTools menu string"

        # 4. Format strings / byte sequences
        elif "\\0" in text or "\\0" in raw_text or text.startswith("%") or re.match(r"^%[-+0-9.*]*[sdf]$", text):
            cls = "FORMAT_STRING"
            reason = "C printf format string or null-delimited token"

        # 5. File paths
        elif "/" in text or text.endswith(".h") or text.endswith(".cpp") or text.endswith(".png") or text.endswith(".o2r"):
            cls = "FILE_PATH"
            reason = "File path or code include"

        # 6. Check C++ source line if available
        elif sf in src_cache and line_no and line_no <= len(src_cache[sf]):
            line_str = src_cache[sf][line_no - 1].strip()
            
            # CVar registration
            if f'CVar("{text}")' in line_str or f'CVAR_ENHANCEMENT("{text}")' in line_str or f'CVAR_SETTING("{text}")' in line_str or f'CVAR_GENERAL("{text}")' in line_str or f'CVAR_CHEAT("{text}")' in line_str or f'CVar_Get' in line_str or f'CVar_Set' in line_str:
                cls = "INTERNAL_IDENTIFIER"
                reason = "CVar registration / access identifier"
            
            # Cosmetic option macro: COSMETIC_OPTION("CVar.Key", "User Label", ...)
            elif "COSMETIC_OPTION" in line_str:
                # Find all quoted strings in line
                quotes = re.findall(r'"([^"]*)"', line_str)
                if quotes and quotes[0] == text and len(quotes) > 1 and quotes[1] != text:
                    cls = "INTERNAL_IDENTIFIER"
                    reason = "Cosmetic option internal key"
                elif quotes and len(quotes) > 1 and quotes[1] == text:
                    cls = "PLAYER_FACING"
                    reason = "Cosmetic option UI label"
                elif "." in text:
                    cls = "INTERNAL_IDENTIFIER"
                    reason = "Cosmetic dotted key"
                else:
                    cls = "PLAYER_FACING"
                    reason = "Cosmetic UI label"

            # AddWidget(path, "Label", ...).CVar(...)
            elif "AddWidget" in line_str:
                quotes = re.findall(r'"([^"]*)"', line_str)
                if quotes and quotes[0] == text:
                    cls = "PLAYER_FACING"
                    reason = "AddWidget UI label"
                elif any(f'CVar' in q for q in quotes) or "." in text:
                    cls = "INTERNAL_IDENTIFIER"
                    reason = "Widget CVar"
                else:
                    cls = "PLAYER_FACING"
                    reason = "Widget parameter"

            # Dotted identifier
            elif "." in text:
                cls = "INTERNAL_IDENTIFIER"
                reason = "Dotted internal identifier"

            # Tooltip
            elif "Tooltip(" in line_str:
                cls = "PLAYER_FACING"
                reason = "UI Tooltip text"

            # ImGui or UI rendering
            elif any(ui_fn in line_str for ui_fn in ["ImGui::", "Text(", "Button(", "Checkbox(", "Combo(", "MenuItem("]):
                cls = "PLAYER_FACING"
                reason = "ImGui UI element"

            else:
                # General heuristics for C++ strings
                if "." in text:
                    cls = "INTERNAL_IDENTIFIER"
                    reason = "Dotted string"
                elif text in ["Small", "Normal", "Large", "Vanilla", "Disabled", "Both", "Always", "Never", "Once", "OHKO", "None", "Cancel", "Start", "Close", "Stop", "Test", "Custom", "Source"]:
                    cls = "PLAYER_FACING"
                    reason = "Common UI keyword/label"
                elif " " in text:
                    cls = "PLAYER_FACING"
                    reason = "Natural text with spaces"
                elif text.startswith("gSaveContext") or text.startswith("CVAR"):
                    cls = "INTERNAL_IDENTIFIER"
                    reason = "C++ variable/macro"
                else:
                    # Look at capitalization and common symbols
                    if any(c.isupper() for c in text[1:]) and not any(sep in text for sep in ["-", " "]):
                        cls = "INTERNAL_IDENTIFIER"
                        reason = "CamelCase internal identifier"
                    else:
                        cls = "PLAYER_FACING"
                        reason = "UI text"

        else:
            if "." in text:
                cls = "INTERNAL_IDENTIFIER"
                reason = "Dotted string"
            else:
                cls = "PLAYER_FACING"
                reason = "Default fallback"

        classifications.append((e, cls, reason))

    counts = Counter(c for _, c, _ in classifications)
    print("\nClassification breakdown:")
    for k, v in counts.most_common():
        print(f"  {k}: {v}")

if __name__ == "__main__":
    analyze()
