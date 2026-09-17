#!/usr/bin/env python3
"""
Localization Corpus Extraction Tool — Ship of Harkinian 9.1.1
READ-ONLY source analysis for Couch Edition localization.

Extracts all player-facing text from:
1. Vanilla OoT messages (from O2R archive)
2. Custom messages (from C++ source)
3. SoH UI strings (from C++ source)
4. Randomizer strings (from C++ source)
5. Accessibility texts (from JSON files)

Usage:
    python extract_localization_corpus.py --source-root <PATH> --o2r-root <PATH> --output-root <PATH>
"""

import argparse
import csv
import hashlib
import json
import os
import re
import struct
import sys
import zipfile
from collections import defaultdict
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Optional


# ============================================================================
# DATA STRUCTURES
# ============================================================================

@dataclass
class LocalizationEntry:
    id: str
    id_numeric: Optional[int] = None
    source_language: str = "ENG"
    source_file: str = ""
    source_line_start: Optional[int] = None
    source_line_end: Optional[int] = None
    system: str = "OTHER"
    category: str = "OTHER_PLAYER_VISIBLE"
    subcategory: str = ""
    speaker_hint: str = "UNKNOWN"
    context_hint: str = ""
    raw_text: str = ""
    plain_text: str = ""
    control_codes: list = field(default_factory=list)
    variables: list = field(default_factory=list)
    button_glyphs: list = field(default_factory=list)
    choices: list = field(default_factory=list)
    page_count: int = 1
    has_color_codes: bool = False
    has_sound_codes: bool = False
    has_dynamic_values: bool = False
    has_player_name: bool = False
    has_button_prompt: bool = False
    is_duplicate_text: bool = False
    duplicate_group: Optional[str] = None
    translation_status: str = "NOT_TRANSLATED"
    german_text: Optional[str] = None
    french_text: Optional[str] = None
    notes: str = ""

    def to_dict(self):
        return asdict(self)


# ============================================================================
# CONTROL CODE ANALYSIS
# ============================================================================

# OoT control codes (from message_data_fmt.h)
CONTROL_CODE_MAP = {
    0x01: "NEWLINE",
    0x02: "END",
    0x04: "BOX_BREAK",
    0x05: "COLOR",
    0x06: "SHIFT",
    0x07: "TEXTID",
    0x08: "QUICKTEXT_ENABLE",
    0x09: "QUICKTEXT_DISABLE",
    0x0A: "PERSISTENT",
    0x0B: "EVENT",
    0x0C: "BOX_BREAK_DELAYED",
    0x0D: "AWAIT_BUTTON_PRESS",
    0x0E: "FADE",
    0x0F: "NAME",
    0x10: "OCARINA",
    0x11: "FADE2",
    0x12: "SFX",
    0x13: "ITEM_ICON",
    0x14: "TEXT_SPEED",
    0x15: "BACKGROUND",
    0x16: "MARATHON_TIME",
    0x17: "RACE_TIME",
    0x18: "POINTS",
    0x19: "TOKENS",
    0x1A: "UNSKIPPABLE",
    0x1B: "TWO_CHOICE",
    0x1C: "THREE_CHOICE",
    0x1D: "FISH_INFO",
    0x1E: "HIGHSCORE",
    0x1F: "TIME",
}

COLOR_NAMES = {
    0: "DEFAULT", 1: "RED", 2: "GREEN", 3: "BLUE",
    4: "LIGHTBLUE", 5: "PURPLE", 6: "YELLOW", 7: "BLACK"
}

# Custom message format codes (%-based)
CUSTOM_COLOR_MAP = {
    "%w": "WHITE", "%r": "RED", "%g": "GREEN", "%b": "BLUE",
    "%c": "LIGHTBLUE", "%p": "PINK", "%y": "YELLOW", "%B": "BLACK",
}

BUTTON_GLYPH_NAMES = [
    "A", "B", "C-Up", "C-Down", "C-Left", "C-Right",
    "L", "R", "Z", "Start", "Control Stick", "D-Pad"
]

# OoT font button glyph byte values (0x9F-0xAB range)
OOT_BUTTON_GLYPH_BYTES = {
    0x9F: "A", 0xA0: "B", 0xA1: "C",
    0xA2: "C-Up", 0xA3: "C-Down", 0xA4: "C-Left", 0xA5: "C-Right",
    0xA6: "L", 0xA7: "R", 0xA8: "Z",
    0xA9: "Control Stick", 0xAA: "D-Pad", 0xAB: "Start",
}


def analyze_raw_text(raw_text: str) -> dict:
    """Analyze raw text and return metadata about control codes, variables, etc."""
    control_codes = []
    variables = []
    button_glyphs = []
    choices = []
    has_color = False
    has_sound = False
    has_player_name = False
    has_button_prompt = False
    page_count = 1

    i = 0
    while i < len(raw_text):
        ch = raw_text[i]
        code = ord(ch)

        # OoT button glyph bytes (0x9F-0xAB range)
        if code in OOT_BUTTON_GLYPH_BYTES:
            glyph = OOT_BUTTON_GLYPH_BYTES[code]
            if glyph not in button_glyphs:
                button_glyphs.append(glyph)
            has_button_prompt = True
            i += 1
            continue

        # OoT control codes
        if code in CONTROL_CODE_MAP:
            name = CONTROL_CODE_MAP[code]
            if name not in control_codes:
                control_codes.append(name)
            if name == "COLOR" and i + 1 < len(raw_text):
                color_code = ord(raw_text[i + 1])
                color_name = COLOR_NAMES.get(color_code, f"UNKNOWN_{color_code}")
                if f"COLOR({color_name})" not in control_codes:
                    control_codes.append(f"COLOR({color_name})")
                has_color = True
                i += 2
                continue
            elif name == "SFX":
                has_sound = True
                i += 3  # skip 2 param bytes
                continue
            elif name in ("FADE", "FADE2", "TEXT_SPEED", "BOX_BREAK_DELAYED", "BACKGROUND",
                          "SHIFT", "TEXTID", "POINTS", "TOKENS", "HIGHSCORE"):
                # Skip parameter bytes
                if name in ("FADE2", "SFX", "BACKGROUND", "TEXTID"):
                    i += 3
                else:
                    i += 2
                continue
            elif name == "NEWLINE":
                i += 1
                continue
            elif name == "END":
                i += 1
                continue
            elif name == "NAME":
                has_player_name = True
                i += 1
                continue
            elif name in ("TWO_CHOICE", "THREE_CHOICE"):
                choices.append("CHOICE_MARKER")
                i += 1
                continue
            else:
                i += 1
                continue

        # Custom message format: & = newline, ^ = page break, %x = color
        if ch == '&':
            pass  # newline in custom format
        elif ch == '^':
            page_count += 1
        elif ch == '%' and i + 1 < len(raw_text):
            next_ch = raw_text[i + 1]
            if next_ch in CUSTOM_COLOR_MAP:
                has_color = True
                color_name = CUSTOM_COLOR_MAP[next_ch]
                if f"CUSTOM_COLOR({color_name})" not in control_codes:
                    control_codes.append(f"CUSTOM_COLOR({color_name})")
                i += 2
                continue

        # Template variables [[varname]]
        if ch == '[' and i + 1 < len(raw_text) and raw_text[i + 1] == '[':
            end = raw_text.find(']]', i + 2)
            if end != -1:
                var_name = raw_text[i + 2:end]
                if var_name not in variables:
                    variables.append(var_name)
                i = end + 2
                continue

        # Button glyphs in custom format ($icon)
        if ch == '$' and i + 1 < len(raw_text):
            icon_char = raw_text[i + 1]
            if icon_char in ('a', 'b', 'c', 'l', 'r', 'z'):
                glyph = icon_char.upper()
                if glyph not in button_glyphs:
                    button_glyphs.append(glyph)
                has_button_prompt = True
                i += 2
                continue

        i += 1

    return {
        "control_codes": control_codes,
        "variables": variables,
        "button_glyphs": button_glyphs,
        "choices": choices,
        "page_count": page_count,
        "has_color_codes": has_color,
        "has_sound_codes": has_sound,
        "has_dynamic_values": len(variables) > 0,
        "has_player_name": has_player_name,
        "has_button_prompt": has_button_prompt or len(button_glyphs) > 0,
    }


