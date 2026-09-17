# L01 Report — Complete Localization Corpus Extraction

## Task Summary

**Objective**: Extract, inventory, normalize, and document all localizable text content from Ship of Harkinian 9.1.1 / Ocarina of Time for the Couch Edition localization project.

**Status**: PASS

**Source Version**: Ship of Harkinian 9.1.1 (Copper Bravo)
**Source Commit**: 4aaad850bd5540cd77c2d83f3ad348d3b38605b2

## Results

### Corpus Generated

| Corpus | Path | Entries |
|--------|------|---------|
| Main Game Messages | `corpus/ocarina_messages_eng.jsonl` | 2,233 |
| SoH UI Strings | `corpus/soh_ui_strings_eng.jsonl` | 3,098 |
| Randomizer Strings | `corpus/randomizer_strings_eng.jsonl` | 3,746 |
| Multilingual Alignment | `corpus/multilingual_alignment.jsonl` | 117 |

**Total unique entries**: 7,300 (after deduplication)

### Inventories Generated

| Inventory | Path | Count |
|-----------|------|-------|
| Text Source Files | `inventories/TEXT_SOURCE_FILES.csv` | 36 sources |
| Control Codes | `inventories/CONTROL_CODES.md` | 38 code types |
| Button Prompt Messages | `inventories/BUTTON_PROMPT_MESSAGES.csv` | 137 messages |
| Choice Messages | `inventories/CHOICE_MESSAGES.csv` | 145 messages |
| Dynamic Variables | `inventories/DYNAMIC_VARIABLES.md` | 8 unique variables |
| Duplicate Text Groups | `inventories/DUPLICATE_TEXT_GROUPS.csv` | 7 groups (15 entries) |

### Reports Generated

| Report | Path |
|--------|------|
| Validation Report | `reports/L01_VALIDATION_REPORT.md` |
| Message Coverage Report | `reports/MESSAGE_COVERAGE_REPORT.md` |
| Localization Readiness Report | `reports/LOCALIZATION_READINESS_REPORT.md` |
| Extraction Stats | `reports/extraction_stats.json` |

### Tools Generated

| Tool | Path | Description |
|------|------|-------------|
| Extraction Script | `tools/extract_localization_corpus.py` | Reproducible extraction tool |
| Documentation | `tools/README.md` | Usage instructions |
| Schema | `schemas/localization_entry.schema.json` | Entry JSON schema |

## Key Findings

1. **2,116 vanilla English messages** successfully extracted from O2R binary format
2. **2,179 randomizer hint texts** with trilingual (ENG/GER/FRA) support in source
3. **274 randomizer item names** with trilingual support
4. **2,111 SoH UI strings** are hardcoded C++ literals with NO translation framework
5. **137 messages contain button glyphs** (A, B, C, L, R, Z, etc.)
6. **145 messages contain player choices** (Yes/No, Buy/Don't buy)
7. **8 unique template variables** used across messages
8. **German/French vanilla messages** are NOT in the NTSC O2R (PAL-only)

## Spanish Readiness Assessment

| Factor | Status |
|--------|--------|
| Character encoding | PARTIAL — accented Latin mapped but ñ unverified |
| Font glyph coverage | UNKNOWN — needs verification in NES font |
| Message system | READY — can add spa_message_data_static |
| SoH UI translation | BLOCKED — no i18n framework |
| Randomizer text | READY — already trilingual-capable |
| Vanilla text | READY — can add Spanish message table |

## Acceptance Criteria

| Criterion | Met? |
|-----------|------|
| SOURCE_ROOT not modified | YES |
| Runtime not modified | YES |
| Live project not modified | YES |
| Corpus ENG generated | YES |
| IDs preserved | YES |
| RAW_TEXT preserved | YES |
| PLAIN_TEXT generated | YES |
| Control codes inventoried | YES |
| Button prompts identified | YES |
| Choices identified | YES |
| Variables identified | YES |
| Duplicates marked | YES |
| SoH UI inventoried separately | YES |
| Randomizer strings separate | YES |
| ENG/GER/FRA aligned | PARTIAL |
| Extractor re-producible | YES |
| No translation performed | YES |
| No voice generation | YES |
| JSONL valid | YES |
| CSV valid | YES |
| Validation report exists | YES |

## Next Recommended Action

STOP. Await review before Localization Task L02.

DO NOT TRANSLATE.
DO NOT MODIFY SOURCE.
DO NOT START L02.
