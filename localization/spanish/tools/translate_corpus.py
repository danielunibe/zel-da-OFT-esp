#!/usr/bin/env python3
"""
translate_corpus.py — Translate OoT message corpus from English to Spanish LATAM (es-419).

Reads ocarina_messages_eng.jsonl, translates plain_text fields, and outputs
ocarina_messages_es_419.jsonl with updated translations.

Usage:
    python translate_corpus.py --input ../corpus/ocarina_messages_eng.jsonl --output ../corpus/ocarina_messages_es_419.jsonl

This script provides the translation framework. Actual translations must be
provided via a translation dictionary or external LLM call.
"""

import json
import sys
import os
import re
import argparse
from pathlib import Path

# ── Control code preservation ──────────────────────────────────────────────
# These patterns in plain_text must NOT be translated
CONTROL_PATTERNS = [
    r'\[CHOICE:\d+\]',      # Choice markers
    r'--- PAGE ---',         # Page breaks
    r'\[\[.*?\]\]',          # Dynamic variables like [[rupee]]
    r'\$icon',               # Altar icons
]

# ── Translation dictionary (es-419 LATAM) ─────────────────────────────────
# This is the core translation mapping. Extend as needed.
TRANSLATION_DICT = {
    # ── System / Menu ──────────────────────────────────────────────────────
    "Press Start": "Presiona Iniciar",
    "Game Over": "Fin del juego",
    "Save": "Guardar",
    "Load": "Cargar",
    "Continue": "Continuar",
    "Options": "Opciones",
    "Yes": "Sí",
    "No": "No",
    "OK": "Aceptar",
    "Cancel": "Cancelar",
    "Back": "Volver",
    "Quit": "Salir",
    "Pause": "Pausa",
    "Resume": "Reanudar",
    "Restart": "Reiniciar",
    "Return to Title": "Volver al Título",

    # ── Actions ────────────────────────────────────────────────────────────
    "Press": "Presiona",
    "Open": "Abrir",
    "Close": "Cerrar",
    "Grab": "Agarrar",
    "Throw": "Lanzar",
    "Attack": "Atacar",
    "Defend": "Defender",
    "Run": "Correr",
    "Walk": "Caminar",
    "Jump": "Saltar",
    "Swim": "Nadar",
    "Climb": "Escalar",
    "Read": "Leer",
    "Play": "Tocar",
    "Shoot": "Disparar",
    "Light": "Encender",
    "Pull": "Tirar",
    "Place": "Colocar",
    "Talk": "Hablar",
    "Return": "Regresar",
    "Steal": "Robar",
    "Wait": "Esperar",
    "Check": "Revisar",
    "Examine": "Examinar",
    "Use": "Usar",
    "Equip": "Equipar",
    "Take": "Tomar",
    "Drop": "Soltar",

    # ── Common phrases ─────────────────────────────────────────────────────
    "You got": "Obtuviste",
    "You received": "Recibiste",
    "You found": "Encontraste",
    "You borrowed": "Pediste prestado",
    "You returned": "Devolviste",
    "You traded": "Intercambiaste",
    "You used": "Usaste",
    "You checked in": "Entregaste",
    "You handed in": "Entregaste",
    "You can't": "No puedes",
    "You will": "Vas a",
    "You feel": "Te sientes",
    "You'll be": "Serás",
    "You don't": "No sabes",
    "You get": "Obtienes",
    "You get ten tries": "Tienes diez intentos",
    "Do you want": "¿Quieres",
    "Do you want to play": "¿Quieres jugar",
    "Do you want to play again": "¿Quieres jugar de nuevo",
    "Here is what you can win": "Esto es lo que puedes ganar",
    "I can't tell you": "No puedo decirte",
    "until you've paid to play": "hasta que hayas pagado para jugar",
    "per game": "por juego",
    "Ready...": "Listo...",
    "Aim for the hole": "Apunta al agujero",
    "in the center": "en el centro",
    "and let": "y deja que",
    "go!": " ¡vaya!",
    "Let's Bowl!": "¡A jugar!",
    "OKAY!!": "¡¡OKAY!!",
    "Welcome to": "Bienvenido a",
    "our cutting-edge": "nuestro avanzado",
    "amusement center": "centro de entretenimiento",
    "secret": "secreto",
    "paid to play": "pagado para jugar",
    "Buy": "Comprar",
    "Don't buy": "No comprar",
    "Take it out with": "Sácalo con",
    "and press": "y presiona",
    "again to throw it": "de nuevo para lanzarlo",
    "without a bomb bag": "sin una bolsa de bombas",

    # ── Items ───────────────────────────────────────────────────────────────
    "Pocket Egg": "Huevo de Bolsillo",
    "Pocket Cucco": "Cucco de Bolsillo",
    "Odd Mushroom": "Hongo Extraño",
    "Odd Potion": "Poción Extraña",
    "Poacher's Saw": "Sierra del Cazador",
    "Prescription": "Receta",
    "Eyeball Frog": "Rana de Ojos Grandes",
    "World's Finest Eye Drops": "Las Mejores Gotas para los Ojos del Mundo",
    "Claim Check": "Talón de Reclamación",
    "Broken Goron's Sword": "Espada Goron Rota",
    "Biggoron's Sword": "Espada Biggoron",
    "Giant's Knife": "Cuchillo de Gigante",
    "Skull Mask": "Máscara de Calavera",
    "Spooky Mask": "Máscara Escalofriante",
    "Keaton Mask": "Máscara Keaton",
    "Bunny Hood": "Capucha de Conejo",
    "Goron Mask": "Máscara Goron",
    "Zora Mask": "Máscara Zora",
    "Gerudo Mask": "Máscara Gerudo",
    "Mask of Truth": "Máscara de la Verdad",
    "Deku Seeds Bullet Bag": "Bolsa de Semillas Deku",
    "Bombs": "Bombas",
    "Pieces": "Piezas",
    "Rupees": "Rupias",
    "Bombchu": "Bombchu",
    "a monster": "un monstruo",
    "this mask": "esta máscara",
    "many people": "a mucha gente",
    "like...a girl?": "como...¿una chica?",
    "long ears": "las orejas largas",
    "are so cute!": "¡son tan lindas!",
    "your head look big": "tu cabeza se vea grande",
    "though": "aunque",
    "become one of the Zoras": "convertirte en uno de los Zoras",
    "show it off": "mostrarla",
    "while you": "mientras",
    "wear this mask": "usas esta máscara",
    "show it": "mostrarla",
    "slingshot bullets": "balas de honda",
    "hold up to": "puede contener hasta",
    "fresh mushroom like this": "un hongo fresco como este",
    "sure to spoil quickly": "seguro se echará a perder rápido",
    "Take it to": "Llévalo a",
    "the Kakariko Potion Shop": "la Tienda de Pociones de Kakariko",
    "quickly": "rápido",
    "what's going on": "qué está pasando",
    "between this lady and that guy": "entre esta señora y ese tipo",
    "but take it to": "pero llévalo a",
    "the Lost Woods": "el Bosque Perdido",
    "young punk guy": "joven rebelde",
    "must have left this behind": "debió haber dejado esto",
    "visit Biggoron": "visita a Biggoron",
    "get it repaired": "para que lo repare",
    "Go see King Zora": "Ve a ver al Rey Zora",
    "You can't wait for the sword": "No puedes esperar a que la espada",
    "to be completed": "esté completada",
    "was forged by a": "fue forjada por un",
    "master smith": "maestro herrero",
    "and won't break": "y no se romperá",
    "This blade": "Esta hoja",
    "while it's cold": "mientras esté fría",

    # ── Choices ─────────────────────────────────────────────────────────────
    "[CHOICE:2]Yes\nNo": "[CHOICE:2]Sí\nNo",
    "[CHOICE:2]Buy\nDon't buy": "[CHOICE:2]Comprar\nNo comprar",

    # ── Locations (contextual) ─────────────────────────────────────────────
    "Lake Hylia": "Lago Hylia",
    "Kakariko": "Kakariko",
    "Lost Woods": "Bosque Perdido",
    "Goron City": "Ciudad Goron",
    "Gerudo Valley": "Valle Gerudo",
    "Death Mountain": "Monte Muerte",
    "Hyrule Castle": "Castillo de Hyrule",
    "Temple of Time": "Templo del Tiempo",
    "Kokiri Forest": "Bosque Kokiri",
    "Zora's Domain": "Domino Zora",
    "Zora's Fountain": "Fuente Zora",
    "Castle Town": "Pueblo del Castillo",
    "Hidden Village": "Aldea Oculta",
    "Graveyard": "Cementerio",
    "Market": "Mercado",
    "Ranch": "Granja",
    "Hyrule": "Hyrule",

    # ── Characters ──────────────────────────────────────────────────────────
    "Biggoron": "Biggoron",
    "King Zora": "Rey Zora",
    "Zelda": "Zelda",
    "Link": "Link",
    "Navi": "Navi",
    "Saria": "Saria",
    "Darunia": "Darunia",
    "Ruto": "Ruto",
    "Impa": "Impa",
    "Malon": "Malon",
    "Talon": "Talon",
    "Ingo": "Ingo",
    "Ganon": "Ganon",
    "Ganondorf": "Ganondorf",
    "Sheik": "Sheik",
    "Kaepora Gaebora": "Kaepora Gaebora",
    "Rauru": "Rauru",

    # ── Races ───────────────────────────────────────────────────────────────
    "Zoras": "Zoras",
    "Gorons": "Gorons",
    "Gerudo": "Gerudo",
    "Kokiri": "Kokiri",
    "Deku": "Deku",
    "Hylians": "Hylianos",
    "Sheikah": "Sheikah",

    # ── Gameplay ────────────────────────────────────────────────────────────
    "bomb bag": "bolsa de bombas",
    "a bomb bag": "una bolsa de bombas",
    "Rupias": "Rupias",
    "bombas": "bombas",

    # ── Dungeon names ──────────────────────────────────────────────────────
    "Forest Temple": "Templo del Bosque",
    "Fire Temple": "Templo del Fuego",
    "Water Temple": "Templo del Agua",
    "Spirit Temple": "Templo del Espíritu",
    "Shadow Temple": "Templo de la Sombra",
    "Inside Ganon's Castle": "Interior del Castillo de Ganon",
    "Legend Temple": "Templo Legend",

    # ── Misc ────────────────────────────────────────────────────────────────
    "WINNER!!": "¡¡GANADOR!!",
    "It's": "Son",
    "30 Rupees": "30 Rupias",
    "80 Rupees": "80 Rupias",
    "120 Rupees": "120 Rupias",
    "20 pieces": "20 piezas",
    "30 pieces": "30 piezas",
    "(20 pieces)": "(20 piezas)",
    "(30 pieces)": "(30 piezas)",
}