def raw_to_plain(raw_text: str) -> str:
    """Convert raw OoT message text to human-readable plain text."""
    result = []
    i = 0
    while i < len(raw_text):
        ch = raw_text[i]
        code = ord(ch)

        # OoT button glyph bytes (0x9F-0xAB range)
        if code in OOT_BUTTON_GLYPH_BYTES:
            glyph = OOT_BUTTON_GLYPH_BYTES[code]
            result.append(f"[{glyph}]")
            i += 1
            continue

        if code in CONTROL_CODE_MAP:
            name = CONTROL_CODE_MAP[code]
            if name == "END":
                break
            elif name == "NEWLINE":
                result.append("\n")
                i += 1
                continue
            elif name == "COLOR":
                i += 2  # skip color byte
                continue
            elif name == "SHIFT":
                i += 2
                continue
            elif name in ("FADE2", "SFX", "BACKGROUND", "TEXTID"):
                i += 3
                continue
            elif name in ("FADE", "TEXT_SPEED", "BOX_BREAK_DELAYED", "POINTS",
                          "TOKENS", "HIGHSCORE", "ITEM_ICON"):
                i += 2
                continue
            elif name == "NAME":
                result.append("[PLAYER]")
                i += 1
                continue
            elif name == "TWO_CHOICE":
                result.append("[CHOICE:2]")
                i += 1
                continue
            elif name == "THREE_CHOICE":
                result.append("[CHOICE:3]")
                i += 1
                continue
            elif name == "BOX_BREAK":
                result.append("\n--- PAGE ---\n")
                i += 1
                continue
            elif name == "AWAIT_BUTTON_PRESS":
                result.append("[WAIT]")
                i += 1
                continue
            else:
                i += 1
                continue

        # Custom message format
        if ch == '&':
            result.append("\n")
        elif ch == '^':
            result.append("\n--- PAGE ---\n")
        elif ch == '%' and i + 1 < len(raw_text):
            next_ch = raw_text[i + 1]
            if next_ch in CUSTOM_COLOR_MAP:
                i += 2
                continue
        elif ch == '#' and i + 1 < len(raw_text) and raw_text[i + 1] == '#':
            # ## = color placeholder in custom messages
            i += 2
            continue
        elif ch == '[' and i + 1 < len(raw_text) and raw_text[i + 1] == '[':
            end = raw_text.find(']]', i + 2)
            if end != -1:
                var_name = raw_text[i + 2:end]
                result.append(f"[{var_name}]")
                i = end + 2
                continue
        elif ch == '$' and i + 1 < len(raw_text):
            icon_char = raw_text[i + 1]
            if icon_char in ('0', '1', '2', '3', '4', '5', '6', '7', '8',
                             'l', 'b', 'o', 'c', 'i', 'L', 'k', 'm', 'C', 's', 'g'):
                result.append(f"[ICON:{icon_char}]")
                i += 2
                continue
        else:
            result.append(ch)

        i += 1

    return "".join(result).strip()


# ============================================================================
# O2R BINARY TEXT PARSER
# ============================================================================

def parse_o2r_text_data(data: bytes) -> list:
    """Parse binary text data from O2R archive.
    
    O2R resource format: 68-byte header, then:
        uint32 count, then for each message:
        uint16 id, uint8 textboxType, uint8 textboxYPos, uint32 msgLength, char[msgLength]
    
    The header contains: padding, "TXTO" magic, padding, 0xDEADBEEF markers, zeros, then count.
    """
    messages = []
    if len(data) < 0x48:  # Minimum: 68-byte header + 4-byte count + 8 bytes per msg
        return messages

    # Skip the 68-byte O2R resource header
    header_offset = 0x44  # 68 bytes
    count = struct.unpack('<I', data[header_offset:header_offset + 4])[0]
    offset = header_offset + 4

    if count > 10000 or count == 0:
        # Try alternate header sizes
        for alt_offset in [0, 4, 8, 12, 16, 32, 64]:
            if alt_offset + 4 <= len(data):
                alt_count = struct.unpack('<I', data[alt_offset:alt_offset + 4])[0]
                if 100 < alt_count < 10000:
                    header_offset = alt_offset
                    count = alt_count
                    offset = alt_offset + 4
                    break

    for _ in range(count):
        if offset + 8 > len(data):
            break
        msg_id = struct.unpack('<H', data[offset:offset + 2])[0]
        textbox_type = data[offset + 2]
        textbox_ypos = data[offset + 3]
        msg_length = struct.unpack('<I', data[offset + 4:offset + 8])[0]
        offset += 8

        if msg_length > 10000 or msg_length < 0:
            break
        if offset + msg_length > len(data):
            break
        msg_bytes = data[offset:offset + msg_length]
        offset += msg_length

        # Try to decode as UTF-8, fallback to latin-1
        try:
            msg_str = msg_bytes.decode('utf-8')
        except UnicodeDecodeError:
            msg_str = msg_bytes.decode('latin-1')

        messages.append({
            "id": msg_id,
            "textboxType": textbox_type,
            "textboxYPos": textbox_ypos,
            "msg": msg_str,
        })

    return messages


