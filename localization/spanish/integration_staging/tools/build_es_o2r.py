#!/usr/bin/env python3
"""
build_es_o2r.py — Package verified Spanish messages and font glyphs into standard es.o2r archive.
Compatible with Ship of Harkinian / LibUltraShip O2rArchive (libzip).
"""

import os
import struct
import zipfile

DEV_ROOT = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV"
SPANISH_WORK = os.path.join(DEV_ROOT, "runtime", "build-test", "_spanish_work")
GLYPHS_DIR = os.path.join(SPANISH_WORK, "glyphs")
OUTPUT_O2R = os.path.join(DEV_ROOT, "runtime", "build-test", "es.o2r")

# Standard 64-byte OTR resource header for OTEX (Texture)
# Endianness: Little (0), IsCustom: False (0), Type: 'OTEX' (0x5845544F -> b'XETO'), Version: 0, ID: 0xDEADBEEFDEADBEEF, Reserved: 44 bytes
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

def main():
    print(f"Building {OUTPUT_O2R}...")
    
    # Read text table
    msg_path = os.path.join(SPANISH_WORK, "spa_message_data_static")
    with open(msg_path, "rb") as f:
        msg_data = f.read()
    print(f"Loaded spa_message_data_static: {len(msg_data)} bytes")
    
    with zipfile.ZipFile(OUTPUT_O2R, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        # Add message table
        zf.writestr("text/spa_message_data_static/spa_message_data_static", msg_data)
        print("  Added: text/spa_message_data_static/spa_message_data_static")
        
        # Add 11 font glyphs
        for bin_name, otr_path in GLYPH_MAP.items():
            bin_path = os.path.join(GLYPHS_DIR, bin_name)
            with open(bin_path, "rb") as bf:
                raw_pixels = bf.read()
            assert len(raw_pixels) == 128, f"Expected 128 bytes for {bin_name}, got {len(raw_pixels)}"
            
            payload = OTR_TEX_HEADER + TEX_META + raw_pixels
            assert len(payload) == 208, f"Expected 208 bytes, got {len(payload)}"
            
            zf.writestr(otr_path, payload)
            print(f"  Added: {otr_path} (208 bytes)")
            
    print(f"Successfully generated {OUTPUT_O2R} ({os.path.getsize(OUTPUT_O2R)} bytes)")

if __name__ == "__main__":
    main()
