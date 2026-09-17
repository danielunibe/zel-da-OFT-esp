#!/usr/bin/env python3
"""
refined_semantic_qa.py — Refined contextual QA with reduced false positives.

Focuses on real semantic issues rather than keyword coincidences.
"""

import json
import csv
import os
import re
from pathlib import Path
from collections import Counter

DEV_ROOT = Path(r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV")
LOCALIZATION_ROOT = DEV_ROOT / "localization_workspace"
SPANISH_ROOT = LOCALIZATION_ROOT / "spanish"

# ── Real directional contexts (where direction matters for gameplay) ────────
# Only flag if the English has a DIRECTIONAL sense (not phrasal verbs)

REAL_DIRECTION_PATTERNS = [
    # Explicit spatial directions
    (r'\bgo\s+(north|south|east|west|left|right|up|down)\b', "DIRECTION"),
    (r'\bhead\s+(north|south|east|west|left|right|up|down)\b', "DIRECTION"),
    (r'\bwalk\s+(north|south|east|west|left|right|up|down)\b', "DIRECTION"),
    (r'\bface\s+(north|south|east|west|left|right|up|down)\b', "DIRECTION"),
    (r'\b(to the|toward|towards)\s+(north|south|east|west|left|right|up|down)\b', "DIRECTION"),
    (r'\b(north|south|east|west|left|right)\s+(wall|side|area|room|passage|path|door)\b', "DIRECTION"),
    (r'\b(above|below|behind|beneath)\s+(the|a|this|that)\b', "SPATIAL"),
    (r'\b(look|search|check|find)\s+(behind|under|above|below|north|south|east|west)\b', "DIRECTION"),
    # Puzzle-specific directions
    (r'\b(light|place|put|set)\s+(the\s+)?(torch|fire|flame)\s+(on|in|at)\b', "PUZZLE_ACTION"),
    (r'\b(push|pull|hit|strike|activate)\s+(the\s+)?(block|switch|button|stone)\b', "PUZZLE_ACTION"),
    (r'\b(play|blow|whistle|sing)\s+(the\s+)?(ocarina|song|melody)\b', "PUZZLE_ACTION"),
    (r'\bhookshot\s+(to|at|on)\b', "PUZZLE_ACTION"),
    (r'\bbomb\s+(the|a)\s+(wall|floor|rock|boulder)\b', "PUZZLE_ACTION"),
]

# ── Actual false friend detection (in context) ─────────────────────────────
FALSE_FRIEND_CONTEXTS = [
    # Only flag when used with wrong meaning
    (r'\bembarazad[ao]\b', "FALSE_FRIEND", "embarazada = pregnant, not embarrassed"),
    (r'\bactualmente\b', "FALSE_FRIEND", "actualmente = currently, not actually"),
    (r'\beventualmente\b', "FALSE_FRIEND", "eventualmente = eventually, not eventually"),
]

# ── Real terminology conflicts ──────────────────────────────────────────────
TERMINOLOGY_RULES = [
    # Hookshot must be consistent
    (r'\bhookshot\b', ['anzuelo', 'gancho', 'ancla'], "HOOKSHOT"),
    (r'\bHookshot\b', ['Anzuelo', 'Gancho', 'Ancla'], "HOOKSHOT"),
    (r'\bLongshot\b', ['Anzuelo Largo', 'Anclaje Largo', 'Gancho Largo'], "LONGSHOT"),
    # Shield
    (r'\bDeku Shield\b', ['Escudo Deku'], "SHIELD"),
    (r'\bHylian Shield\b', ['Escudo Hyliano', 'Escudo Hyruleano'], "SHIELD"),
    (r'\bMirror Shield\b', ['Escudo Espejo'], "SHIELD"),
    # Tunic
    (r'\bKokiri Tunic\b', ['Túnica Kokiri'], "TUNIC"),
    (r'\bGoron Tunic\b', ['Túnica Goron'], "TUNIC"),
    (r'\bZora Tunic\b', ['Túnica Zora'], "TUNIC"),
    # Boots
    (r'\bHover Boots\b', ['Botas Voladoras', 'Botas Flotantes'], "BOOTS"),
    (r'\bIron Boots\b', ['Botas de Hierro'], "BOOTS"),
]

# ── Puzzle hint patterns ────────────────────────────────────────────────────
PUZZLE_HINT_PATTERNS = [
    # Light/fire torches
    (r'\b(light|lit|burning|flame|fire)\b.*\b(torch|brazier|lamp)\b', "TORCH_PUZZLE"),
    (r'\b(torch|brazier|lamp)\b.*\b(light|lit|burning|flame|fire)\b', "TORCH_PUZZLE"),
    # Block pushing
    (r'\b(push|pull|move)\b.*\b(block|stone|crate|boulder)\b', "BLOCK_PUZZLE"),
    (r'\b(block|stone|crate|boulder)\b.*\b(push|pull|move)\b', "BLOCK_PUZZLE"),
    # Switch activation
    (r'\b(step|stand|press|activate)\b.*\b(switch|plate|button)\b', "SWITCH_PUZZLE"),
    (r'\b(switch|plate|button)\b.*\b(step|stand|press|activate)\b', "SWITCH_PUZZLE"),
    # Water puzzles
    (r'\b(fill|drain|raise|lower)\b.*\b(water|level|pool)\b', "WATER_PUZZLE"),
    # Song puzzles
    (r'\b(play|blow|whistle|sing)\b.*\b(song|melody|ocarina)\b', "SONG_PUZZLE"),
    # Hookshot targets
    (r'\b(hookshot|grapple)\b.*\b(target|wood|sign|post)\b', "HOOKSHOT_PUZZLE"),
]


def load_corpus(path):
    entries = []
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line:
                entries.append(json.loads(line))
    return entries


def load_english_corpus(path):
    entries = {}
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line:
                e = json.loads(line)
                entries[e['id']] = e
    return entries


def check_real_direction_loss(es_text, eng_text):
    """Check for REAL direction loss (not phrasal verb false positives)."""
    issues = []
    
    for pattern, ptype in REAL_DIRECTION_PATTERNS:
        eng_matches = re.findall(pattern, eng_text, re.IGNORECASE)
        if eng_matches:
            # Check if the direction word is preserved or translated
            for match in eng_matches:
                if isinstance(match, tuple):
                    words = match
                else:
                    words = [match]
                for word in words:
                    # Check if Spanish has equivalent
                    direction_map = {
                        'north': 'norte', 'south': 'sur', 'east': 'este', 'west': 'oeste',
                        'left': 'izquierda', 'right': 'derecha', 'up': 'arriba', 'down': 'abajo',
                        'behind': 'detrás', 'above': 'arriba', 'below': 'abajo',
                    }
                    es_dir = direction_map.get(word.lower(), '')
                    if es_dir and es_dir not in es_text.lower() and word.lower() not in es_text.lower():
                        issues.append(f"REAL_DIRECTION_LOSS: '{word}' (context: {ptype})")
    
    return issues


def check_puzzle_hints(es_text, eng_text):
    """Check if puzzle-critical information is preserved."""
    issues = []
    
    # Number preservation check
    eng_numbers = set(re.findall(r'\b(\d+)\b', eng_text))
    es_numbers = set(re.findall(r'\b(\d+)\b', es_text))
    if eng_numbers and es_numbers:
        missing = eng_numbers - es_numbers
        if missing:
            issues.append(f"NUMBER_MISSING: {missing}")
    
    # Color preservation (some puzzles use color cues)
    color_words = ['red', 'blue', 'green', 'yellow', 'white', 'black',
                   'rojo', 'azul', 'verde', 'amarillo', 'blanco', 'negro']
    eng_colors = [c for c in color_words[:6] if c in eng_text.lower()]
    es_colors = [c for c in color_words[6:] if c in es_text.lower()]
    eng_colors_es = [c for c in color_words[6:] if c in eng_text.lower()]
    es_colors_en = [c for c in color_words[:6] if c in es_text.lower()]
    
    # Check if English colors were translated
    for ec in eng_colors:
        color_map = {'red': 'rojo', 'blue': 'azul', 'green': 'verde',
                     'yellow': 'amarillo', 'white': 'blanco', 'black': 'negro'}
        expected_es = color_map.get(ec, '')
        if expected_es and expected_es not in es_text.lower() and ec not in es_text.lower():
            issues.append(f"COLOR_NOT_TRANSLATED: '{ec}'")
    
    return issues


def check_overly_literal(es_text, eng_text):
    """Check for overly literal translations."""
    issues = []
    
    # Check for word-for-word calques
    calque_patterns = [
        (r'\ben este momento\b', "CALQUE", "Too literal for 'right now'"),
        (r'\bde hecho\b', "CALQUE", "May be over-explaining"),
        (r'\bes decir\b', "CALQUE", "May be over-explaining"),
    ]
    
    for pattern, flag, desc in calque_patterns:
        if re.search(pattern, es_text, re.IGNORECASE):
            issues.append(f"{flag}: {desc}")
    
    return issues


def check_naturalness(es_text):
    """Check if translation sounds natural."""
    issues = []
    
    # Vosotros (Spain)
    if re.search(r'\bvosotros\b', es_text, re.IGNORECASE):
        issues.append("VOSOTROS: Spain-specific")
    
    # Usted (too formal)
    if re.search(r'\busted\b', es_text, re.IGNORECASE):
        issues.append("USTED: Formal register")
    
    # Check for awkward double spaces
    if '  ' in es_text:
        issues.append("DOUBLE_SPACE")
    
    return issues


def analyze_entry(entry, english_entry):
    """Perform refined semantic QA."""
    es_text = entry.get('plain_text_es_419', '')
    eng_text = english_entry.get('plain_text', '') if english_entry else ''
    entry_id = entry.get('id', '')
    
    if not es_text:
        return {
            'id': entry_id,
            'english': eng_text[:300],
            'current_spanish': '',
            'category': english_entry.get('category', '') if english_entry else '',
            'risk_level': 'SKIPPED_NO_TEXT',
            'decision': 'SKIP',
            'confidence': 'N/A',
            'qa_flags': [],
            'corrected_spanish': '',
            'notes': 'No translatable text',
        }
    
    # Collect all QA flags
    qa_flags = []
    qa_flags.extend(check_real_direction_loss(es_text, eng_text))
    qa_flags.extend(check_puzzle_hints(es_text, eng_text))
    qa_flags.extend(check_overly_literal(es_text, eng_text))
    qa_flags.extend(check_naturalness(es_text))
    
    # Risk classification
    category = english_entry.get('category', '') if english_entry else ''
    subcategory = english_entry.get('subcategory', '') if english_entry else ''
    
    risk = 'LOW_RISK'
    if category in ['PUZZLE_HINT', 'NAV_HINT', 'TUTORIAL', 'SHOP']:
        risk = 'HIGH_RISK'
    elif category in ['ITEM_GET', 'DUNGEON_HINT']:
        risk = 'MEDIUM_RISK'
    elif any('REAL_DIRECTION_LOSS' in f or 'NUMBER_MISSING' in f for f in qa_flags):
        risk = 'HIGH_RISK'
    elif qa_flags:
        risk = 'MEDIUM_RISK'
    
    # Decision
    if qa_flags:
        if any('REAL_DIRECTION_LOSS' in f or 'NUMBER_MISSING' in f or 'FALSE_FRIEND' in f for f in qa_flags):
            decision = 'NEEDS_REVIEW'
            confidence = 'LOW'
        elif any('VOSOTROS' in f or 'USTED' in f for f in qa_flags):
            decision = 'NEEDS_REVIEW'
            confidence = 'MEDIUM'
        else:
            decision = 'ACCEPT'
            confidence = 'HIGH'
    else:
        decision = 'ACCEPT'
        confidence = 'HIGH'
    
    return {
        'id': entry_id,
        'english': eng_text[:300],
        'current_spanish': es_text[:300],
        'category': category,
        'risk_level': risk,
        'decision': decision,
        'confidence': confidence,
        'qa_flags': qa_flags,
        'corrected_spanish': '',
        'notes': '; '.join(qa_flags) if qa_flags else '',
    }


def main():
    print("Loading corpora...")
    es_entries = load_corpus(SPANISH_ROOT / "corpus" / "ocarina_messages_es_419.jsonl")
    eng_entries = load_english_corpus(LOCALIZATION_ROOT / "corpus" / "ocarina_messages_eng.jsonl")
    
    print(f"  ES: {len(es_entries)}, EN: {len(eng_entries)}")
    
    print("\nRunning refined contextual QA...")
    results = []
    risk_counts = {}
    decision_counts = {}
    flagged_count = 0
    
    for entry in es_entries:
        entry_id = entry.get('id', '')
        eng_entry = eng_entries.get(entry_id)
        result = analyze_entry(entry, eng_entry)
        results.append(result)
        
        risk = result['risk_level']
        risk_counts[risk] = risk_counts.get(risk, 0) + 1
        
        decision = result['decision']
        decision_counts[decision] = decision_counts.get(decision, 0) + 1
        
        if result['qa_flags']:
            flagged_count += 1
    
    # Write refined QA report
    qa_path = SPANISH_ROOT / "qa" / "MAIN_GAME_CONTEXTUAL_QA.csv"
    with open(qa_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=[
            'id', 'english', 'current_spanish', 'category', 'risk_level',
            'decision', 'confidence', 'qa_flags', 'corrected_spanish', 'notes'
        ])
        writer.writeheader()
        for r in results:
            row = dict(r)
            row['qa_flags'] = '|'.join(r['qa_flags']) if r['qa_flags'] else ''
            writer.writerow(row)
    
    # Write human review queue (only genuinely problematic)
    review_items = [r for r in results if r['decision'] == 'NEEDS_REVIEW']
    review_items.sort(key=lambda x: (
        {'CRITICAL_GAMEPLAY': 0, 'HIGH_RISK': 1, 'MEDIUM_RISK': 2, 'LOW_RISK': 3}.get(x['risk_level'], 4)
    ))
    
    queue_path = SPANISH_ROOT / "qa" / "HUMAN_REVIEW_QUEUE.csv"
    with open(queue_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=[
            'priority', 'domain', 'id', 'english', 'spanish', 'context',
            'reason', 'candidate_alternative', 'confidence'
        ])
        writer.writeheader()
        for r in review_items:
            priority = {
                'HIGH_RISK': 'P0_GAMEPLAY',
                'MEDIUM_RISK': 'P1_CONTEXT',
                'LOW_RISK': 'P2_TERMINOLOGY',
            }.get(r['risk_level'], 'P3_STYLE')
            
            writer.writerow({
                'priority': priority,
                'domain': 'MAIN_GAME',
                'id': r['id'],
                'english': r['english'],
                'spanish': r['current_spanish'],
                'context': r['category'],
                'reason': r['notes'],
                'candidate_alternative': '',
                'confidence': r['confidence'],
            })
    
    # Stats
    print(f"\nRefined Contextual QA:")
    print(f"  Total: {len(results)}")
    print(f"  Flagged: {flagged_count}")
    print(f"\nRisk distribution:")
    for risk, count in sorted(risk_counts.items()):
        print(f"  {risk}: {count}")
    print(f"\nDecision distribution:")
    for dec, count in sorted(decision_counts.items()):
        print(f"  {dec}: {count}")
    print(f"\nReview queue: {len(review_items)} items")
    print(f"  Written to: {queue_path}")
    
    # Flag type breakdown
    all_flags = []
    for r in results:
        for flag in r.get('qa_flags', []):
            flag_type = flag.split(':')[0] if ':' in flag else flag
            all_flags.append(flag_type)
    
    print(f"\nFlag type distribution:")
    for flag, count in Counter(all_flags).most_common():
        print(f"  {flag}: {count}")
    
    # Save stats
    stats = {
        'total': len(results),
        'translated': sum(1 for r in results if r['risk_level'] != 'SKIPPED_NO_TEXT'),
        'skipped': sum(1 for r in results if r['risk_level'] == 'SKIPPED_NO_TEXT'),
        'flagged': flagged_count,
        'accepted': decision_counts.get('ACCEPT', 0),
        'needs_review': decision_counts.get('NEEDS_REVIEW', 0),
        'risk_distribution': risk_counts,
        'flag_types': dict(Counter(all_flags)),
    }
    
    stats_path = SPANISH_ROOT / "qa" / "contextual_qa_stats.json"
    with open(stats_path, 'w', encoding='utf-8') as f:
        json.dump(stats, f, indent=2)
    
    print(f"\nStats: {stats_path}")
    return stats


if __name__ == "__main__":
    main()