def extract_vanilla_messages(o2r_path: str) -> dict:
    """Extract all vanilla messages from O2R archive."""
    result = {}
    if not os.path.exists(o2r_path):
        print(f"  WARNING: O2R not found: {o2r_path}")
        return result

    with zipfile.ZipFile(o2r_path, 'r') as z:
        text_entries = [n for n in z.namelist() if n.startswith('text/') and 'message' in n]
        for entry in text_entries:
            data = z.read(entry)
            messages = parse_o2r_text_data(data)
            lang_key = entry.split('/')[-1]
            result[lang_key] = messages
            print(f"  Parsed {entry}: {len(messages)} messages")

    return result


# ============================================================================
# SOURCE CODE PARSERS
# ============================================================================

def extract_custom_messages_from_source(source_root: str) -> list:
    """Extract custom messages from z_message_OTR.cpp and randomizer.cpp."""
    entries = []

    # Parse z_message_OTR.cpp
    otr_path = os.path.join(source_root, "soh", "soh", "z_message_OTR.cpp")
    if os.path.exists(otr_path):
        entries.extend(_parse_z_message_otr(otr_path))

    # Parse randomizer.cpp CreateCustomMessages section
    rando_path = os.path.join(source_root, "soh", "soh", "Enhancements", "randomizer", "randomizer.cpp")
    if os.path.exists(rando_path):
        entries.extend(_parse_randomizer_custom_messages(rando_path))

    return entries


def _parse_z_message_otr(filepath: str) -> list:
    """Parse custom messages from z_message_OTR.cpp."""
    entries = []
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()

    # Pattern for CustomMessage("eng", "ger", "fra", ...)
    pattern = re.compile(
        r'CustomMessage\(\s*'
        r'(?:STRINGIFY\()?"((?:[^"\\]|\\.)*)"\s*\)'  # english - may have STRINGIFY
        r'|'
        r'CustomMessage\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"'  # eng, ger, fra
        r'|'
        r'CustomMessage\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*TEXTBOX_TYPE',  # eng only with type
    )

    # Simpler approach: find all CustomMessage constructors
    cm_pattern = re.compile(r'CustomMessage\(')
    for match in cm_pattern.finditer(content):
        start = match.start()
        line_num = content[:start].count('\n') + 1

        # Extract the full constructor call
        depth = 0
        end = start
        for i in range(start, len(content)):
            if content[i] == '(':
                depth += 1
            elif content[i] == ')':
                depth -= 1
                if depth == 0:
                    end = i + 1
                    break

        call_text = content[start:end]

        # Extract string literals
        strings = re.findall(r'"((?:[^"\\]|\\.)*)"', call_text)
        if len(strings) >= 1:
            eng = strings[0] if len(strings) >= 1 else ""
            ger = strings[1] if len(strings) >= 2 else None
            fra = strings[2] if len(strings) >= 3 else None

            if eng:
                entries.append({
                    "id": f"Z_MESSAGE_OTR::L{line_num}",
                    "id_numeric": None,
                    "source_file": "soh/soh/z_message_OTR.cpp",
                    "source_line_start": line_num,
                    "english": eng,
                    "german": ger,
                    "french": fra,
                    "system": "CUSTOM_MESSAGE",
                })

    return entries


def _parse_randomizer_custom_messages(filepath: str) -> list:
    """Parse GIMESSAGE entries from randomizer.cpp."""
    entries = []
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()

    content = ''.join(lines)

    # Find GIMESSAGE entries
    gi_pattern = re.compile(r'GIMESSAGE\(\s*(\w+)\s*,\s*(\w+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)')
    for match in gi_pattern.finditer(content):
        giid = match.group(1)
        iid = match.group(2)
        eng = match.group(3)
        ger = match.group(4)
        fra = match.group(5)
        line_num = content[:match.start()].count('\n') + 1

        entries.append({
            "id": f"Rando::GIMESSAGE::{giid}",
            "id_numeric": None,
            "source_file": "soh/soh/Enhancements/randomizer/randomizer.cpp",
            "source_line_start": line_num,
            "english": eng,
            "german": ger,
            "french": fra,
            "system": "RANDOMIZER_ITEM",
        })

    # Find CustomMessage("eng", "ger", "fra" in randomizer.cpp
    cm_pattern = re.compile(r'CustomMessage\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*[,\)]')
    for match in cm_pattern.finditer(content):
        eng = match.group(1)
        ger = match.group(2)
        fra = match.group(3)
        line_num = content[:match.start()].count('\n') + 1

        # Skip if it's a GIMESSAGE (already captured)
        before = content[max(0, match.start() - 20):match.start()]
        if 'GIMESSAGE' in before:
            continue

        entries.append({
            "id": f"Rando::CUSTOM_MSG::L{line_num}",
            "id_numeric": None,
            "source_file": "soh/soh/Enhancements/randomizer/randomizer.cpp",
            "source_line_start": line_num,
            "english": eng,
            "german": ger,
            "french": fra,
            "system": "RANDOMIZER_NPC_DIALOG",
        })

    return entries


def extract_hint_texts(source_root: str) -> list:
    """Extract hint text entries from randomizer hint list files."""
    entries = []
    hint_dir = os.path.join(source_root, "soh", "soh", "Enhancements", "randomizer", "3drando", "hint_list")

    if not os.path.exists(hint_dir):
        return entries

    for filename in os.listdir(hint_dir):
        if not filename.endswith('.cpp'):
            continue
        filepath = os.path.join(hint_dir, filename)
        entries.extend(_parse_hint_list_file(filepath, filename))

    # Also parse hint_list.cpp in parent directory
    hint_list_cpp = os.path.join(source_root, "soh", "soh", "Enhancements", "randomizer", "3drando", "hint_list.cpp")
    if os.path.exists(hint_list_cpp):
        entries.extend(_parse_hint_list_file(hint_list_cpp, "hint_list.cpp"))

    return entries


def _parse_hint_list_file(filepath: str, filename: str) -> list:
    """Parse a hint list file for CustomMessage entries."""
    entries = []
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()

    # Remove C++ comments (/* german */, /* french */, // comments)
    # But preserve string contents - only remove comments outside strings
    cleaned = re.sub(r'/\*[^*]*\*/', '', content)
    cleaned = re.sub(r'//.*?$', '', cleaned, flags=re.MULTILINE)

    # Find all CustomMessage("eng", "ger", "fra") patterns
    cm_pattern = re.compile(
        r'CustomMessage\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*[,)]'
    )

    # Also find hintTextTable keys for context
    ht_pattern = re.compile(r'hintTextTable\[(\w+)\]\s*=\s*HintText\(')

    for match in cm_pattern.finditer(cleaned):
        eng = match.group(1)
        ger = match.group(2)
        fra = match.group(3)
        line_num = content[:match.start()].count('\n') + 1

        # Find the closest hintTextTable key
        ht_key = "UNKNOWN"
        for ht_match in ht_pattern.finditer(cleaned):
            if ht_match.start() < match.start():
                ht_key = ht_match.group(1)
            else:
                break

        entries.append({
            "id": f"Hint::{ht_key}::{len(entries)}",
            "id_numeric": None,
            "source_file": f"soh/soh/Enhancements/randomizer/3drando/{filename}",
            "source_line_start": line_num,
            "english": eng,
            "german": ger,
            "french": fra,
            "system": "RANDOMIZER_HINT",
            "subcategory": ht_key,
        })

    return entries


