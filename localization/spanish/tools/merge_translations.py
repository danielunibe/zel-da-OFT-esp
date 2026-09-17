#!/usr/bin/env python3
"""
merge_translations.py — Merge translated batches back into a single corpus file.

Reads batch_*_translated.jsonl files and merges them into ocarina_messages_es_419.jsonl.
"""

import json
import sys
import os
import glob
import argparse


def merge_batches(batch_dir: str, output_path: str):
    """Merge all translated batch files into a single output."""
    # Find all translated batch files
    pattern = os.path.join(batch_dir, "batch_*_translated.jsonl")
    batch_files = sorted(glob.glob(pattern))
    
    if not batch_files:
        print(f"No translated batch files found in {batch_dir}", file=sys.stderr)
        sys.exit(1)
    
    print(f"Found {len(batch_files)} translated batch files")
    
    all_entries = []
    for bf in batch_files:
        with open(bf, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if line:
                    entry = json.loads(line)
                    all_entries.append(entry)
    
    # Sort by id_numeric
    all_entries.sort(key=lambda e: e.get('id_numeric', 0))
    
    # Write merged output
    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)
    with open(output_path, 'w', encoding='utf-8') as f:
        for entry in all_entries:
            f.write(json.dumps(entry, ensure_ascii=False) + '\n')
    
    print(f"Merged {len(all_entries)} entries to {output_path}")
    
    # Stats
    statuses = {}
    for e in all_entries:
        s = e.get('translation_status', 'UNKNOWN')
        statuses[s] = statuses.get(s, 0) + 1
    
    print("\nTranslation status summary:")
    for s, c in sorted(statuses.items()):
        print(f"  {s}: {c}")


def main():
    parser = argparse.ArgumentParser(description="Merge translated batches")
    parser.add_argument("--batch-dir", "-d", required=True, help="Directory containing batch files")
    parser.add_argument("--output", "-o", required=True, help="Output merged JSONL file")
    args = parser.parse_args()
    merge_batches(args.batch_dir, args.output)


if __name__ == "__main__":
    main()
