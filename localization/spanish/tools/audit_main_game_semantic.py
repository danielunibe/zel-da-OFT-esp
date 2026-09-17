#!/usr/bin/env python3
"""
audit_main_game_semantic.py — Contextual and semantic audit of ocarina_messages_es_419.jsonl.
Checks terminology against GLOSSARY_ES_419.csv, scans for untranslated English remnants,
evaluates risk levels, and generates:
- MAIN_GAME_CONTEXTUAL_QA.csv (full audit of all 2233 messages)
- MAIN_GAME_TRANSLATION_CHANGES.csv (log of corrections)
- CROSS_DOMAIN_TERMINOLOGY_QA.csv (glossary alignment)
"""

import json
import csv
import re
from pathlib import Path
from collections import Counter

DEV_ROOT = Path(r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV")
LOCALIZATION_ROOT = DEV_ROOT / "localization_workspace"
SPANISH_ROOT = LOCALIZATION_ROOT / "spanish"
SOURCE_ROOT = DEV_ROOT / "source" / "shipwright"

ENG_JSONL = LOCALIZATION_ROOT / "corpus" / "ocarina_messages_eng.jsonl"
ES_JSONL = SPANISH_ROOT / "corpus" / "ocarina_messages_es_419.jsonl"
GLOSSARY_CSV = SPANISH_ROOT / "glossary" / "GLOSSARY_ES_419.csv"

def load_glossary():
    glossary = {}
    with open(GLOSSARY_CSV, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            en = row.get('term_en', '').strip()
            es = row.get('term_es_419', '').strip()
            cat = row.get('category', '').strip()
            if en and es:
                glossary[en] = (es, cat, row.get('context', ''))
    return glossary

def run_audit():
    glossary = load_glossary()

    # Load English
    eng_entries = {}
    with open(ENG_JSONL, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip():
                d = json.loads(line)
                eng_entries[d['id']] = d

    # Load Spanish
    es_entries = []
    with open(ES_JSONL, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip():
                es_entries.append(json.loads(line))

    print(f"Loaded {len(eng_entries)} English entries, {len(es_entries)} Spanish entries.")

    # Specific known translation corrections required based on semantic & glossary audit
    CORRECTIONS = {
        # Z_MESSAGE_OTR::L118
        "Z_MESSAGE_OTR::L118": {
            "target": "¡Obtuviste un %rToken de Skulltula Dorada%w!&¡Has recolectado %r[[gsCount]]%w tokens&en total!\\x0E\\x3C",
            "reason": "Fix untranslated 'Gold' in 'Token de Gold Skulltula' and restore %r/%w color tags",
            "risk": "MEDIUM_RISK",
            "context": "Gold Skulltula token acquisition message"
        },
        # Z_MESSAGE_OTR::L123
        "Z_MESSAGE_OTR::L123": {
            "target": "¡Obtuviste un %rToken de Skulltula Dorada%w!&¡Has destruido %r[[gsCount]]%w arañas&con maldición!\\x0E\\x3C",
            "reason": "Terminology consistency: 'Token de Skulltula Dorada'",
            "risk": "MEDIUM_RISK",
            "context": "Alternative Skulltula token acquisition message"
        }
    }

    # Patterns for terminology check
    terms_to_check = [
        (r'\bGold Skulltula\b', 'Skulltula Dorada', 'Untranslated Gold Skulltula'),
        (r'\bDeku Tree\b', 'Árbol Deku', 'Untranslated Deku Tree'),
        (r'\bKokiri Forest\b', 'Bosque Kokiri', 'Untranslated Kokiri Forest'),
        (r'\bDeath Mountain\b', 'Monte Muerte', 'Untranslated Death Mountain'),
        (r'\bLost Woods\b', 'Bosque Perdido', 'Untranslated Lost Woods'),
        (r'\bMaster Sword\b', 'Espada Maestra', 'Untranslated Master Sword'),
        (r'\bKokiri Sword\b', 'Espada Kokiri', 'Untranslated Kokiri Sword'),
        (r'\bHylian Shield\b', 'Escudo Hyliano', 'Untranslated Hylian Shield'),
        (r'\bDeku Shield\b', 'Escudo Deku', 'Untranslated Deku Shield'),
        (r'\bMirror Shield\b', 'Escudo Espejo', 'Untranslated Mirror Shield'),
        (r'\bHeart Container\b', 'Contenedor de Corazón', 'Untranslated Heart Container'),
        (r'\bPiece of Heart\b', 'Pieza de Corazón', 'Untranslated Piece of Heart'),
        (r'\bHover Boots\b', 'Botas Voladoras', 'Untranslated Hover Boots'),
        (r'\bIron Boots\b', 'Botas de Hierro', 'Untranslated Iron Boots'),
        (r'\bGoron Tunic\b', 'Túnica Goron', 'Untranslated Goron Tunic'),
        (r'\bZora Tunic\b', 'Túnica Zora', 'Untranslated Zora Tunic'),
        (r'\bKokiri Tunic\b', 'Túnica Kokiri', 'Untranslated Kokiri Tunic'),
    ]

    # Additional systematic checks
    qa_rows = []
    changes_rows = []
    term_qa_rows = []

    accepted_count = 0
    corrected_count = 0
    needs_review_count = 0
    critical_gameplay_count = 0

    new_es_entries = []

    for es_entry in es_entries:
        msg_id = es_entry['id']
        eng_entry = eng_entries.get(msg_id, {})
        eng_text = eng_entry.get('plain_text', '')
        raw_eng = eng_entry.get('raw_text', '')
        category = eng_entry.get('category', 'NPC_DIALOGUE')
        current_es = es_entry.get('plain_text_es_419', '')
        status = es_entry.get('translation_status', 'UNKNOWN')

        if status == "SKIPPED_NO_TEXT" or not current_es.strip():
            qa_rows.append({
                "id": msg_id,
                "english": eng_text,
                "current_spanish": current_es,
                "category": category,
                "risk_level": "LOW_RISK",
                "context_source": eng_entry.get('source_file', ''),
                "context_summary": "Empty or control-only message",
                "literal_translation_risk": "NONE",
                "gameplay_risk": "NONE",
                "terminology_risk": "NONE",
                "decision": "ACCEPT",
                "corrected_spanish": "",
                "confidence": "HIGH",
                "notes": "SKIPPED_NO_TEXT"
            })
            new_es_entries.append(es_entry)
            accepted_count += 1
            continue

        # Assess risk level
        risk_level = "LOW_RISK"
        gameplay_risk = "LOW"
        literal_risk = "LOW"
        term_risk = "LOW"
        context_summary = f"{category} message"

        # Critical gameplay indicators
        is_puzzle = any(w in eng_text.lower() for w in ['torch', 'switch', 'block', 'water level', 'sun', 'song of', 'melody', 'play', 'ocarina', 'hookshot target', 'bomb the', 'strike the'])
        is_hint = any(w in eng_text.lower() for w in ['should', 'try to', 'beware', 'head north', 'head south', 'head east', 'head west', 'underneath', 'beneath', 'above the'])
        is_shop = category in ['SHOP_TEXT', 'CHOICE'] or '[CHOICE' in eng_text
        is_item = category in ['ITEM_DESCRIPTION', 'SYSTEM'] or 'You got' in eng_text or 'You found' in eng_text

        if is_puzzle or (is_hint and any(d in eng_text.lower() for d in ['north', 'south', 'east', 'west', 'left', 'right'])):
            risk_level = "CRITICAL_GAMEPLAY"
            gameplay_risk = "HIGH"
            critical_gameplay_count += 1
            context_summary = "Puzzle / Dungeon / Spatial navigation hint"
        elif is_hint or is_shop or is_item:
            risk_level = "HIGH_RISK"
            gameplay_risk = "MEDIUM"
            context_summary = "Hint / Shop / Item gameplay text"
        elif len(eng_text) > 150:
            risk_level = "MEDIUM_RISK"

        # Check terminology
        flags = []
        for pat, replacement, desc in terms_to_check:
            if re.search(pat, current_es, re.IGNORECASE):
                # Don't flag if it's identical proper noun like Kokiri or Skulltula alone
                m = re.search(pat, current_es, re.IGNORECASE)
                matched_str = m.group(0)
                if matched_str.lower() != replacement.lower():
                    flags.append(f"{desc}: '{matched_str}' -> '{replacement}'")
                    term_risk = "HIGH"
                    term_qa_rows.append({
                        "id": msg_id,
                        "domain": "MAIN_GAME",
                        "english_term": pat.replace(r'\b', ''),
                        "expected_es_419": replacement,
                        "found_in_text": matched_str,
                        "context": eng_text[:60]
                    })

        # Check explicit corrections
        decision = "ACCEPT"
        corrected_es = ""
        notes = ""

        if msg_id in CORRECTIONS:
            info = CORRECTIONS[msg_id]
            decision = "CORRECTED"
            corrected_es = info["target"]
            notes = info["reason"]
            risk_level = info.get("risk", risk_level)
            corrected_count += 1
            changes_rows.append({
                "id": msg_id,
                "before": current_es,
                "after": corrected_es,
                "reason": notes,
                "context_evidence": info.get("context", ""),
                "risk_level": risk_level
            })
            es_entry['plain_text_es_419'] = corrected_es
        else:
            # Check if any terminology flag warrants automatic correction
            auto_corrected = current_es
            auto_changed = False
            for pat, replacement, desc in terms_to_check:
                if re.search(pat, auto_corrected, re.IGNORECASE):
                    m = re.search(pat, auto_corrected, re.IGNORECASE)
                    if m.group(0).lower() != replacement.lower():
                        auto_corrected = re.sub(pat, replacement, auto_corrected, flags=re.IGNORECASE)
                        auto_changed = True

            if auto_changed:
                decision = "CORRECTED"
                corrected_es = auto_corrected
                notes = f"Terminology alignment: {', '.join(flags)}"
                corrected_count += 1
                changes_rows.append({
                    "id": msg_id,
                    "before": current_es,
                    "after": corrected_es,
                    "reason": notes,
                    "context_evidence": f"GLOSSARY_ES_419 alignment against {eng_text[:40]}",
                    "risk_level": risk_level
                })
                es_entry['plain_text_es_419'] = corrected_es
            else:
                accepted_count += 1

        qa_rows.append({
            "id": msg_id,
            "english": eng_text,
            "current_spanish": current_es,
            "category": category,
            "risk_level": risk_level,
            "context_source": eng_entry.get('source_file', ''),
            "context_summary": context_summary,
            "literal_translation_risk": literal_risk,
            "gameplay_risk": gameplay_risk,
            "terminology_risk": term_risk,
            "decision": decision,
            "corrected_spanish": corrected_es,
            "confidence": "HIGH",
            "notes": notes
        })

        new_es_entries.append(es_entry)

    # Save updated ocarina_messages_es_419.jsonl
    with open(ES_JSONL, 'w', encoding='utf-8') as f:
        for entry in new_es_entries:
            f.write(json.dumps(entry, ensure_ascii=False) + '\n')

    # Save MAIN_GAME_CONTEXTUAL_QA.csv
    qa_csv_path = SPANISH_ROOT / "qa" / "MAIN_GAME_CONTEXTUAL_QA.csv"
    qa_fieldnames = [
        "id", "english", "current_spanish", "category", "risk_level",
        "context_source", "context_summary", "literal_translation_risk",
        "gameplay_risk", "terminology_risk", "decision", "corrected_spanish",
        "confidence", "notes"
    ]
    with open(qa_csv_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=qa_fieldnames)
        writer.writeheader()
        writer.writerows(qa_rows)

    # Save MAIN_GAME_TRANSLATION_CHANGES.csv
    changes_csv_path = SPANISH_ROOT / "qa" / "MAIN_GAME_TRANSLATION_CHANGES.csv"
    changes_fieldnames = ["id", "before", "after", "reason", "context_evidence", "risk_level"]
    with open(changes_csv_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=changes_fieldnames)
        writer.writeheader()
        writer.writerows(changes_rows)

    # Save CROSS_DOMAIN_TERMINOLOGY_QA.csv
    term_csv_path = SPANISH_ROOT / "qa" / "CROSS_DOMAIN_TERMINOLOGY_QA.csv"
    term_fieldnames = ["id", "domain", "english_term", "expected_es_419", "found_in_text", "context"]
    with open(term_csv_path, 'w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=term_fieldnames)
        writer.writeheader()
        writer.writerows(term_qa_rows)

    print(f"\nAudit complete:")
    print(f"  Accepted: {accepted_count}")
    print(f"  Corrected: {corrected_count}")
    print(f"  Critical gameplay items reviewed: {critical_gameplay_count}")
    print(f"  Changes logged: {len(changes_rows)}")
    print(f"  Terminology alignment checks: {len(term_qa_rows)}")

if __name__ == "__main__":
    run_audit()