def extract_item_list(source_root: str) -> list:
    """Extract item names from randomizer item_list.cpp."""
    entries = []
    filepath = os.path.join(source_root, "soh", "soh", "Enhancements", "randomizer", "item_list.cpp")
    if not os.path.exists(filepath):
        return entries

    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()

    # Remove C++ comments
    cleaned = re.sub(r'/\*[^*]*\*/', '', content)
    cleaned = re.sub(r'//.*?$', '', cleaned, flags=re.MULTILINE)

    # Find Text{ "eng", "fra", "ger" } entries
    # Note: order is eng, fra, ger in the Text constructor
    text_pattern = re.compile(r'Text\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')
    for match in text_pattern.finditer(cleaned):
        eng = match.group(1)
        fra = match.group(2)
        ger = match.group(3)
        line_num = content[:match.start()].count('\n') + 1

        # Find the RG_ enum name for context
        preceding = content[max(0, match.start() - 200):match.start()]
        rg_match = re.search(r'itemTable\[(\w+)\]', preceding)
        rg_name = rg_match.group(1) if rg_match else f"L{line_num}"

        entries.append({
            "id": f"Rando::ITEM::{rg_name}",
            "id_numeric": None,
            "source_file": "soh/soh/Enhancements/randomizer/item_list.cpp",
            "source_line_start": line_num,
            "english": eng,
            "german": ger,
            "french": fra,
            "system": "RANDOMIZER_ITEM",
            "category": "ITEM_NAME",
            "subcategory": rg_name,
        })

    return entries


def extract_soh_ui_strings(source_root: str) -> list:
    """Extract hardcoded UI strings from SoH menu/enhancement files."""
    entries = []

    ui_files = [
        ("soh/soh/SohGui/SohMenuSettings.cpp", "SOH_MENU", "CONFIGURATION"),
        ("soh/soh/SohGui/SohMenuEnhancements.cpp", "SOH_ENHANCEMENT", "ENHANCEMENT_MENU"),
        ("soh/soh/SohGui/SohMenuRandomizer.cpp", "SOH_ENHANCEMENT", "RANDOMIZER"),
        ("soh/soh/SohGui/SohMenuDevTools.cpp", "SOH_DEVTOOLS", "SYSTEM"),
        ("soh/soh/SohGui/SohMenuNetwork.cpp", "SOH_NETWORK", "SYSTEM"),
        ("soh/soh/SohGui/SohMenu.cpp", "SOH_MENU", "SYSTEM"),
        ("soh/soh/SohGui/SohMenuBar.cpp", "SOH_MENU", "SYSTEM"),
        ("soh/soh/SohGui/ResolutionEditor.cpp", "SOH_MENU", "CONFIGURATION"),
        ("soh/soh/Enhancements/mod_menu.cpp", "SOH_MOD_MENU", "MOD_MENU"),
        ("soh/soh/Enhancements/cosmetics/CosmeticsEditor.cpp", "SOH_COSMETICS", "ENHANCEMENT_MENU"),
        ("soh/soh/Enhancements/audio/AudioEditor.cpp", "SOH_AUDIO", "ENHANCEMENT_MENU"),
        ("soh/soh/Enhancements/controls/SohInputEditorWindow.cpp", "SOH_MENU", "CONTROLLER"),
        ("soh/soh/Enhancements/randomizer/randomizer_check_tracker.cpp", "SOH_ENHANCEMENT", "RANDOMIZER"),
        ("soh/soh/Enhancements/randomizer/randomizer_entrance_tracker.cpp", "SOH_ENHANCEMENT", "RANDOMIZER"),
        ("soh/soh/Enhancements/randomizer/randomizer_item_tracker.cpp", "SOH_ENHANCEMENT", "RANDOMIZER"),
        ("soh/soh/Enhancements/randomizer/option_descriptions.cpp", "SOH_ENHANCEMENT", "RANDOMIZER"),
        ("soh/soh/Enhancements/randomizer/randomizer.cpp", "SOH_ENHANCEMENT", "RANDOMIZER"),
    ]

    for rel_path, system, category in ui_files:
        filepath = os.path.join(source_root, rel_path)
        if os.path.exists(filepath):
            entries.extend(_extract_string_literals(filepath, rel_path, system, category))

    return entries


def _extract_string_literals(filepath: str, rel_path: str, system: str, category: str) -> list:
    """Extract string literals from a C++ file that are likely UI-facing."""
    entries = []
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()

    # Skip files that are also parsed for custom messages
    if 'randomizer.cpp' in rel_path:
        return entries  # Handled separately

    # Look for AddWidget, Tooltip, and string literal patterns
    in_string_region = False
    for i, line in enumerate(lines):
        stripped = line.strip()

        # Skip comments
        if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
            continue

        # Look for string literals passed to UI functions
        # Common patterns: AddWidget(..., "text"), ImGui::Text("text"), etc.
        string_matches = re.findall(r'"([A-Z][^"]{3,80})"', line)
        for s in string_matches:
            # Filter out includes, paths, and technical strings
            if any(skip in s.lower() for skip in ['#include', 'http', 'cvar', 'cv-', ' ',
                                                    'png', 'jpg', '.cpp', '.h', 'soh/', 'lib/']):
                continue
            if len(s) < 4 or len(s) > 100:
                continue

            entry_id = f"SOH_UI::{rel_path.replace('/', '::').replace('.', '_')}::L{i + 1}"
            entries.append({
                "id": entry_id,
                "id_numeric": None,
                "source_file": rel_path,
                "source_line_start": i + 1,
                "english": s,
                "german": None,
                "french": None,
                "system": system,
                "category": category,
            })

    return entries


