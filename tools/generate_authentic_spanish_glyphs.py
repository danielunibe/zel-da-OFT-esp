#!/usr/bin/env python3
"""
generate_authentic_spanish_glyphs.py
Generates 11 authentic Nintendo-styled Spanish glyphs (16x16 I4 format, 128 bytes each)
derived directly from stock Nintendo OoT font textures in oot.o2r.
"""

import os
import struct
import zipfile

import argparse
from pathlib import Path

def decode_i4(data):
    grid = [[0]*16 for _ in range(16)]
    for r in range(16):
        for c in range(8):
            b = data[r * 8 + c]
            grid[r][c * 2] = (b >> 4) & 0xF
            grid[r][c * 2 + 1] = b & 0xF
    return grid

def encode_i4(grid):
    out = bytearray(128)
    for r in range(16):
        for c in range(8):
            hi = grid[r][c * 2] & 0xF
            lo = grid[r][c * 2 + 1] & 0xF
            out[r * 8 + c] = (hi << 4) | lo
    return bytes(out)

def main():
    repo_root = Path(__file__).resolve().parent.parent
    default_oot = repo_root / "oot.o2r"
    default_glyphs = repo_root / "data" / "glyphs"

    parser = argparse.ArgumentParser(description="Generate 11 authentic Spanish glyphs from stock OoT font textures")
    parser.add_argument("--oot", type=Path, default=default_oot, help="Path to user-owned oot.o2r archive")
    parser.add_argument("--glyphs-dir", type=Path, default=default_glyphs, help="Output directory for glyph .bin files")
    args = parser.parse_args()

    args.glyphs_dir.mkdir(parents=True, exist_ok=True)
    if not args.oot.exists():
        print(f"Error: Base ROM archive not found at {args.oot}. Specify with --oot <path>.", file=sys.stderr)
        sys.exit(1)

    print(f"Reading stock font textures from {args.oot}...")
    with zipfile.ZipFile(args.oot, "r") as zf:
        acute_lower = decode_i4(zf.read("textures/nes_font_static/gMsgChar96LatinSmallLetterEWithAcuteTex")[80:])
        acute_upper = decode_i4(zf.read("textures/nes_font_static/gMsgChar86LatinCapitalLetterEWithAcuteTex")[80:])
        excl = decode_i4(zf.read("textures/nes_font_static/gMsgChar21ExclamationMarkTex")[80:])
        quest = decode_i4(zf.read("textures/nes_font_static/gMsgChar3FQuestionMarkTex")[80:])
        a_grave = decode_i4(zf.read("textures/nes_font_static/gMsgChar80LatinCapitalLetterAWithGraveTex")[80:])
        i_upper = decode_i4(zf.read("textures/nes_font_static/gMsgChar49LatinCapitalLetterITex")[80:])
        n_upper = decode_i4(zf.read("textures/nes_font_static/gMsgChar4ELatinCapitalLetterNTex")[80:])
        o_circ = decode_i4(zf.read("textures/nes_font_static/gMsgChar8ALatinCapitalLetterOWithCircumflexTex")[80:])
        u_grave = decode_i4(zf.read("textures/nes_font_static/gMsgChar8CLatinCapitalLetterUWithGraveTex")[80:])
        i_lower = decode_i4(zf.read("textures/nes_font_static/gMsgChar69LatinSmallLetterITex")[80:])
        n_lower = decode_i4(zf.read("textures/nes_font_static/gMsgChar6ELatinSmallLetterNTex")[80:])
        o_lower = decode_i4(zf.read("textures/nes_font_static/gMsgChar6FLatinSmallLetterOTex")[80:])
        u_lower = decode_i4(zf.read("textures/nes_font_static/gMsgChar75LatinSmallLetterUTex")[80:])

    glyphs = {}

    # 1. msg_char_80.bin: Á (0x80)
    g_80 = [list(r) for r in a_grave]
    for c in range(16):
        g_80[0][c] = acute_upper[0][c]
        g_80[1][c] = acute_upper[1][c]
        g_80[2][c] = 0
    glyphs["msg_char_80.bin"] = g_80

    # 2. msg_char_81.bin: Ñ (0x81)
    g_81 = [[0]*16 for _ in range(16)]
    tilde_N = {
        0: [0, 0, 0x8, 0xF, 0xF, 0x4, 0, 0],
        1: [0, 0x4, 0x9, 0x4, 0x2, 0xF, 0x9, 0]
    }
    for r, vals in tilde_N.items():
        for c, v in enumerate(vals):
            g_81[r][c + 1] = v
    for r in range(1, 11):
        for c in range(16):
            g_81[r + 2][c] = n_upper[r][c]
    glyphs["msg_char_81.bin"] = g_81

    # 3. msg_char_83.bin: ¡ (0x83)
    g_83 = [[0]*16 for _ in range(16)]
    for r in range(1, 13):
        g_83[13 - r] = list(excl[r])
    glyphs["msg_char_83.bin"] = g_83

    # 4. msg_char_85.bin: ¿ (0x85)
    g_85 = [[0]*16 for _ in range(16)]
    for r in range(1, 13):
        for c in range(1, 9):
            g_85[13 - r][9 - c] = quest[r][c]
    glyphs["msg_char_85.bin"] = g_85

    # 5. msg_char_89.bin: Í (0x89)
    g_89 = [[0]*16 for _ in range(16)]
    for c in range(1, 8):
        g_89[0][c - 1] = acute_upper[0][c]
        g_89[1][c - 1] = acute_upper[1][c]
    for r in range(1, 11):
        for c in range(16):
            g_89[r + 2][c] = i_upper[r][c]
    glyphs["msg_char_89.bin"] = g_89

    # 6. msg_char_8a.bin: Ó (0x8A)
    g_8a = [list(r) for r in o_circ]
    for c in range(16):
        g_8a[0][c] = acute_upper[0][c]
        g_8a[1][c] = acute_upper[1][c]
        g_8a[2][c] = 0
    glyphs["msg_char_8a.bin"] = g_8a

    # 7. msg_char_8c.bin: Ú (0x8C)
    g_8c = [list(r) for r in u_grave]
    for c in range(16):
        g_8c[0][c] = acute_upper[0][c]
        g_8c[1][c] = acute_upper[1][c]
        g_8c[2][c] = 0
    glyphs["msg_char_8c.bin"] = g_8c

    # 8. msg_char_92.bin: ñ (0x92)
    g_92 = [list(r) for r in n_lower]
    tilde_n = {
        1: [0, 0, 0x8, 0xF, 0xF, 0x4, 0, 0],
        2: [0, 0x6, 0xF, 0xB, 0x4, 0xF, 0x9, 0],
        3: [0, 0x4, 0x9, 0x2, 0, 0x8, 0xF, 0x2]
    }
    for r, vals in tilde_n.items():
        for c, v in enumerate(vals):
            g_92[r][c] = v
    glyphs["msg_char_92.bin"] = g_92

    # 9. msg_char_99.bin: í (0x99)
    g_99 = [[0]*16 for _ in range(16)]
    for r in range(5, 16):
        for c in range(16):
            g_99[r][c] = i_lower[r][c]
    for r in range(1, 4):
        for c in range(1, 8):
            g_99[r][c - 1] = acute_lower[r][c]
    glyphs["msg_char_99.bin"] = g_99

    # 10. msg_char_9a.bin: ó (0x9A)
    g_9a = [list(r) for r in o_lower]
    for r in range(0, 5):
        for c in range(16):
            g_9a[r][c] = acute_lower[r][c]
    glyphs["msg_char_9a.bin"] = g_9a

    # 11. msg_char_9d.bin: ú (0x9D)
    g_9d = [list(r) for r in u_lower]
    for r in range(0, 5):
        for c in range(16):
            g_9d[r][c] = acute_lower[r][c]
    glyphs["msg_char_9d.bin"] = g_9d

    for filename, grid in glyphs.items():
        raw_bin = encode_i4(grid)
        assert len(raw_bin) == 128
        out_path = os.path.join(GLYPHS_DIR, filename)
        with open(out_path, "wb") as f:
            f.write(raw_bin)
        print(f"Generated {filename}: 128 bytes -> {out_path}")

    print("All 11 authentic Spanish glyphs successfully generated!")

if __name__ == "__main__":
    main()
