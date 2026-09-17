# RANDOMIZER ES-419 INTEGRATION PLAN

**Project**: Ship of Harkinian (Couch Edition)  
**Task**: L03A — Spanish Integration Staging + Font Encoding Package  
**Focus**: Integration architecture for 3,746 Randomizer strings, hint templates, and items  

---

## 1. Overview and Scope

The Randomizer in Ship of Harkinian is derived from `3drando` and embedded within `soh/soh/Enhancements/randomizer/`. It generates dynamic item placements, gossip stone hints, spoiler logs, and entrance shuffles.

All 3,746 Randomizer strings have been translated, certified, and packaged in:
`packages/randomizer/normalized_corpus.jsonl`  
`packages/randomizer/encoded_messages.jsonl`  

---

## 2. Structural Subdivisions

The 3,746 strings break down into six distinct functional subsystems:

1. **Gossip Stone Hints (CLEAR / OBSCURE / JOKE)**:
   - 3,659 CLEAR hints: Direct or semi-direct hint templates (`"Dicen que en #[2]# se encuentra #[1]#"`).
   - 80 JOKE hints: Parody hints preserving humor without confusing players (`"Dicen que a Malon le gusta montar a caballo..."`).
   - 7 OBSCURE hints: Poetic/mystical clues certified in L02C to preserve intentional riddle difficulty.
2. **Item Names & Dynamic Placeholders (`#[1]#`)**:
   - Master Sword, Kokiri Tunic, Hookshot, Hover Boots, etc., fully aligned with `GLOSSARY_ES_419.csv`.
3. **Location Names & Dynamic Placeholders (`#[2]#`)**:
   - All check locations (e.g. `"Kokiri Forest Mido Chest"`, `"Water Temple Boss Key Chest"`).
4. **Area Names & Region Headers**:
   - Overworld and dungeon areas matching main game terminology.
5. **Entrance Shuffle Text**:
   - Door, grotto, and warp destination labels.
6. **Ice Trap / Fool Messages**:
   - Deceptive item acquisition messages.

---

## 3. Dynamic Variable Preservation Architecture

A primary requirement certified in Task L02C and L03A is dynamic grammar safety:
- In English: `"They say that in [Location] lies [Item]"`
- In Spanish: `"Dicen que en #[2]# se encuentra #[1]#"`

### Crucial Engineering Constraints for L03B:
1. **No Hardcoded Articles Preceding `#[1]#`**:
   Because item gender in Spanish varies (`el Gancho` [masculine] vs `la Túnica` [feminine] vs `las Botas` [plural feminine]), templates must NEVER use `"se encuentra el #[1]#"`. The verbal frame `"se encuentra #[1]#"` accepts any item without grammatical collision.
2. **Location Nominative Integrity (`#[2]#`)**:
   Locations are named in canonical nominative form without prepositions attached (e.g. `"Templo del Agua"`, not `"al Templo del Agua"`). The template provides the preposition `"en #[2]#"`.

---

## 4. Integration Call Sites in Source

In `soh/soh/Enhancements/randomizer/3drando/`:
- `text.hpp`: Randomizer string catalog lookup. Add case `4: // LANGUAGE_ESP`.
- `spoiler_log.cpp`: Localized spoiler log generation.
- `hook_handlers.cpp:1036-1070`: Custom message injection for randomized checks.
- `CustomMessageManager.cpp`: Custom message table injection for Spanish entries.
