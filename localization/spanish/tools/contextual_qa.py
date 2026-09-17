#!/usr/bin/env python3
"""
contextual_qa.py — Contextual Semantic QA for OoT Spanish translations.

Analyzes all translated entries for semantic issues beyond structural correctness.
Flags entries for human review based on risk categories.
"""

import json
import csv
import os
import re
from pathlib import Path

DEV_ROOT = Path(r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV")
LOCALIZATION_ROOT = DEV_ROOT / "localization_workspace"
SPANISH_ROOT = LOCALIZATION_ROOT / "spanish"

# ── Risk keywords ──────────────────────────────────────────────────────────
# Messages containing these patterns are inherently higher risk

PUZZLE_KEYWORDS = [
    "light", "fire", "water", "push", "pull", "block", "switch", "door",
    "torch", "wall", "floor", "ceiling", "hookshot", "bomb", "arrow",
    "crystal", "sun", "moon", "time", "song", "play", "warp", "temple",
    "stone", "statue", "chest", "key", "compass", "map", "medallion",
    "spiritual", "sage", "pedestal", "altar", "triforce",
    # Spanish
    "luz", "fuego", "agua", "empujar", "tirar", "bloque", "interruptor",
    "antorcha", "pared", "suelo", "techo", "anzuelo", "bomba", "flecha",
    "cristal", "sol", "luna", "tiempo", "canción", "teletransporte",
    "piedra", "estatua", "cofre", "llave", "brújula", "mapa", "medallón",
    "espíritu", "sabio", "pedestal", "altar", "trifuerza",
]

DIRECTION_KEYWORDS = [
    "north", "south", "east", "west", "left", "right", "up", "down",
    "behind", "above", "below", "near", "far", "next to", "between",
    "norte", "sur", "este", "oeste", "izquierda", "derecha",
    "arriba", "abajo", "detrás", "cerca", "lejos", "junto", "entre",
]

HINT_KEYWORDS = [
    "hint", "secret", "try", "look", "search", "find", "hidden",
    "pista", "secreto", "intentar", "buscar", "encontrar", "oculto",
    "should", "might", "could", "must", "need",
    "deberías", "podrías", "podría", "necesitas", "tienes que",
]

ITEM_KEYWORDS = [
    "sword", "shield", "bow", "arrow", "bomb", "hookshot", "boomerang",
    "ocarina", "tunic", "boots", "gauntlets", "wallet", "heart",
    "magic", "potion", "bottle", "mask", "bean", "skulltula",
    "espada", "escudo", "arco", "flecha", "bomba", "anzuelo", "bumerán",
    "ocarina", "túnica", "botas", "guantes", "billetera", "corazón",
    "magia", "poción", "botella", "máscara", "frijol", "skulltula",
]

SHOP_KEYWORDS = [
    "buy", "sell", "price", "rupee", "shop", "store", "cost",
    "comprar", "vender", "precio", "rupia", "tienda",
]

CHOICE_KEYWORDS = [
    "yes", "no", "ok", "cancel", "sí", "no", "aceptar", "cancelar",
]

# ── Overly literal patterns ────────────────────────────────────────────────
LITERAL_PATTERNS = [
    (r"\bnombre propio\b", "FORMAL_EXPLANATION"),
    (r"\bcomo se\b", "OVER_EXPLANATION"),
    (r"\bes decir\b", "ADDED_EXPLANATION"),
    (r"\bde hecho\b", "ADDED_EXPLANATION"),
    (r"\ben otras palabras\b", "ADDED_EXPLANATION"),
    (r"\btiene que\b", "POSSIBLE_LITERAL"),
]

# ── False friends ──────────────────────────────────────────────────────────
FALSE_FRIENDS = [
    (r"\bembarazada\b", "FALSE_FRIEND", "Means 'pregnant' in Spanish, not 'embarrassed'"),
    (r"\bactualmente\b", "FALSE_FRIEND", "Means 'currently' in Spanish, not 'actually'"),
    (r"\brealmente\b", "CHECK_CONTEXT", "Means 'really/truly' — verify context"),
    (r"\beventualmente\b", "FALSE_FRIEND", "Means 'eventually' in Spanish, not 'eventually'"),
    (r"\bsensible\b", "CHECK_CONTEXT", "Means 'sensitive' in Spanish, not 'sensible'"),
    (r"\bnotable\b", "CHECK_CONTEXT", "Means 'noteworthy' in Spanish, not 'notable'"),
]


def load_corpus(path):
    """Load a JSONL corpus."""
    entries = []
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line:
                entries.append(json.loads(line))
    return entries


def load_english_corpus(path):
    """Load English corpus and index by ID."""
    entries = {}
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line:
                e = json.loads(line)
                entries[e['id']] = e
    return entries


def classify_risk(entry, english_entry=None):
    """Classify the semantic risk level of a message."""
    text = entry.get('plain_text_es_419', '').lower()
    eng_text = ''
    if english_entry:
        eng_text = english_entry.get('plain_text', '').lower()
    
    combined = text + ' ' + eng_text
    
    # Check for critical gameplay patterns
    puzzle_matches = sum(1 for kw in PUZZLE_KEYWORDS if kw in combined)
    direction_matches = sum(1 for kw in DIRECTION_KEYWORDS if kw in combined)
    hint_matches = sum(1 for kw in HINT_KEYWORDS if kw in combined)
    item_matches = sum(1 for kw in ITEM_KEYWORDS if kw in combined)
    shop_matches = sum(1 for kw in SHOP_KEYWORDS if combined)
    
    # Decision matrix
    if puzzle_matches >= 3 or (puzzle_matches >= 2 and direction_matches >= 1):
        return "CRITICAL_GAMEPLAY"
    elif puzzle_matches >= 2 or hint_matches >= 2 or shop_matches >= 2:
        return "HIGH_RISK"
    elif puzzle_matches >= 1 or direction_matches >= 1 or hint_matches >= 1 or item_matches >= 1:
        return "MEDIUM_RISK"
    else:
        return "LOW_RISK"


def check_literal_translation(es_text, eng_text):
    """Check if translation appears overly literal."""
    issues = []
    
    for pattern, flag in LITERAL_PATTERNS:
        if re.search(pattern, es_text, re.IGNORECASE):
            issues.append(flag)
    
    # Check for false friends
    for pattern, flag, desc in FALSE_FRIENDS:
        if re.search(pattern, es_text, re.IGNORECASE):
            issues.append(f"{flag}: {desc}")
    
    return issues


def check_terminology_consistency(es_text, entry_id):
    """Check for terminology inconsistencies."""
    issues = []
    
    # Hookshot variations
    hookshot_forms = ['anzuelo', 'gancho', 'hookshot']
    found_hookshot = [f for f in hookshot_forms if f in es_text.lower()]
    if len(found_hookshot) > 1:
        issues.append(f"HOOKSHOT_VARIATION: found {found_hookshot}")
    
    # Shield variations
    shield_forms = ['escudo', 'shield']
    found_shield = [f for f in shield_forms if f in es_text.lower()]
    if len(found_shield) > 1:
        issues.append(f"SHIELD_VARIATION: found {found_shield}")
    
    return issues


def check_puzzle_integrity(es_text, eng_text):
    """Check if puzzle-critical information is preserved."""
    issues = []
    
    # Check for number preservation
    eng_numbers = re.findall(r'\b(\d+)\b', eng_text)
    es_numbers = re.findall(r'\b(\d+)\b', es_text)
    if eng_numbers and es_numbers:
        if set(eng_numbers) != set(es_numbers):
            issues.append(f"NUMBER_MISMATCH: ENG={eng_numbers} ES={es_numbers}")
    
    # Check for direction preservation
    direction_map = {
        'north': 'norte', 'south': 'sur', 'east': 'este', 'west': 'oeste',
        'left': 'izquierda', 'right': 'derecha', 'up': 'arriba', 'down': 'abajo',
    }
    for eng_dir, es_dir in direction_map.items():
        if eng_dir in eng_text and es_dir not in es_text and eng_dir not in es_text:
            issues.append(f"POSSIBLE_DIRECTION_LOSS: '{eng_dir}' not found in translation")
    
    return issues


def check_naturalness(es_text):
    """Check if the translation sounds natural in LATAM Spanish."""
    issues = []
    
    # Check for vosotros (Spain-specific)
    vosotros_patterns = [r'\bvosotros\b', r'\bveis\b', r'\báis\b', r'\béis\b', r'\bís\b']
    for pat in vosotros_patterns:
        if re.search(pat, es_text, re.IGNORECASE):
            issues.append("VOSOTROS_USED: Spain-specific conjugation detected")
            break
    
    # Check for usted (too formal)
    if re.search(r'\busted\b', es_text, re.IGNORECASE):
        issues.append("USTED_USED: Formal register detected (should be tú)")
    
    return issues


def analyze_entry(entry, english_entry):
    """Perform full contextual QA on a single entry."""
    es_text = entry.get('plain_text_es_419', '')
    eng_text = english_entry.get('plain_text', '') if english_entry else ''
    entry_id = entry.get('id', '')
    
    if not es_text:
        return {
            'id': entry_id,
            'english': eng_text[:200],
            'current_spanish': '',
            'category': english_entry.get('category', '') if english_entry else '',
            'risk_level': 'SKIPPED_NO_TEXT',
            'decision': 'SKIP',
            'confidence': 'N/A',
            'qa_flags': [],
            'corrected_spanish': '',
            'notes': 'No translatable text',
        }
    
    # Risk classification
    risk = classify_risk(entry, english_entry)
    
    # Collect all QA flags
    qa_flags = []
    qa_flags.extend(check_literal_translation(es_text, eng_text))
    qa_flags.extend(check_terminology_consistency(es_text, entry_id))
    qa_flags.extend(check_puzzle_integrity(es_text, eng_text))
    qa_flags.extend(check_naturalness(es_text))
    
    # Decision logic
    if qa_flags:
        if any('CRITICAL' in f or 'DIRECTION_LOSS' in f or 'NUMBER_MISMATCH' in f for f in qa_flags):
            decision = 'NEEDS_REVIEW'
            confidence = 'LOW'
        elif any('VOSOTROS' in f or 'USTED' in f or 'FALSE_FRIEND' in f for f in qa_flags):
            decision = 'NEEDS_REVIEW'
            confidence = 'MEDIUM'
        else:
            decision = 'ACCEPT'
            confidence = 'HIGH'
    elif risk in ['CRITICAL_GAMEPLAY', 'HIGH_RISK']:
        decision = 'NEEDS_REVIEW'
        confidence = 'MEDIUM'
    else:
        decision = 'ACCEPT'
        confidence = 'HIGH'
    
    return {
        'id': entry_id,
        'english': eng_text[:200],
        'current_spanish': es_text[:200],
        'category': english_entry.get('category', '') if english_entry else '',
        'risk_level': risk,
        'decision': decision,
        'confidence': confidence,
        'qa_flags': qa_flags,
        'corrected_spanish': '',
        'notes': '; '.join(qa_flags) if qa_flags else '',
    }


def main():
    # Load corpora
    print("Loading corpora...")
    es_entries = load_corpus(SPANISH_ROOT / "corpus" / "ocarina_messages_es_419.jsonl")
    eng_entries = load_english_corpus(LOCALIZATION_ROOT / "corpus" / "ocarina_messages_eng.jsonl")
    
    print(f"  ES entries: {len(es_entries)}")
    print(f"  EN entries: {len(eng_entries)}")
    
    # Run contextual QA
    print("\nRunning contextual QA...")
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
    
    # Write contextual QA report
    qa_path = SPANISH_ROOT / "qa" / "MAIN_GAME_CONTEXTUAL_QA.csv"
    os.makedirs(qa_path.parent, exist_ok=True)
    
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
    
    print(f"\nContextual QA complete:")
    print(f"  Total entries: {len(results)}")
    print(f"  Flagged entries: {flagged_count}")
    print(f"\nRisk distribution:")
    for risk, count in sorted(risk_counts.items()):
        print(f"  {risk}: {count}")
    print(f"\nDecision distribution:")
    for dec, count in sorted(decision_counts.items()):
        print(f"  {dec}: {count}")
    print(f"\nQA report written to: {qa_path}")
    
    # Write human review queue (only entries needing review)
    review_queue = [r for r in results if r['decision'] == 'NEEDS_REVIEW']
    review_queue.sort(key=lambda x: (
        {'CRITICAL_GAMEPLAY': 0, 'HIGH_RISK': 1, 'MEDIUM_RISK': 2, 'LOW_RISK': 3}.get(x['risk_level'], 4)
    ))
    
    queue_path = SPANISH_ROOT / "qa" / "HUMAN_REVIEW_QUEUE.csv"
    with open(queue_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=[
            'priority', 'domain', 'id', 'english', 'spanish', 'context',
            'reason', 'candidate_alternative', 'confidence'
        ])
        writer.writeheader()
        for r in review_queue:
            priority = {
                'CRITICAL_GAMEPLAY': 'P0_GAMEPLAY',
                'HIGH_RISK': 'P1_CONTEXT',
                'MEDIUM_RISK': 'P2_TERMINOLOGY',
                'LOW_RISK': 'P3_STYLE',
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
    
    print(f"\nHuman review queue: {len(review_queue)} items")
    print(f"  Written to: {queue_path}")
    
    # Summary stats
    stats = {
        'total': len(results),
        'translated': sum(1 for r in results if r['risk_level'] != 'SKIPPED_NO_TEXT'),
        'skipped': sum(1 for r in results if r['risk_level'] == 'SKIPPED_NO_TEXT'),
        'flagged': flagged_count,
        'accepted': decision_counts.get('ACCEPT', 0),
        'needs_review': decision_counts.get('NEEDS_REVIEW', 0),
        'risk_distribution': risk_counts,
        'decision_distribution': decision_counts,
    }
    
    stats_path = SPANISH_ROOT / "qa" / "contextual_qa_stats.json"
    with open(stats_path, 'w', encoding='utf-8') as f:
        json.dump(stats, f, indent=2)
    
    print(f"\nStats written to: {stats_path}")
    return stats


if __name__ == "__main__":
    main()
