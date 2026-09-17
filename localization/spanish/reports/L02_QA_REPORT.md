# QA Report — L02 Spanish Corpus Translation

## Summary

| Metric | Value |
|--------|-------|
| Total entries | 2,233 |
| Translated | 2,170 (97.2%) |
| Skipped (no text) | 63 (2.8%) |
| QA Issues | 0 |

## Translation Statistics

- **Source**: `ocarina_messages_eng.jsonl` (2,233 vanilla messages)
- **Output**: `ocarina_messages_es_419.jsonl` (2,233 entries)
- **Locale**: es-419 (Español Latinoamericano Neutral)
- **Register**: Informal tú
- **Processing**: 23 parallel batch translations via subagents

## QA Checks Performed

1. **Line count consistency**: All translations preserve original line structure
2. **Control code preservation**: `[CHOICE:2]`, `--- PAGE ---`, `[[var]]`, `$icon`, `[PLAYER]` markers preserved
3. **English remnants**: No untranslated English articles/prepositions detected
4. **Encoding**: All entries valid UTF-8 JSON
5. **ID integrity**: All message IDs preserved from source

## Skipped Entries

63 entries skipped (SKIPPED_NO_TEXT) — these contained:
- Hex placeholder codes (e.g., "01a2", "01a4")
- Empty/null text
- Code-only entries with no translatable content

## Sample Quality

### NPC Dialogue (0x0190-0x0198)
```
EN: "This passage is all twisted!"
ES: "¡Este pasaje está todo retorcido!"

EN: "Be careful of the shadows of monsters hanging from the ceiling."
ES: "Cuidado con las sombras de los monstruos que cuelgan del techo."

EN: "There's a treasure chest here."
ES: "Hay un cofre del tesoro aquí."

EN: "This torch is lit... that means..."
ES: "Esta antorcha está encendida... eso significa..."

EN: "Watch out, [PLAYER]! The ceiling is falling!"
ES: "[PLAYER], ¡cuidado! ¡El techo se está cayendo!"
```

### Randomizer Messages
```
EN: "You got an Infinite Bomb Bag!"
ES: "¡Obtuviste una Bolsa de Bombas Infinita!"

EN: "You now have infinite sticks!"
ES: "¡Ahora tienes infinitos Palos Deku!"

EN: "You found the Skeleton Key!"
ES: "¡Encontraste la Llave Esqueleto!"
```

## Translation Approach

- **Batch processing**: 2,233 entries split into 23 batches of 100 (last: 33)
- **Parallel execution**: Multiple subagents translating simultaneously
- **Glossary-driven**: Consistent terminology across all batches
- **LATAM-neutral**: No country-specific slang, tú register throughout

## Known Limitations

1. **Font dependency**: Ñ, í, ó, ú, ñ, ¡, ¿ require font modification (see `SPANISH_GLYPH_AUDIT.md`)
2. **Dictionary approach**: Some entries used partial dictionary translation (batch 1 initial pass) — merged output uses subagent translations
3. **SoH UI strings**: Not yet translated (3,098 entries — separate corpus)
4. **Randomizer strings**: Not yet translated (3,746 entries — separate corpus)

## Next Steps

1. Translate SoH UI strings corpus
2. Translate randomizer strings corpus
3. Font modification (source change — outside L02 scope)
4. Integration testing with translated corpus