def extract_accessibility_texts(o2r_path: str) -> list:
    """Extract texts from accessibility JSON files in soh.o2r."""
    entries = []
    soh_o2r = os.path.join(os.path.dirname(o2r_path), "soh.o2r")

    if not os.path.exists(soh_o2r):
        return entries

    with zipfile.ZipFile(soh_o2r, 'r') as z:
        json_entries = [n for n in z.namelist() if n.startswith('accessibility/texts/') and n.endswith('.json')]
        for json_path in json_entries:
            raw = z.read(json_path).decode('utf-8')
            # Remove JavaScript-style comments (// ...) before parsing JSON
            cleaned = re.sub(r'//.*?$', '', raw, flags=re.MULTILINE)
            try:
                data = json.loads(cleaned)
            except json.JSONDecodeError:
                continue
            lang = "ENG"
            if '_fra' in json_path:
                lang = "FRA"
            elif '_ger' in json_path:
                lang = "GER"

            for key, value in data.items():
                if value and isinstance(value, str) and len(value) > 1:
                    entries.append({
                        "id": f"ACCESSIBILITY::{os.path.basename(json_path).replace('.json', '')}::{key}",
                        "id_numeric": None,
                        "source_file": json_path,
                        "source_line_start": None,
                        "english": value if lang == "ENG" else None,
                        "german": value if lang == "GER" else None,
                        "french": value if lang == "FRA" else None,
                        "system": "ACCESSIBILITY",
                        "category": "SYSTEM",
                        "subcategory": key,
                    })

    return entries


# ============================================================================
# ENTRANCE TRACKER NAMES
# ============================================================================

def extract_entrance_names(source_root: str) -> list:
    """Extract entrance/area names from entrance tracker."""
    entries = []
    filepath = os.path.join(source_root, "soh", "soh", "Enhancements", "randomizer",
                            "randomizer_entrance_tracker.cpp")
    if not os.path.exists(filepath):
        return entries

    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()

    # Find quoted strings that look like area/entrance names
    name_pattern = re.compile(r'"([A-Z][^"]{2,60})"')
    for match in name_pattern.finditer(content):
        name = match.group(1)
        line_num = content[:match.start()].count('\n') + 1

        # Filter out UI labels
        if any(skip in name.lower() for skip in ['sort by', 'list items', 'group by',
                                                    'window', 'spoiler', 'filter', 'show',
                                                    'enable', 'disable', 'type', 'one way',
                                                    'overworld', 'interior', 'grotto', 'dungeon']):
            continue

        entries.append({
            "id": f"ENTRANCE::L{line_num}",
            "id_numeric": None,
            "source_file": "soh/soh/Enhancements/randomizer/randomizer_entrance_tracker.cpp",
            "source_line_start": line_num,
            "english": name,
            "system": "RANDOMIZER_LOCATION",
            "category": "LOCATION_NAME",
        })

    return entries


def extract_location_names(source_root: str) -> list:
    """Extract location names from location_list.cpp (RC_ enum names as human-readable)."""
    entries = []
    filepath = os.path.join(source_root, "soh", "soh", "Enhancements", "randomizer", "location_list.cpp")
    if not os.path.exists(filepath):
        return entries

    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()

    # The location table uses RC_ enum names. Extract them as location identifiers.
    # The actual display names come from the check tracker.
    loc_pattern = re.compile(r'locationTable\[(RC_\w+)\]')
    for match in loc_pattern.finditer(content):
        rc_name = match.group(1)
        line_num = content[:match.start()].count('\n') + 1

        # Convert RC_KF_SHOP_ITEM_1 to "KF Shop Item 1" as a rough human-readable name
        readable = rc_name.replace('RC_', '').replace('_', ' ').title()

        entries.append({
            "id": f"LOCATION::{rc_name}",
            "id_numeric": None,
            "source_file": "soh/soh/Enhancements/randomizer/location_list.cpp",
            "source_line_start": line_num,
            "english": readable,
            "system": "RANDOMIZER_LOCATION",
            "category": "LOCATION_NAME",
            "subcategory": rc_name,
        })

    return entries


def extract_area_names(source_root: str) -> list:
    """Extract area names from randomizer_check_objects.cpp."""
    entries = []
    filepath = os.path.join(source_root, "soh", "soh", "Enhancements", "randomizer",
                            "randomizer_check_objects.cpp")
    if not os.path.exists(filepath):
        return entries

    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()

    # Find rcAreaNames entries: { RCAREA_ENUM, "Name" }
    area_pattern = re.compile(r'\{\s*RCAREA_(\w+)\s*,\s*"([^"]+)"\s*\}')
    for match in area_pattern.finditer(content):
        area_enum = match.group(1)
        area_name = match.group(2)
        line_num = content[:match.start()].count('\n') + 1

        entries.append({
            "id": f"AREA::RCAREA_{area_enum}",
            "id_numeric": None,
            "source_file": "soh/soh/Enhancements/randomizer/randomizer_check_objects.cpp",
            "source_line_start": line_num,
            "english": area_name,
            "system": "RANDOMIZER_LOCATION",
            "category": "LOCATION_NAME",
        })

    return entries


# ============================================================================
# ICE TRAP MESSAGES
# ============================================================================

def extract_ice_trap_messages(source_root: str) -> list:
    """Extract ice trap joke messages from randomizer.cpp."""
    entries = []
    filepath = os.path.join(source_root, "soh", "soh", "Enhancements", "randomizer", "randomizer.cpp")
    if not os.path.exists(filepath):
        return entries

    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.readlines()

    in_ice_trap = False
    for i, line in enumerate(lines):
        if 'iceTrapMessages' in line or 'IceTrapMessages' in line:
            in_ice_trap = True
        if in_ice_trap:
            # Find string literals
            match = re.search(r'"([^"]{5,200})"', line)
            if match:
                text = match.group(1)
                # Filter out code-like strings
                if not any(skip in text for skip in ['#include', ' ', 'void', 'int', 'return']):
                    entries.append({
                        "id": f"Rando::ICE_TRAP::L{i + 1}",
                        "id_numeric": None,
                        "source_file": "soh/soh/Enhancements/randomizer/randomizer.cpp",
                        "source_line_start": i + 1,
                        "english": text,
                        "system": "RANDOMIZER_ITEM",
                        "category": "OTHER_PLAYER_VISIBLE",
                        "subcategory": "ice_trap_joke",
                    })
            if line.strip().startswith('};') or line.strip() == '}':
                in_ice_trap = False

    return entries


# ============================================================================
# DUPLICATE DETECTION
# ============================================================================

def detect_duplicates(entries: list) -> dict:
    """Detect entries with identical plain_text. Returns group mapping."""
    text_to_ids = defaultdict(list)
    for entry in entries:
        text_to_ids[entry.plain_text].append(entry.id)

    duplicates = {}
    group_id = 0
    for text, ids in text_to_ids.items():
        if len(ids) > 1:
            group_id += 1
            group_name = f" DUP_GROUP_{group_id:04d}"
            for eid in ids:
                duplicates[eid] = group_name

    return duplicates


# ============================================================================
# MULTILINGUAL ALIGNMENT
# ============================================================================

def build_multilingual_alignment(entries: list) -> list:
    """Build ENG/GER/FRA alignment table for entries with all three languages."""
    alignments = []
    seen = set()

    for entry in entries:
        if entry.german_text or entry.french_text:
            key = entry.id
            if key not in seen:
                seen.add(key)
                alignments.append({
                    "id": entry.id,
                    "eng": entry.raw_text if entry.source_language == "ENG" else None,
                    "ger": entry.german_text,
                    "fra": entry.french_text,
                })

    return alignments


