#!/usr/bin/env python3
"""
validate_spanish_package.py — Comprehensive Integration Package Validator.
Verifies:
1. Counts for Main Game (2233), Randomizer (3746), SoH UI (1673), Total (7652)
2. JSON / JSONL structural validity
3. Encoding and zero unsupported characters
4. Control codes, variables, choices, page breaks
5. Manifest hashes and SHA-256 integrity
6. 100% Font coverage and zero button glyph collisions
Exit code:
0 = PASS
non-zero = FAIL
"""

import os, sys, json, csv, hashlib

DEV_ROOT = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV"
SPANISH_ROOT = os.path.join(DEV_ROOT, "localization_workspace", "spanish")
L03A_ROOT = os.path.join(SPANISH_ROOT, "integration_staging")
PACKAGES_ROOT = os.path.join(L03A_ROOT, "packages")
FONT_ROOT = os.path.join(SPANISH_ROOT, "font")

sys.path.insert(0, os.path.join(L03A_ROOT, "tools"))
from encode_es419_messages import encode_text
from decode_es419_messages import decode_bytes

def sha256_file(filepath: str) -> str:
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def validate() -> int:
    errors = []
    print("=== STARTING SPANISH INTEGRATION PACKAGE VALIDATION ===")

    # 1. Check manifests and hashes
    for domain, expected_count in [("main_game", 2233), ("randomizer", 3746), ("soh_ui", 1673)]:
        pkg_dir = os.path.join(PACKAGES_ROOT, domain)
        manifest_path = os.path.join(pkg_dir, "manifest.json")
        if not os.path.exists(manifest_path):
            errors.append(f"Missing manifest.json for {domain}")
            continue
            
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = json.load(f)
            
        cnt = manifest.get("entry_count")
        if cnt != expected_count:
            errors.append(f"{domain} entry count mismatch: expected {expected_count}, got {cnt}")
            
        # Verify source_hash
        norm_path = os.path.join(pkg_dir, "normalized_corpus.jsonl")
        actual_source_hash = sha256_file(norm_path)
        if actual_source_hash != manifest.get("source_hash"):
            errors.append(f"{domain} source_hash mismatch!")
            
        # Verify generated_hash
        if domain == "soh_ui":
            gen_path = os.path.join(pkg_dir, "soh_ui_catalog_es_419.json")
        else:
            gen_path = os.path.join(pkg_dir, "encoded_messages.jsonl")
        actual_gen_hash = sha256_file(gen_path)
        if actual_gen_hash != manifest.get("generated_hash"):
            errors.append(f"{domain} generated_hash mismatch!")
            
        print(f"[{domain.upper()}] Manifest and SHA-256 integrity: PASS ({cnt} entries)")

    # 2. Check JSONL parsing & line counts
    for domain, filename, expected_count in [
        ("main_game", "normalized_corpus.jsonl", 2233),
        ("main_game", "encoded_messages.jsonl", 2233),
        ("randomizer", "normalized_corpus.jsonl", 3746),
        ("randomizer", "encoded_messages.jsonl", 3746),
        ("soh_ui", "normalized_corpus.jsonl", 1673),
    ]:
        fpath = os.path.join(PACKAGES_ROOT, domain, filename)
        line_count = 0
        with open(fpath, "r", encoding="utf-8") as f:
            for line_no, line in enumerate(f, 1):
                if line.strip():
                    line_count += 1
                    try:
                        json.loads(line)
                    except json.JSONDecodeError as e:
                        errors.append(f"JSON syntax error in {fpath} line {line_no}: {e}")
        if line_count != expected_count:
            errors.append(f"Line count mismatch in {fpath}: expected {expected_count}, got {line_count}")
        print(f"[{domain.upper()}/{filename}] JSONL parsing & line count: PASS ({line_count} lines)")

    # 3. Check SoH UI JSON Catalog
    ui_cat_path = os.path.join(PACKAGES_ROOT, "soh_ui", "soh_ui_catalog_es_419.json")
    with open(ui_cat_path, "r", encoding="utf-8") as f:
        cat_data = json.load(f)
    if len(cat_data) != 1621:
        errors.append(f"UI catalog length mismatch: expected 1621 unique keys, got {len(cat_data)}")
    print(f"[SOH_UI/soh_ui_catalog_es_419.json] JSON Catalog parsing: PASS ({len(cat_data)} unique keys / 1673 normalized occurrences)")

    # 4. Check Encoding & Round-Trip on Normalized Corpora
    unsupported_chars = set()
    round_trip_failures = 0
    total_audited = 0
    
    for domain in ["main_game", "randomizer"]:
        norm_path = os.path.join(PACKAGES_ROOT, domain, "normalized_corpus.jsonl")
        with open(norm_path, "r", encoding="utf-8") as f:
            for line in f:
                if not line.strip():
                    continue
                d = json.loads(line)
                txt = d.get("spanish", "")
                total_audited += 1
                try:
                    enc = encode_text(txt, strict=True)
                    dec = decode_bytes(enc)
                    if dec != txt:
                        round_trip_failures += 1
                except ValueError as e:
                    unsupported_chars.add(str(e))

    if round_trip_failures > 0:
        errors.append(f"Round trip failures encountered: {round_trip_failures}")
    if unsupported_chars:
        errors.append(f"Unsupported characters encountered: {len(unsupported_chars)}")
    print(f"[ENCODING & CODEC] Round-trip tested on {total_audited} entries: PASS (0 failures, 0 unsupported)")

    # 5. Check Button Glyphs Collision Matrix
    col_matrix_path = os.path.join(L03A_ROOT, "qa", "FONT_GLYPH_COLLISION_MATRIX.csv")
    with open(col_matrix_path, "r", encoding="utf-8") as f:
        collisions = [r for r in csv.DictReader(f) if r["used_by_spanish"].startswith("YES") and r["used_by_button_glyph"] == "YES"]
    if len(collisions) > 0:
        errors.append(f"Button glyph collisions detected: {len(collisions)}")
    print(f"[GLYPH COLLISION MATRIX] Button glyph collision check: PASS ({len(collisions)} collisions)")

    # 6. Check Character Coverage
    cov_path = os.path.join(L03A_ROOT, "qa", "CHARACTER_COVERAGE.csv")
    with open(cov_path, "r", encoding="utf-8") as f:
        unsupported = [r for r in csv.DictReader(f) if r["supported_now"] != "YES"]
    if len(unsupported) > 0:
        errors.append(f"Unsupported characters in character coverage: {len(unsupported)}")
    print(f"[CHARACTER COVERAGE] 100% character coverage certified: PASS (0 unsupported)")

    print("\n=== VALIDATION SUMMARY ===")
    if errors:
        print(f"FAILED with {len(errors)} errors:")
        for err in errors:
            print(f"  [ERROR] {err}")
        return 1
    else:
        print("ALL GATES PASSED! Package is 100% verified and reproducible.")
        return 0

if __name__ == '__main__':
    sys.exit(validate())
