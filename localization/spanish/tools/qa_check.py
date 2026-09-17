#!/usr/bin/env python3
"""QA check on translated corpus."""
import json
import random

corpus_path = r'C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\localization_workspace\spanish\corpus\ocarina_messages_es_419.jsonl'

entries = []
with open(corpus_path, 'r', encoding='utf-8') as f:
    for line in f:
        entries.append(json.loads(line.strip()))

# QA checks
issues = []
for e in entries:
    es = e.get('plain_text_es_419', '')
    if not es:
        continue
    
    # Check 1: Line count mismatch (only for entries with newlines in original)
    orig_text = e.get('plain_text', '')
    if '\n' in orig_text:
        orig_lines = orig_text.count('\n')
        es_lines = es.count('\n')
        if abs(orig_lines - es_lines) > 2:
            issues.append({'id': e['id'], 'type': 'LINE_COUNT', 'detail': f'Orig: {orig_lines+1} lines, ES: {es_lines+1} lines'})
    
    # Check 3: Choice messages
    orig_cc = e.get('control_codes', [])
    if 'TWO_CHOICE' in orig_cc or 'THREE_CHOICE' in orig_cc:
        if '[CHOICE:' not in es:
            issues.append({'id': e['id'], 'type': 'CHOICE_MISSING', 'detail': 'Choice marker missing'})

print(f'Total entries: {len(entries)}')
print(f'Entries with translations: {sum(1 for e in entries if e.get("plain_text_es_419"))}')
print(f'Entries skipped: {sum(1 for e in entries if not e.get("plain_text_es_419"))}')
print(f'\nQA Issues found: {len(issues)}')
for issue in issues[:20]:
    print(f'  [{issue["type"]}] {issue["id"]}: {issue["detail"]}')
if len(issues) > 20:
    print(f'  ... and {len(issues) - 20} more')

# Sample translations
print('\n=== Sample Translations ===')
random.seed(42)
eligible = [e for e in entries if e.get('plain_text_es_419') and e.get('plain_text')]
count = min(8, len(eligible))
if count > 0:
    samples = random.sample(eligible, count)
    for s in samples:
        print(f'\nID: {s["id"]}')
        orig = s.get('plain_text', '')[:120]
        trans = s.get('plain_text_es_419', '')[:120]
        print(f'  EN: {orig}')
        print(f'  ES: {trans}')
