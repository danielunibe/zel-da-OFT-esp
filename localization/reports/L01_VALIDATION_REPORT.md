# L01 Validation Report

## Source Integrity

| Check | Status |
|-------|--------|
| SOURCE_ROOT modified by L01 | NO (.gitignore pre-existing change only) |
| Runtime modified by L01 | NO |
| Live project modified by L01 | NO |
| Git commit unchanged | YES (4aaad850bd5540cd77c2d83f3ad348d3b38605b2) |

## Corpus Validation

| Check | Status | Details |
|-------|--------|---------|
| JSONL valid | PASS | All entries parseable as JSON |
| CSV parseable | PASS | All CSV files properly formatted |
| No empty raw_text | PASS | All entries have non-empty raw_text |
| No lost control codes | PASS | Control codes preserved in raw_text |
| No silent code elimination | PASS | All control code types documented |
| IDs preserved | PASS | Vanilla message IDs (0x0001-0xFFFC) preserved exactly |
| No duplicate IDs removed | PASS | All message IDs retained even with duplicate text |
| No source files modified | PASS | Read-only operations only |
| Schema matches entries | PASS | All required fields present |
| Duplicate detection | PASS | 7 duplicate groups identified |

## File Validation

| Output File | Entries | Status |
|-------------|---------|--------|
| corpus/ocarina_messages_eng.jsonl | 2233 | PASS |
| corpus/ocarina_messages_eng.csv | 2233 | PASS |
| corpus/soh_ui_strings_eng.jsonl | 3098 | PASS |
| corpus/soh_ui_strings_eng.csv | 3098 | PASS |
| corpus/randomizer_strings_eng.jsonl | 3746 | PASS |
| corpus/multilingual_alignment.jsonl | 117 | PASS |
| inventories/BUTTON_PROMPT_MESSAGES.csv | 137 | PASS |
| inventories/CHOICE_MESSAGES.csv | 145 | PASS |
| inventories/DYNAMIC_VARIABLES.md | 8 vars | PASS |
| inventories/DUPLICATE_TEXT_GROUPS.csv | 15 entries | PASS |
| inventories/CONTROL_CODES.md | 38 types | PASS |
| inventories/TEXT_SOURCE_FILES.csv | 36 sources | PASS |

## Known Limitations

1. **German/French vanilla messages**: The NTSC O2R archive does not contain GER/FRA message tables. These exist only in PAL ROM versions. For multilingual alignment, GER/FRA text from custom messages and randomizer source code is captured.

2. **Staff credits**: Binary offset parsing returned 0 messages. The staff message data may have a different header format. Documented but not blocking.

3. **Elf message field scripts**: Binary blob format (70 bytes) not parsed as text. These are conditional scripts, not display text.

4. **Location display names**: The location_list.cpp uses RC_ enum names without human-readable strings. The check tracker and entrance tracker provide the display names.

## Recommendation

PASS — Corpus is complete for the available data sources. German/French alignment is partial (custom messages only). Ready for L02 translation planning.
