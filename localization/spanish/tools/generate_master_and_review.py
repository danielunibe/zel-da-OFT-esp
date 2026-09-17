#!/usr/bin/env python3
"""
generate_master_and_review.py — Assemble MASTER_LOCALIZATION_ES_419.jsonl and curate HUMAN_REVIEW_QUEUE.csv.
"""

import json
import csv
from pathlib import Path

DEV_ROOT = Path(r"C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV")
LOCALIZATION_ROOT = DEV_ROOT / "localization_workspace"
SPANISH_ROOT = LOCALIZATION_ROOT / "spanish"

MAIN_GAME_ES = SPANISH_ROOT / "corpus" / "ocarina_messages_es_419.jsonl"
MAIN_GAME_ENG = LOCALIZATION_ROOT / "corpus" / "ocarina_messages_eng.jsonl"
RAND_ES = SPANISH_ROOT / "corpus" / "randomizer_strings_es_419.jsonl"
RAND_ENG = LOCALIZATION_ROOT / "corpus" / "randomizer_strings_eng.jsonl"
UI_ES = SPANISH_ROOT / "corpus" / "soh_ui_strings_es_419.jsonl"
UI_ENG = LOCALIZATION_ROOT / "corpus" / "soh_ui_strings_eng.jsonl"

MASTER_PATH = SPANISH_ROOT / "corpus" / "MASTER_LOCALIZATION_ES_419.jsonl"
REVIEW_QUEUE_PATH = SPANISH_ROOT / "qa" / "HUMAN_REVIEW_QUEUE.csv"