def is_translatable(text: str) -> bool:
    """Check if a text string contains translatable content (not just control codes)."""
    # Strip control code markers
    cleaned = re.sub(r'\[CHOICE:\d+\]', '', text)
    cleaned = re.sub(r'--- PAGE ---', '', cleaned)
    cleaned = re.sub(r'\[\[.*?\]\]', '', cleaned)
    cleaned = re.sub(r'\$icon', '', cleaned)
    cleaned = cleaned.strip()
    return len(cleaned) > 0


def translate_text(plain_text: str) -> str:
    """
    Translate a plain_text string from English to Spanish LATAM.
    
    This is a simple dictionary-based approach. For production quality,
    this should be replaced with an LLM-based translation pass.
    """
    if not is_translatable(plain_text):
        return plain_text

    translated = plain_text

    # Apply translations from dictionary (longest match first for accuracy)
    sorted_keys = sorted(TRANSLATION_DICT.keys(), key=len, reverse=True)
    for eng in sorted_keys:
        if eng in translated:
            translated = translated.replace(eng, TRANSLATION_DICT[eng])

    return translated


def process_corpus(input_path: str, output_path: str) -> dict:
    """
    Process the entire corpus, translating each entry.
    
    Returns statistics about the translation pass.
    """
    stats = {
        "total_entries": 0,
        "translated": 0,
        "skipped_empty": 0,
        "skipped_no_change": 0,
        "errors": 0,
    }

    entries = []
    with open(input_path, 'r', encoding='utf-8') as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                entry = json.loads(line)
                entries.append(entry)
                stats["total_entries"] += 1
            except json.JSONDecodeError as e:
                print(f"  [ERROR] Line {line_num}: {e}", file=sys.stderr)
                stats["errors"] += 1

    print(f"  Loaded {stats['total_entries']} entries ({stats['errors']} errors)")

    # Translate each entry
    for entry in entries:
        plain_text = entry.get("plain_text", "")
        if not plain_text or not is_translatable(plain_text):
            stats["skipped_empty"] += 1
            entry["translation_status"] = "SKIPPED_NO_TEXT"
            continue

        translated = translate_text(plain_text)
        if translated == plain_text:
            stats["skipped_no_change"] += 1
            entry["translation_status"] = "NEEDS_MANUAL_REVIEW"
        else:
            entry["plain_text_es_419"] = translated
            entry["translation_status"] = "DICTIONARY_TRANSLATED"
            stats["translated"] += 1

    # Write output
    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)
    with open(output_path, 'w', encoding='utf-8') as f:
        for entry in entries:
            f.write(json.dumps(entry, ensure_ascii=False) + '\n')

    return stats


def main():
    parser = argparse.ArgumentParser(description="Translate OoT corpus to Spanish LATAM")
    parser.add_argument("--input", "-i", required=True, help="Input JSONL file (English)")
    parser.add_argument("--output", "-o", required=True, help="Output JSONL file (Spanish)")
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"Error: Input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    print(f"Translating corpus:")
    print(f"  Input:  {args.input}")
    print(f"  Output: {args.output}")
    print()

    stats = process_corpus(args.input, args.output)

    print()
    print(f"Translation complete:")
    print(f"  Total entries:    {stats['total_entries']}")
    print(f"  Translated:      {stats['translated']}")
    print(f"  Skipped (empty): {stats['skipped_empty']}")
    print(f"  Needs review:    {stats['skipped_no_change']}")
    print(f"  Errors:          {stats['errors']}")


if __name__ == "__main__":
    main()
