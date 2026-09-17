#!/usr/bin/env python3
"""
test_round_trip.py — Full Corpus Round-Trip Verification & Character Coverage.
Tests all 7,652 entries in MASTER_LOCALIZATION_ES_419.jsonl through the codec,
validating 100% data preservation and character coverage.
"""

import os, sys, json, csv
from collections import Counter

DEV_ROOT = r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV"
SPANISH_ROOT = os.path.join(DEV_ROOT, "localization_workspace", "spanish")
L03A_ROOT = os.path.join(SPANISH_ROOT, "integration_staging")
QA_DIR = os.path.join(L03A_ROOT, "qa")

sys.path.insert(0, os.path.join(L03A_ROOT, "tools"))
from encode_es419_messages import encode_text, UNICODE_TO_BYTE
from decode_es419_messages import decode_bytes

MASTER_PATH = os.path.join(SPANISH_ROOT, "corpus", "MASTER_LOCALIZATION_ES_419.jsonl")

# 1. Round-trip verification
round_trip_rows = []
all_chars_seen = Counter()
mismatches = 0
encoding_errors = 0

with open(MASTER_PATH, "r", encoding="utf-8") as f:
    for line in f:
        if not line.strip():
            continue
        entry = json.loads(line)
        domain = entry.get("domain")
        eid = entry.get("id")
        orig_text = entry.get("spanish", "")
        
        for c in orig_text:
            all_chars_seen[c] += 1
            
        unsupported = 0
        encoded_data = b""
        decoded_text = ""
        verdict = "PASS"
        
        try:
            encoded_data = encode_text(orig_text, strict=True)
            decoded_text = decode_bytes(encoded_data)
            
            # Check preservation
            if decoded_text != orig_text:
                # Normalize \r or trailing space differences if any
                if decoded_text.replace("\r", "") != orig_text.replace("\r", ""):
                    verdict = "MISMATCH"
                    mismatches += 1
        except ValueError as e:
            verdict = "UNSUPPORTED_CHAR"
            encoding_errors += 1
            unsupported = 1
            
        round_trip_rows.append({
            "domain": domain,
            "id": eid,
            "original_length": len(orig_text),
            "encoded_bytes": len(encoded_data),
            "decoded_match": "YES" if verdict == "PASS" else "NO",
            "unsupported_char_count": unsupported,
            "verdict": verdict
        })

os.makedirs(QA_DIR, exist_ok=True)
round_trip_path = os.path.join(QA_DIR, "ROUND_TRIP_RESULTS.csv")
with open(round_trip_path, "w", encoding="utf-8", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=[
        "domain", "id", "original_length", "encoded_bytes",
        "decoded_match", "unsupported_char_count", "verdict"
    ])
    writer.writeheader()
    writer.writerows(round_trip_rows)
print(f"Wrote ROUND_TRIP_RESULTS.csv with {len(round_trip_rows)} entries.")
print(f"Mismatches: {mismatches}, Encoding Errors: {encoding_errors}")

# 2. Character coverage analysis
coverage_rows = []
total_corpus_chars = sum(all_chars_seen.values())
unsupported_chars = []

for ch, freq in all_chars_seen.most_common():
    cp = ord(ch)
    u_hex = f"U+{cp:04X}"
    is_ascii = (32 <= cp <= 126) or ch in ["\n", "\r", "\t"]
    is_sp = ch in UNICODE_TO_BYTE
    
    if is_sp:
        slot = f"0x{UNICODE_TO_BYTE[ch]:02X}"
        supported = "YES"
        status = "NATIVE_OR_REMAPPED_SLOT"
    elif is_ascii:
        slot = f"0x{cp:02X}"
        supported = "YES"
        status = "STANDARD_ASCII_FONT"
    else:
        slot = "NONE"
        supported = "NO"
        status = "UNSUPPORTED_BLOCKED"
        unsupported_chars.append(ch)
        
    coverage_rows.append({
        "unicode": u_hex,
        "character": repr(ch) if ch in ["\n", "\r", "\t"] else ch,
        "frequency": freq,
        "percentage": f"{(freq / total_corpus_chars) * 100:.4f}%",
        "supported_now": supported,
        "future_slot": slot,
        "status": status
    })

coverage_path = os.path.join(QA_DIR, "CHARACTER_COVERAGE.csv")
with open(coverage_path, "w", encoding="utf-8", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=[
        "unicode", "character", "frequency", "percentage",
        "supported_now", "future_slot", "status"
    ])
    writer.writeheader()
    writer.writerows(coverage_rows)

print(f"Wrote CHARACTER_COVERAGE.csv with {len(coverage_rows)} unique characters.")
supported_count = sum(r["frequency"] for r in coverage_rows if r["supported_now"] == "YES")
coverage_pct = (supported_count / total_corpus_chars) * 100
print(f"Overall Character Coverage: {coverage_pct:.4f}% ({supported_count}/{total_corpus_chars})")
print(f"Unsupported characters: {unsupported_chars if unsupported_chars else 'NONE'}")