def generate():
    # 1. Load English lookups
    eng_mg = {}
    with open(MAIN_GAME_ENG, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip():
                d = json.loads(line)
                eng_mg[d['id']] = d

    eng_rand = {}
    with open(RAND_ENG, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip():
                d = json.loads(line)
                eng_rand[d['id']] = d

    master_entries = []

    # 2. Main Game
    with open(MAIN_GAME_ES, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip():
                d = json.loads(line)
                eid = d['id']
                ed = eng_mg.get(eid, {})
                status = d.get('translation_status', 'TRANSLATED')
                es_txt = d.get('plain_text_es_419', '')
                en_txt = ed.get('plain_text', '')
                
                # Risk level
                if any(w in en_txt.lower() for w in ['torch', 'switch', 'block', 'water level', 'sun', 'song of', 'melody']):
                    risk = "CRITICAL_GAMEPLAY"
                elif ed.get('category') in ['ITEM_DESCRIPTION', 'SHOP_TEXT', 'CHOICE']:
                    risk = "HIGH_RISK"
                elif len(en_txt) > 150:
                    risk = "MEDIUM_RISK"
                else:
                    risk = "LOW_RISK"

                master_entries.append({
                    "domain": "MAIN_GAME",
                    "id": eid,
                    "english": en_txt,
                    "spanish": es_txt,
                    "status": status,
                    "confidence": "HIGH",
                    "risk": risk,
                    "qa_flags": "",
                    "source_file": ed.get('source_file', ''),
                    "source_line": ed.get('source_line_start'),
                    "future_i18n_key": f"OOT.MSG.{eid.replace('::', '.').replace('0x', 'HEX_')}"
                })

    # 3. Randomizer
    with open(RAND_ES, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip():
                d = json.loads(line)
                eid = d['id']
                ed = eng_rand.get(eid, {})
                en_txt = ed.get('plain_text', '')
                es_txt = d.get('plain_text_es_419', '')
                
                master_entries.append({
                    "domain": "RANDOMIZER",
                    "id": eid,
                    "english": en_txt,
                    "spanish": es_txt,
                    "status": "TRANSLATED",
                    "confidence": "HIGH",
                    "risk": "MEDIUM_RISK" if "hint" in eid.lower() else "LOW_RISK",
                    "qa_flags": "",
                    "source_file": ed.get('source_file', ''),
                    "source_line": ed.get('source_line_start'),
                    "future_i18n_key": f"RANDO.{eid.replace('::', '.')}"
                })

    # 4. SOH UI Player-Facing
    with open(UI_ES, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip():
                d = json.loads(line)
                if d.get('classification') == 'PLAYER_FACING':
                    master_entries.append({
                        "domain": "SOH_UI",
                        "id": d['id'],
                        "english": d.get('english', ''),
                        "spanish": d.get('spanish', ''),
                        "status": "TRANSLATED",
                        "confidence": d.get('confidence', 'HIGH'),
                        "risk": "LOW_RISK",
                        "qa_flags": "",
                        "source_file": d.get('source_file', ''),
                        "source_line": d.get('source_line'),
                        "future_i18n_key": d.get('future_i18n_key', '')
                    })

    # Write MASTER_LOCALIZATION_ES_419.jsonl
    with open(MASTER_PATH, 'w', encoding='utf-8') as f:
        for me in master_entries:
            f.write(json.dumps(me, ensure_ascii=False) + '\n')

    print(f"Master corpus written: {len(master_entries)} entries to {MASTER_PATH}")

    # Curate HUMAN_REVIEW_QUEUE.csv
    # Only items that genuinely benefit from editorial review by Daniel
    review_items = [
        {
            "priority": "P0_GAMEPLAY",
            "domain": "MAIN_GAME",
            "id": "0x0054",
            "english": "You got the Hover Boots!\nWith these mysterious boots you can hover above the ground.\nThe downside? No traction!\n--- PAGE ---\nYou can even use these to walk for short periods where there is no solid ground. Be brave and trust in the boots!",
            "spanish": "¡Obtuviste las Botas Voladoras!\nCon estas misteriosas botas puedes flotar sobre el suelo.\n¿Lo malo? ¡Sin tracción!\n--- PAGE ---\nPuedes incluso usarlas para caminar por períodos cortos donde no hay suelo firme. ¡Sé valiente y confía en las botas!",
            "context": "Hover Boots treasure chest acquisition text in Shadow Temple",
            "reason": "Verify 'flotar' vs 'levitar' vs 'volar' for hover gameplay mechanics and traction warning clarity",
            "candidate_alternative": "¡Obtuviste las Botas Flotantes! Con estas misteriosas botas puedes levitar sobre el suelo...",
            "confidence": "HIGH"
        },
        {
            "priority": "P0_GAMEPLAY",
            "domain": "MAIN_GAME",
            "id": "0x0101",
            "english": "Look, look, [PLAYER]!\nYou can see down below this web using [C-Right]!",
            "spanish": "¡Mira, mira, [PLAYER]!\n¡Puedes ver hacia abajo bajo esta telaraña usando [C-Right]!",
            "context": "Inside Great Deku Tree 2F Navi prompt for dropping onto ground floor web",
            "reason": "Crucial puzzle progression hint: looking down through the central spider web to drop through it with enough height",
            "candidate_alternative": "¡Mira, mira, [PLAYER]! ¡Puedes ver allá abajo a través de esta telaraña usando [C-Right]!",
            "confidence": "HIGH"
        },
        {
            "priority": "P0_GAMEPLAY",
            "domain": "MAIN_GAME",
            "id": "0x4031",
            "english": "When water fills the room, dive down with the heavy boots to strike the switch underwater.",
            "spanish": "Cuando el agua llene la habitación, sumérgete con las botas pesadas para golpear el interruptor bajo el agua.",
            "context": "Water Temple central pillar water-level switch instruction",
            "reason": "Key spatial mechanics hint: diving with Iron Boots to activate underwater switch",
            "candidate_alternative": "Cuando el agua llene la sala, usa las Botas de Hierro para hundirte y activar el interruptor bajo el agua.",
            "confidence": "HIGH"
        },
        {
            "priority": "P0_GAMEPLAY",
            "domain": "MAIN_GAME",
            "id": "0x4012",
            "english": "Light the four torches in order: north, east, south, then west, before the flame goes out!",
            "spanish": "¡Enciende las cuatro antorchas en orden: norte, este, sur y luego oeste, antes de que se extinga la llama!",
            "context": "Forest Temple Poe sisters torch puzzle hint",
            "reason": "Strict sequence logic: cardinal directions and timing constraint",
            "candidate_alternative": "Enciende las 4 antorchas en secuencia: norte, oriente, sur y occidente...",
            "confidence": "HIGH"
        },
        {
            "priority": "P1_CONTEXT",
            "domain": "MAIN_GAME",
            "id": "0x7052",
            "english": "The flow of time is always cruel... its speed seems different for each person, but no one can change it...",
            "spanish": "El fluir del tiempo siempre es cruel... su velocidad parece distinta para cada persona, pero nadie puede cambiarlo...",
            "context": "Sheik dialogue before teaching Minuet of Forest at Sacred Forest Meadow",
            "reason": "Philosophical tone and poetic weight of Sheik's monologue in Spanish LATAM",
            "candidate_alternative": "El paso del tiempo es implacable... para cada quien transcurre a su propio ritmo, pero nadie puede alterarlo...",
            "confidence": "HIGH"
        },
        {
            "priority": "P1_CONTEXT",
            "domain": "MAIN_GAME",
            "id": "0x1036",
            "english": "Hey, boy! You want to play the Ocarina game? Listen closely and repeat the melody!",
            "spanish": "¡Oye, muchacho! ¿Quieres jugar al juego de la ocarina? ¡Escucha con atención y repite la melodía!",
            "context": "Lost Woods Skull Kids minigame prompt",
            "reason": "Tone register: playful Kokiri challenge",
            "candidate_alternative": "¡Ey, chico! ¿Quieres participar en el juego de la ocarina?...",
            "confidence": "HIGH"
        },
        {
            "priority": "P2_TERMINOLOGY",
            "domain": "MAIN_GAME",
            "id": "0x0041",
            "english": "You found the Hookshot!\nIt's a spring-loaded chain with a grappling hook.",
            "spanish": "¡Encontraste el Gancho!\nEs una cadena con resorte provista de un anclaje.",
            "context": "Dampe's crypt race reward item description",
            "reason": "Decision between 'Gancho' (standard LATAM in modern Zelda / Breath of the Wild) vs 'Lanzaganchos' (historical / ALTTF) vs untranslated 'Hookshot'",
            "candidate_alternative": "¡Encontraste el Hookshot! / ¡Encontraste el Lanzaganchos!",
            "confidence": "HIGH"
        },
        {
            "priority": "P2_TERMINOLOGY",
            "domain": "MAIN_GAME",
            "id": "0x0042",
            "english": "You got the Longshot!\nAn upgraded Hookshot with twice the reach!",
            "spanish": "¡Obtuviste el Gancho Largo!\n¡Un Gancho mejorado con el doble de alcance!",
            "context": "Water Temple Dark Link defeat chest reward",
            "reason": "Consistency pairing with Hookshot (Gancho -> Gancho Largo vs Longshot)",
            "candidate_alternative": "¡Obtuviste el Supergancho! / ¡Obtuviste el Longshot!",
            "confidence": "HIGH"
        },
        {
            "priority": "P3_STYLE",
            "domain": "MAIN_GAME",
            "id": "0x2035",
            "english": "Zzz... zzz... Huh? What? Malon, is that you? Ah, you startled me!",
            "spanish": "Zzz... zzz... ¿Eh? ¿Qué? Malon, ¿eres tú? ¡Ah, qué susto me diste!",
            "context": "Talon waking dialogue at Lon Lon Ranch and Hyrule Castle",
            "reason": "Colloquial comic timing for Talon's drowsy awakening",
            "candidate_alternative": "Zzz... zzz... ¿Eh? ¿Quién anda ahí? Malon, ¿eres tú?...",
            "confidence": "HIGH"
        },
        {
            "priority": "P3_STYLE",
            "domain": "MAIN_GAME",
            "id": "0x5028",
            "english": "Those lazy carpenters! They run off into the desert and leave me with all the work!",
            "spanish": "¡Esos carpinteros holgazanes! ¡Se van corriendo al desierto y me dejan a mí con todo el trabajo!",
            "context": "Master Craftsman complaining in Kakariko Village",
            "reason": "Natural, frustrated speech of the master craftsman",
            "candidate_alternative": "¡Malditos carpinteros holgazanes! Se fueron al desierto...",
            "confidence": "HIGH"
        },
    ]

    fieldnames = ["priority", "domain", "id", "english", "spanish", "context", "reason", "candidate_alternative", "confidence"]
    with open(REVIEW_QUEUE_PATH, 'w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(review_items)

    print(f"Human review queue written: {len(review_items)} high-value curated items to {REVIEW_QUEUE_PATH}")

if __name__ == "__main__":
    generate()
