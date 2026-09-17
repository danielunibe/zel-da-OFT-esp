#!/usr/bin/env python3
import csv

qa_path = r'C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\localization_workspace\spanish\qa\MAIN_GAME_CONTEXTUAL_QA.csv'

flagged = []
with open(qa_path, 'r', encoding='utf-8') as f:
    reader = csv.DictReader(f)
    for row in reader:
        if row.get('qa_flags'):
            flagged.append(row)

print("Total flagged:", len(flagged))
print()

# Show first 15 with actual flags
for entry in flagged[:15]:
    eid = entry['id']
    en = entry['english'][:100]
    es = entry['current_spanish'][:100]
    risk = entry['risk_level']
    flags = entry['qa_flags']
    print("ID:", eid)
    print("  EN:", en)
    print("  ES:", es)
    print("  Risk:", risk)
    print("  Flags:", flags)
    print()

# Count flag types
from collections import Counter
all_flags = []
for entry in flagged:
    for flag in entry['qa_flags'].split('|'):
        all_flags.append(flag.split(':')[0] if ':' in flag else flag)

print("Flag type distribution:")
for flag, count in Counter(all_flags).most_common():
    print(f"  {flag}: {count}")
