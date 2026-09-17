#!/usr/bin/env python3
"""
rebuild_spanish_archive.py - Couch Edition RC1.1 Deterministic Package Builder
Generates a reproducible es.o2r archive containing validated static Spanish messages
and authentic 16x16 I4 font glyph textures.
"""

import os
import sys
import struct
import zipfile
import argparse
from pathlib import Path

# Standard 64-byte OTR resource header for OTEX (Texture)
OTR_TEX_HEADER = (
    b'\x00\x00\x00\x00' +
    b'XETO' +
    b'\x00\x00\x00\x00' +
    b'\xef\xbe\xad\xde\xef\xbe\xad\xde' +  # Canonical upstream default sentinel ID
    b'\x00' * 44
)

EXPECTED_GLYPHS = [
    (0x80, "msg_char_80.bin"), # Á
    (0x81, "msg_char_81.bin"), # Ñ
    (0x83, "msg_char_83.bin"), # ¡
    (0x85, "msg_char_85.bin"), # ¿
    (0x89, "msg_char_89.bin"), # Í
    (0x8A, "msg_char_8a.bin"), # Ó
    (0x8C, "msg_char_8c.bin"), # Ú
    (0x92, "msg_char_92.bin"), # ñ
    (0x99, "msg_char_99.bin"), # í
    (0x9A, "msg_char_9a.bin"), # ó
    (0x9D, "msg_char_9d.bin"), # ú
]

def validate_otxt(data: bytes):
    if len(data) < 0x44:
        raise ValueError(f"OTXT data too short: {len(data)} bytes")
    header = data[:0x40]
    count = struct.unpack('<I', data[0x40:0x44])[0]
    offset = 0x44
    entries = []
    seen_ids = set()

    for idx in range(count):
        if offset + 8 > len(data):
            raise ValueError(f"Truncated entry header at index {idx}, offset {offset}")
        tid = struct.unpack('<H', data[offset:offset+2])[0]
        tb_type = data[offset+2]
        tb_pos = data[offset+3]
        offset += 4
        slen = struct.unpack('<I', data[offset:offset+4])[0]
        offset += 4
        if offset + slen > len(data):
            raise ValueError(f"Message {tid:04X} length {slen} exceeds EOF at offset {offset}")
        msg = data[offset:offset+slen]
        offset += slen

        if tid in seen_ids:
            raise ValueError(f"Duplicate message ID: 0x{tid:04X}")
        seen_ids.add(tid)
        entries.append((tid, tb_type, tb_pos, msg))

    if offset != len(data):
        raise ValueError(f"Trailing unexpected bytes: {len(data) - offset} bytes remain after {count} entries")

    return count, entries

def build_archive(data_dir: Path, output_path: Path):
    spa_msg_file = data_dir / "spa_message_data_static"
    glyphs_dir = data_dir / "glyphs"

    if not spa_msg_file.exists():
        raise FileNotFoundError(f"Message table file not found: {spa_msg_file}")
    if not glyphs_dir.exists():
        raise FileNotFoundError(f"Glyphs directory not found: {glyphs_dir}")

    # Read and validate static messages
    with open(spa_msg_file, "rb") as f:
        msg_bytes = f.read()

    count, entries = validate_otxt(msg_bytes)
    print(f"Validated {count} static Spanish messages from {spa_msg_file.name}")

    # Collect archive contents in a dictionary for deterministic ordering
    archive_contents = {}

    # 1. Static messages resource
    archive_contents["text/nes_message_data_static/spa_message_data_static"] = msg_bytes

    # 2. Font glyph textures
    glyph_count = 0
    for char_code, fname in EXPECTED_GLYPHS:
        gpath = glyphs_dir / fname
        if not gpath.exists():
            raise FileNotFoundError(f"Required font glyph missing: {gpath}")
        with open(gpath, "rb") as gf:
            gdata = gf.read()
        if len(gdata) != 128:
            raise ValueError(f"Invalid font glyph size for {fname}: expected 128 bytes, got {len(gdata)}")

        # Build full OTEX resource with 64-byte header
        otex_resource = OTR_TEX_HEADER + gdata
        archive_name = f"textures/font/font_pal/msg_char_{char_code:02x}"
        archive_contents[archive_name] = otex_resource
        glyph_count += 1

    print(f"Validated and packed {glyph_count}/11 authentic font glyphs")

    # Write deterministic ZIP archive
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fixed_time = (2026, 1, 1, 0, 0, 0)

    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        # Sort archive entries lexicographically for 100% deterministic output
        for arcname in sorted(archive_contents.keys()):
            content = archive_contents[arcname]
            zinfo = zipfile.ZipInfo(filename=arcname, date_time=fixed_time)
            zinfo.compress_type = zipfile.ZIP_DEFLATED
            zinfo.external_attr = 0o644 << 16
            zf.writestr(zinfo, content)

    print(f"Successfully generated deterministic archive: {output_path} ({output_path.stat().st_size} bytes)")

def main():
    repo_root = Path(__file__).resolve().parent.parent
    default_data_dir = repo_root / "data"
    default_output = repo_root / "es.o2r"

    parser = argparse.ArgumentParser(description="Rebuild reproducible Couch Edition es.o2r archive")
    parser.add_argument("--data-dir", type=Path, default=default_data_dir, help="Directory containing spa_message_data_static and glyphs/")
    parser.add_argument("--output", type=Path, default=default_output, help="Target path for es.o2r")
    args = parser.parse_args()

    try:
        build_archive(args.data_dir, args.output)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