# ============================================================================
# MAIN EXTRACTION PIPELINE
# ============================================================================

def run_extraction(source_root: str, o2r_root: str, output_root: str):
    """Main extraction pipeline."""
    print("=" * 70)
    print("LOCALIZATION CORPUS EXTRACTION — L01")
    print("Ship of Harkinian 9.1.1 / Ocarina of Time")
    print("=" * 70)

    all_entries = []
    all_soh_ui_entries = []
    all_randomizer_entries = []

    # 1. Extract vanilla messages from O2R
    print("\n[1/7] Extracting vanilla messages from O2R...")
    o2r_path = os.path.join(o2r_root, "oot.o2r")
    vanilla_data = extract_vanilla_messages(o2r_path)

    # Process NES (English) messages
    nes_messages = vanilla_data.get("ntsc_nes_message_data_static", vanilla_data.get("nes_message_data_static", []))
    ger_messages = vanilla_data.get("ger_message_data_static", [])
    fra_messages = vanilla_data.get("fra_message_data_static", [])

    print(f"  NES (English): {len(nes_messages)} messages")
    print(f"  German: {len(ger_messages)} messages")
    print(f"  French: {len(fra_messages)} messages")

    # Build GER/FRA lookup by ID
    ger_by_id = {m['id']: m['msg'] for m in ger_messages}
    fra_by_id = {m['id']: m['msg'] for m in fra_messages}

    for msg in nes_messages:
        msg_id = msg['id']
        raw = msg['msg']
        plain = raw_to_plain(raw)
        analysis = analyze_raw_text(raw)

        entry = LocalizationEntry(
            id=f"0x{msg_id:04X}",
            id_numeric=msg_id,
            source_language="ENG",
            source_file="oot.o2r::text/nes_message_data_static",
            system="VANILLA_MESSAGE",
            category="NPC_DIALOGUE",
            raw_text=raw,
            plain_text=plain,
            german_text=ger_by_id.get(msg_id),
            french_text=fra_by_id.get(msg_id),
            **analysis,
        )

        # Infer category from ID range
        if 0x0140 <= msg_id <= 0x015F or msg_id == 0x01B3:
            entry.category = "NAVI"
            entry.speaker_hint = "Navi"
        elif 0x0160 <= msg_id <= 0x016D:
            entry.category = "NPC_DIALOGUE"
            entry.speaker_hint = "Saria"
        elif 0x0200 <= msg_id <= 0x02FF:
            entry.category = "SIGN"
        elif 0x0300 <= msg_id <= 0x0346:
            entry.category = "SIGN"
        elif 0x0500 <= msg_id <= 0x05FF:
            entry.category = "CUTSCENE"
            entry.subcategory = "credits"
        elif 0x088D <= msg_id <= 0x0892:
            entry.category = "SYSTEM"
            entry.subcategory = "warp_song_name"
        elif 0x407B <= msg_id <= 0x40AF:
            entry.category = "MINIGAME"
            entry.subcategory = "fishing"

        all_entries.append(entry)

    # Process staff credits
    staff_messages = vanilla_data.get("staff_message_data_static", [])
    for msg in staff_messages:
        raw = msg['msg']
        plain = raw_to_plain(raw)
        analysis = analyze_raw_text(raw)
        entry = LocalizationEntry(
            id=f"0x{msg['id']:04X}",
            id_numeric=msg['id'],
            source_language="ENG",
            source_file="oot.o2r::text/staff_message_data_static",
            system="VANILLA_MESSAGE",
            category="CUTSCENE",
            subcategory="credits",
            raw_text=raw,
            plain_text=plain,
            **analysis,
        )
        all_entries.append(entry)

    # 2. Extract custom messages from source
    print("\n[2/7] Extracting custom messages from source...")
    custom_msgs = extract_custom_messages_from_source(source_root)
    print(f"  Found {len(custom_msgs)} custom message entries")

    for cm in custom_msgs:
        raw = cm['english']
        plain = raw_to_plain(raw)
        analysis = analyze_raw_text(raw)

        entry = LocalizationEntry(
            id=cm['id'],
            id_numeric=cm.get('id_numeric'),
            source_language="ENG",
            source_file=cm['source_file'],
            source_line_start=cm.get('source_line_start'),
            system=cm['system'],
            category="NPC_DIALOGUE",
            raw_text=raw,
            plain_text=plain,
            german_text=cm.get('german'),
            french_text=cm.get('french'),
            **analysis,
        )
        all_entries.append(entry)

    # 3. Extract hint texts
    print("\n[3/7] Extracting randomizer hint texts...")
    hint_entries = extract_hint_texts(source_root)
    print(f"  Found {len(hint_entries)} hint text entries")

    for h in hint_entries:
        raw = h['english']
        plain = raw_to_plain(raw)
        analysis = analyze_raw_text(raw)

        entry = LocalizationEntry(
            id=h['id'],
            source_language="ENG",
            source_file=h['source_file'],
            source_line_start=h.get('source_line_start'),
            system="RANDOMIZER_HINT",
            category="RANDOMIZER",
            subcategory=h.get('subcategory', ''),
            raw_text=raw,
            plain_text=plain,
            german_text=h.get('german'),
            french_text=h.get('french'),
            **analysis,
        )
        all_randomizer_entries.append(entry)

    # 4. Extract item list
    print("\n[4/7] Extracting randomizer item names...")
    item_entries = extract_item_list(source_root)
    print(f"  Found {len(item_entries)} item name entries")

    for item in item_entries:
        raw = item['english']
        plain = raw_to_plain(raw) if raw else raw
        entry = LocalizationEntry(
            id=item['id'],
            source_language="ENG",
            source_file=item['source_file'],
            source_line_start=item.get('source_line_start'),
            system="RANDOMIZER_ITEM",
            category="ITEM_NAME",
            raw_text=raw or "",
            plain_text=plain or "",
            german_text=item.get('german'),
            french_text=item.get('french'),
        )
        all_randomizer_entries.append(entry)

    # 5. Extract entrance/location/area names
    print("\n[5/7] Extracting location and entrance names...")
    entrance_entries = extract_entrance_names(source_root)
    location_entries = extract_location_names(source_root)
    area_entries = extract_area_names(source_root)
    ice_entries = extract_ice_trap_messages(source_root)
    print(f"  Entrance names: {len(entrance_entries)}")
    print(f"  Location names: {len(location_entries)}")
    print(f"  Area names: {len(area_entries)}")
    print(f"  Ice trap messages: {len(ice_entries)}")

    for e in entrance_entries + location_entries + area_entries + ice_entries:
        raw = e['english']
        plain = raw_to_plain(raw) if raw else raw
        entry = LocalizationEntry(
            id=e['id'],
            source_language="ENG",
            source_file=e['source_file'],
            source_line_start=e.get('source_line_start'),
            system=e['system'],
            category=e['category'],
            subcategory=e.get('subcategory', ''),
            raw_text=raw or "",
            plain_text=plain or "",
        )
        all_randomizer_entries.append(entry)

    # 6. Extract SoH UI strings
    print("\n[6/7] Extracting SoH UI strings...")
    ui_entries = extract_soh_ui_strings(source_root)
    print(f"  Found {len(ui_entries)} UI string entries")

    for ui in ui_entries:
        raw = ui['english']
        entry = LocalizationEntry(
            id=ui['id'],
            source_language="ENG",
            source_file=ui['source_file'],
            source_line_start=ui.get('source_line_start'),
            system=ui['system'],
            category=ui['category'],
            raw_text=raw,
            plain_text=raw,  # UI strings are already plain
        )
        all_soh_ui_entries.append(entry)

    # 7. Extract accessibility texts
    print("\n[7/7] Extracting accessibility texts...")
    acc_entries = extract_accessibility_texts(o2r_path)
    print(f"  Found {len(acc_entries)} accessibility text entries")

    for acc in acc_entries:
        raw = acc.get('english') or acc.get('german') or acc.get('french') or ""
        entry = LocalizationEntry(
            id=acc['id'],
            source_language="ENG",
            source_file=acc['source_file'],
            system="ACCESSIBILITY",
            category="SYSTEM",
            subcategory=acc.get('subcategory', ''),
            raw_text=raw,
            plain_text=raw,
            german_text=acc.get('german'),
            french_text=acc.get('french'),
        )
        all_soh_ui_entries.append(entry)

    # Detect duplicates in main corpus
    print("\n Detecting duplicates...")
    all_plain_texts = [e.plain_text for e in all_entries]
    text_to_indices = defaultdict(list)
    for i, text in enumerate(all_plain_texts):
        text_to_indices[text].append(i)

    dup_count = 0
    for text, indices in text_to_indices.items():
        if len(indices) > 1:
            group = f"DUP_{dup_count + 1:04d}"
            for idx in indices:
                all_entries[idx].is_duplicate_text = True
                all_entries[idx].duplicate_group = group
            dup_count += 1

    # Build multilingual alignment
    print(" Building multilingual alignment...")
    alignment = build_multilingual_alignment(all_entries)

    # Write outputs
    print("\n" + "=" * 70)
    print("WRITING OUTPUTS")
    print("=" * 70)

    os.makedirs(os.path.join(output_root, "corpus"), exist_ok=True)
    os.makedirs(os.path.join(output_root, "inventories"), exist_ok=True)
    os.makedirs(os.path.join(output_root, "reports"), exist_ok=True)

    # Main corpus JSONL
    jsonl_path = os.path.join(output_root, "corpus", "ocarina_messages_eng.jsonl")
    with open(jsonl_path, 'w', encoding='utf-8') as f:
        for entry in all_entries:
            f.write(json.dumps(entry.to_dict(), ensure_ascii=False) + '\n')
    print(f"  Written: {jsonl_path} ({len(all_entries)} entries)")

    # Main corpus CSV
    csv_path = os.path.join(output_root, "corpus", "ocarina_messages_eng.csv")
    with open(csv_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['id', 'id_numeric', 'system', 'category', 'subcategory',
                         'speaker_hint', 'plain_text', 'page_count', 'has_color_codes',
                         'has_button_prompt', 'is_duplicate_text'])
        for entry in all_entries:
            writer.writerow([
                entry.id, entry.id_numeric, entry.system, entry.category,
                entry.subcategory, entry.speaker_hint, entry.plain_text,
                entry.page_count, entry.has_color_codes, entry.has_button_prompt,
                entry.is_duplicate_text,
            ])
    print(f"  Written: {csv_path}")

    # SoH UI corpus
    soh_jsonl = os.path.join(output_root, "corpus", "soh_ui_strings_eng.jsonl")
    with open(soh_jsonl, 'w', encoding='utf-8') as f:
        for entry in all_soh_ui_entries:
            f.write(json.dumps(entry.to_dict(), ensure_ascii=False) + '\n')
    print(f"  Written: {soh_jsonl} ({len(all_soh_ui_entries)} entries)")

    soh_csv = os.path.join(output_root, "corpus", "soh_ui_strings_eng.csv")
    with open(soh_csv, 'w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['id', 'system', 'category', 'subcategory', 'plain_text', 'source_file'])
        for entry in all_soh_ui_entries:
            writer.writerow([
                entry.id, entry.system, entry.category, entry.subcategory,
                entry.plain_text, entry.source_file,
            ])
    print(f"  Written: {soh_csv}")

    # Randomizer corpus
    rando_jsonl = os.path.join(output_root, "corpus", "randomizer_strings_eng.jsonl")
    with open(rando_jsonl, 'w', encoding='utf-8') as f:
        for entry in all_randomizer_entries:
            f.write(json.dumps(entry.to_dict(), ensure_ascii=False) + '\n')
    print(f"  Written: {rando_jsonl} ({len(all_randomizer_entries)} entries)")

    # Multilingual alignment
    align_path = os.path.join(output_root, "corpus", "multilingual_alignment.jsonl")
    with open(align_path, 'w', encoding='utf-8') as f:
        for a in alignment:
            f.write(json.dumps(a, ensure_ascii=False) + '\n')
    print(f"  Written: {align_path} ({len(alignment)} entries)")

    # Button prompt inventory
    button_entries = [e for e in all_entries + all_soh_ui_entries + all_randomizer_entries
                      if e.has_button_prompt]
    btn_path = os.path.join(output_root, "inventories", "BUTTON_PROMPT_MESSAGES.csv")
    with open(btn_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['id', 'source_file', 'plain_text', 'glyphs', 'category', 'context_hint'])
        for e in button_entries:
            writer.writerow([e.id, e.source_file, e.plain_text, '|'.join(e.button_glyphs),
                             e.category, e.context_hint])
    print(f"  Written: {btn_path} ({len(button_entries)} entries)")

    # Choice messages
    choice_entries = [e for e in all_entries + all_randomizer_entries
                      if 'TWO_CHOICE' in e.control_codes or 'THREE_CHOICE' in e.control_codes
                      or 'CHOICE_MARKER' in str(e.control_codes)]
    choice_path = os.path.join(output_root, "inventories", "CHOICE_MESSAGES.csv")
    with open(choice_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['id', 'source_file', 'plain_text', 'choice_count', 'category'])
        for e in choice_entries:
            choice_count = 2 if 'TWO_CHOICE' in e.control_codes else 3 if 'THREE_CHOICE' in e.control_codes else 0
            writer.writerow([e.id, e.source_file, e.plain_text, choice_count, e.category])
    print(f"  Written: {choice_path} ({len(choice_entries)} entries)")

    # Dynamic variables
    var_entries = [e for e in all_entries + all_randomizer_entries if e.variables]
    var_path = os.path.join(output_root, "inventories", "DYNAMIC_VARIABLES.md")
    with open(var_path, 'w', encoding='utf-8') as f:
        f.write("# Dynamic Variables Inventory\n\n")
        f.write("| Variable | Example Message ID | Count |\n")
        f.write("|----------|-------------------|-------|\n")
        var_counts = defaultdict(lambda: {'count': 0, 'example': ''})
        for e in var_entries:
            for v in e.variables:
                var_counts[v]['count'] += 1
                if not var_counts[v]['example']:
                    var_counts[v]['example'] = e.id
        for var, info in sorted(var_counts.items()):
            f.write(f"| `{var}` | {info['example']} | {info['count']} |\n")
    print(f"  Written: {var_path} ({len(var_counts)} unique variables)")

    # Duplicate text groups
    dup_entries = [e for e in all_entries if e.is_duplicate_text]
    dup_path = os.path.join(output_root, "inventories", "DUPLICATE_TEXT_GROUPS.csv")
    with open(dup_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['id', 'duplicate_group', 'plain_text'])
        for e in dup_entries:
            writer.writerow([e.id, e.duplicate_group, e.plain_text])
    print(f"  Written: {dup_path} ({len(dup_entries)} duplicate entries)")

    # Control codes inventory
    all_codes = set()
    for e in all_entries + all_soh_ui_entries + all_randomizer_entries:
        all_codes.update(e.control_codes)
    cc_path = os.path.join(output_root, "inventories", "CONTROL_CODES.md")
    with open(cc_path, 'w', encoding='utf-8') as f:
        f.write("# Control Codes Inventory\n\n")
        f.write("## OoT Native Control Codes\n\n")
        f.write("| Code | Hex | Name | Description |\n")
        f.write("|------|-----|------|-------------|\n")
        for code_val, name in sorted(CONTROL_CODE_MAP.items()):
            f.write(f"| `{code_val:02X}` | 0x{code_val:02X} | {name} | See message_data_fmt.h |\n")
        f.write("\n## Custom Message Format Codes\n\n")
        f.write("| Code | Name | Description |\n")
        f.write("|------|------|-------------|\n")
        f.write("| `&` | NEWLINE | Line break within page |\n")
        f.write("| `^` | PAGE_BREAK | Wait for input, new page |\n")
        f.write("| `%w` | WHITE | White text |\n")
        f.write("| `%r` | RED | Red text |\n")
        f.write("| `%g` | GREEN | Green text (adjustable) |\n")
        f.write("| `%b` | BLUE | Blue text |\n")
        f.write("| `%c` | LIGHTBLUE | Light blue text |\n")
        f.write("| `%p` | PINK | Pink text |\n")
        f.write("| `%y` | YELLOW | Yellow text |\n")
        f.write("| `%B` | BLACK | Black text |\n")
        f.write("| `##` | COLOR_PLACEHOLDER | Replaced by stored color |\n")
        f.write("| `[[var]]` | VARIABLE | Dynamic variable insertion |\n")
        f.write("| `$icon` | ALTAR_ICON | Item icon display |\n")
        f.write("\n## Codes Found in Corpus\n\n")
        f.write("| Code | Occurrences |\n")
        f.write("|------|-------------|\n")
        code_counts = defaultdict(int)
        for e in all_entries + all_soh_ui_entries + all_randomizer_entries:
            for c in e.control_codes:
                code_counts[c] += 1
        for code, count in sorted(code_counts.items(), key=lambda x: -x[1]):
            f.write(f"| `{code}` | {count} |\n")
    print(f"  Written: {cc_path}")

    # Save raw exports
    raw_dir = os.path.join(output_root, "raw_exports")
    os.makedirs(raw_dir, exist_ok=True)
    raw_path = os.path.join(raw_dir, "vanilla_messages_nes.json")
    with open(raw_path, 'w', encoding='utf-8') as f:
        json.dump(nes_messages, f, ensure_ascii=False, indent=1)
    print(f"  Written: {raw_path}")

    # Statistics
    total_game = len(all_entries)
    total_soh_ui = len(all_soh_ui_entries)
    total_rando = len(all_randomizer_entries)
    unique_plain = len(set(e.plain_text for e in all_entries + all_soh_ui_entries + all_randomizer_entries))
    btn_count = len(button_entries)
    choice_count_len = len(choice_entries)
    var_count_len = len(var_entries)
    multipage = len([e for e in all_entries if e.page_count > 1])
    code_count = len(all_codes)
    unknown_speaker = len([e for e in all_entries if e.speaker_hint == "UNKNOWN"])
    unknown_context = len([e for e in all_entries + all_soh_ui_entries + all_randomizer_entries
                           if not e.context_hint and e.category == "OTHER_PLAYER_VISIBLE"])

    stats = {
        "TOTAL_GAME_MESSAGES_ENG": total_game,
        "TOTAL_SOH_UI_STRINGS": total_soh_ui,
        "TOTAL_RANDOMIZER_STRINGS": total_rando,
        "TOTAL_UNIQUE_PLAIN_TEXT": unique_plain,
        "TOTAL_DUPLICATE_ENTRIES": dup_count,
        "MESSAGES_WITH_BUTTON_GLYPHS": btn_count,
        "MESSAGES_WITH_CHOICES": choice_count_len,
        "MESSAGES_WITH_DYNAMIC_VARIABLES": var_count_len,
        "MULTIPAGE_MESSAGES": multipage,
        "CONTROL_CODE_TYPES": code_count,
        "UNKNOWN_CONTEXT_COUNT": unknown_context,
        "UNKNOWN_SPEAKER_COUNT": unknown_speaker,
    }

    stats_path = os.path.join(output_root, "reports", "extraction_stats.json")
    with open(stats_path, 'w', encoding='utf-8') as f:
        json.dump(stats, f, indent=2)
    print(f"  Written: {stats_path}")

    print("\n" + "=" * 70)
    print("EXTRACTION COMPLETE")
    print("=" * 70)
    for k, v in stats.items():
        print(f"  {k}: {v}")

    return stats


# ============================================================================
# ENTRY POINT
# ============================================================================

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Extract localization corpus from SoH source")
    parser.add_argument("--source-root", required=True, help="Path to shipwright source root")
    parser.add_argument("--o2r-root", required=True, help="Path to directory containing oot.o2r")
    parser.add_argument("--output-root", required=True, help="Path to localization workspace root")
    args = parser.parse_args()

    run_extraction(args.source_root, args.o2r_root, args.output_root)
