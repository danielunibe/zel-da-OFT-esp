#!/usr/bin/env python3
"""
rebuild_spanish_archive.py
Restores proper OoT message terminators and control tails from NTSC message table into Spanish messages,
serializes spa_message_data_static, and rebuilds es.o2r.
"""

import os
import struct
import zipfile

DEV_ROOT = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV"
OOT_O2R = os.path.join(DEV_ROOT, "runtime", "build-test", "oot.o2r")
SPANISH_WORK = os.path.join(DEV_ROOT, "runtime", "build-test", "_spanish_work")
SPA_MSG_PATH = os.path.join(SPANISH_WORK, "spa_message_data_static")
GLYPHS_DIR = os.path.join(SPANISH_WORK, "glyphs")
OUTPUT_O2R = os.path.join(DEV_ROOT, "runtime", "build-test", "es.o2r")

CTRL_ARGS = {
    0x01: 0, # NEWLINE
    0x02: 0, # END
    0x04: 0, # BOX_BREAK
    0x05: 1, # COLOR
    0x06: 1, # SHIFT
    0x07: 2, # TEXTID
    0x08: 0, # QUICKTEXT_ENABLE
    0x09: 0, # QUICKTEXT_DISABLE
    0x0A: 0, # PERSISTENT
    0x0B: 0, # EVENT
    0x0C: 1, # BOX_BREAK_DELAYED
    0x0D: 0, # AWAIT_BUTTON_PRESS
    0x0E: 1, # FADE
    0x0F: 0, # NAME
    0x10: 0, # OCARINA
    0x11: 2, # FADE2
    0x12: 2, # SFX
    0x13: 1, # ITEM_ICON
    0x14: 1, # TEXT_SPEED
    0x15: 3, # BACKGROUND
    0x16: 0, # MARATHON_TIME
    0x17: 0, # RACE_TIME
    0x18: 0, # POINTS
    0x19: 0, # TOKENS
    0x1A: 0, # UNSKIPPABLE
    0x1B: 0, # TWO_CHOICE
    0x1C: 0, # THREE_CHOICE
    0x1D: 0, # FISH_INFO
    0x1E: 1, # HIGHSCORE
    0x1F: 0, # TIME
}

# Standard 64-byte OTR resource header for OTEX (Texture)
OTR_TEX_HEADER = (
    b'\x00\x00\x00\x00' +
    b'XETO' +
    b'\x00\x00\x00\x00' +
    b'\xef\xbe\xad\xde\xef\xbe\xad\xde' +
    b'\x00' * 44
)

# 16-byte Fast::Texture header:
# Type: 5 (I4), Width: 16, Height: 16, ImageDataSize: 128
TEX_META = struct.pack('<IIII', 5, 16, 16, 128)

GLYPH_MAP = {
    "msg_char_80.bin": "textures/nes_font_static/gMsgChar80LatinCapitalLetterAWithAcuteTex",
    "msg_char_81.bin": "textures/nes_font_static/gMsgChar81LatinCapitalLetterNWithTildeTex",
    "msg_char_83.bin": "textures/nes_font_static/gMsgChar83InvertedExclamationMarkTex",
    "msg_char_85.bin": "textures/nes_font_static/gMsgChar85InvertedQuestionMarkTex",
    "msg_char_89.bin": "textures/nes_font_static/gMsgChar89LatinCapitalLetterIWithAcuteTex",
    "msg_char_8a.bin": "textures/nes_font_static/gMsgChar8ALatinCapitalLetterOWithAcuteTex",
    "msg_char_8c.bin": "textures/nes_font_static/gMsgChar8CLatinCapitalLetterUWithAcuteTex",
    "msg_char_92.bin": "textures/nes_font_static/gMsgChar92LatinSmallLetterNWithTildeTex",
    "msg_char_99.bin": "textures/nes_font_static/gMsgChar99LatinSmallLetterIWithAcuteTex",
    "msg_char_9a.bin": "textures/nes_font_static/gMsgChar9ALatinSmallLetterOWithAcuteTex",
    "msg_char_9d.bin": "textures/nes_font_static/gMsgChar9DLatinSmallLetterUWithAcuteTex",
}

