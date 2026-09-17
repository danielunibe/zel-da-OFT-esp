# Localization Corpus Extraction Tool

## Purpose
Extracts all player-facing text from Ship of Harkinian 9.1.1 source code and OTR/O2R archives for the Couch Edition localization project.

## Usage
```bash
python extract_localization_corpus.py \
  --source-root "C:\path\to\shipwright" \
  --o2r-root "C:\path\to\runtime" \
  --output-root "C:\path\to\localization_workspace"
```

## What It Extracts
1. **Vanilla OoT messages** — from `oot.o2r` binary text resources (NES English, German, French, Japanese, Staff credits)
2. **Custom messages** — from `z_message_OTR.cpp` and `randomizer.cpp` (trilingual)
3. **Randomizer hint texts** — from `hint_list_*.cpp` files (trilingual)
4. **Randomizer item names** — from `item_list.cpp` (trilingual)
5. **Location/entrance/area names** — from tracker and location list files
6. **SoH UI strings** — from menu, enhancement, cosmetics, audio, devtools, network files
7. **Accessibility texts** — from JSON files in `soh.o2r`

## Output Structure
```
localization_workspace/
  corpus/
    ocarina_messages_eng.jsonl      # Main game messages (rich format)
    ocarina_messages_eng.csv        # Main game messages (flat view)
    soh_ui_strings_eng.jsonl        # SoH UI strings
    soh_ui_strings_eng.csv          # SoH UI strings (flat)
    randomizer_strings_eng.jsonl    # Randomizer strings
    multilingual_alignment.jsonl    # ENG/GER/FRA alignment
  inventories/
    CONTROL_CODES.md                # All control code types
    BUTTON_PROMPT_MESSAGES.csv      # Messages with button refs
    CHOICE_MESSAGES.csv             # Messages with choices
    DYNAMIC_VARIABLES.md            # Template variables
    DUPLICATE_TEXT_GROUPS.csv       # Duplicate text detection
  reports/
    extraction_stats.json           # Count statistics
```

## Requirements
- Python 3.8+
- No external dependencies (uses only stdlib)

## Read-Only Guarantee
This tool NEVER modifies:
- Source code
- OTR/O2R archives
- Runtime files
- Any file outside the output directory

## Re-producibility
Run the same command multiple times for deterministic results.