def parse_tail(m: bytes):
    tokens = []
    idx = 0
    while idx < len(m):
        b = m[idx]
        if b in CTRL_ARGS:
            n = CTRL_ARGS[b]
            tokens.append((True, b, m[idx:idx+1+n]))
            idx += 1 + n
        else:
            tokens.append((False, b, m[idx:idx+1]))
            idx += 1
    tail = []
    for is_ctrl, code, raw in reversed(tokens):
        if is_ctrl and code != 0x01: # control token, not newline
            tail.append(raw)
        else:
            break
    tail.reverse()
    return b''.join(tail)

def read_otxt(data: bytes):
    header = data[:0x40]
    offset = 0x40
    count = struct.unpack('<I', data[offset:offset+4])[0]
    offset += 4
    entries = []
    for i in range(count):
        tid = struct.unpack('<H', data[offset:offset+2])[0]
        tb_type = data[offset+2]
        tb_pos = data[offset+3]
        offset += 4
        slen = struct.unpack('<I', data[offset:offset+4])[0]
        offset += 4
        m = data[offset:offset+slen]
        offset += slen
        entries.append((tid, tb_type, tb_pos, m))
    return header, entries

def main():
    print(f"Reading NTSC message table from {OOT_O2R}...")
    with zipfile.ZipFile(OOT_O2R, "r") as zf:
        ntsc_raw = zf.read("text/nes_message_data_static/ntsc_nes_message_data_static")
    _, ntsc_entries = read_otxt(ntsc_raw)
    ntsc_dict = {e[0]: e for e in ntsc_entries}
    print(f"Loaded {len(ntsc_entries)} NTSC messages.")

    print(f"Reading Spanish message table from {SPA_MSG_PATH}...")
    with open(SPA_MSG_PATH, "rb") as f:
        spa_raw = f.read()
    spa_header, spa_entries = read_otxt(spa_raw)
    print(f"Loaded {len(spa_entries)} Spanish messages.")

    fixed_entries = []
    modified_count = 0
    preserved_count = 0

    for tid, stype, spos, smsg in spa_entries:
        if tid in ntsc_dict:
            ntid, ntype, npos, nmsg = ntsc_dict[tid]
            ntail = parse_tail(nmsg)
            stail = parse_tail(smsg)

            if stail == ntail:
                preserved_count += 1
                new_msg = smsg
            else:
                modified_count += 1
                if len(stail) > 0:
                    smsg_base = smsg[:-len(stail)]
                else:
                    smsg_base = smsg
                new_msg = smsg_base + ntail
        else:
            new_msg = smsg

        fixed_entries.append((tid, stype, spos, new_msg))

    print(f"Tail reconciliation: {modified_count} messages updated with exact NTSC control tail, {preserved_count} preserved.")

    # Re-serialize spa_message_data_static
    out_buf = bytearray(spa_header)
    out_buf.extend(struct.pack('<I', len(fixed_entries)))
    for tid, stype, spos, msg in fixed_entries:
        out_buf.extend(struct.pack('<HBB', tid, stype, spos))
        out_buf.extend(struct.pack('<I', len(msg)))
        out_buf.extend(msg)

    # Save backup of previous spa_message_data_static
    bak_path = SPA_MSG_PATH + ".bak_before_tail_fix"
    if not os.path.exists(bak_path):
        with open(bak_path, "wb") as bf:
            bf.write(spa_raw)
        print(f"Backed up original spa_message_data_static to {bak_path}")

    # Write updated spa_message_data_static
    with open(SPA_MSG_PATH, "wb") as f:
        f.write(out_buf)
    print(f"Wrote updated {SPA_MSG_PATH} ({len(out_buf)} bytes)")

    # Build es.o2r
    print(f"Packaging {OUTPUT_O2R}...")
    with zipfile.ZipFile(OUTPUT_O2R, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("text/spa_message_data_static/spa_message_data_static", out_buf)
        print("  Added: text/spa_message_data_static/spa_message_data_static")

        for bin_name, otr_path in GLYPH_MAP.items():
            bin_path = os.path.join(GLYPHS_DIR, bin_name)
            with open(bin_path, "rb") as bf:
                raw_pixels = bf.read()
            assert len(raw_pixels) == 128, f"Expected 128 bytes for {bin_name}, got {len(raw_pixels)}"
            payload = OTR_TEX_HEADER + TEX_META + raw_pixels
            zf.writestr(otr_path, payload)
            print(f"  Added: {otr_path} (208 bytes)")

    print(f"Successfully generated {OUTPUT_O2R} ({os.path.getsize(OUTPUT_O2R)} bytes)")

if __name__ == "__main__":
    main()
