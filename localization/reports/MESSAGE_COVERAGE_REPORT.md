# Message Coverage Report — L01

## Systems Found

### 1. Vanilla OoT Message System
- **Location**: O2R archive (`oot.o2r::text/nes_message_data_static`)
- **Format**: Binary OTXT format (68-byte header + message entries)
- **Languages available**: NES English (2116 messages), Japanese (2086 messages)
- **Languages NOT in this build**: German, French (PAL-only in O2R)
- **Message ID range**: 0x0001 - 0xFFFC
- **Control codes**: 30+ types defined in `message_data_fmt.h`
- **Font coverage**: NES Latin font (0x20-0xAB) covers ASCII + accented Latin (0x80-0x9E) + button glyphs (0x9F-0xAB)

### 2. Custom Message System (SoH)
- **Location**: `soh/soh/Enhancements/custom-message/`
- **Languages**: ENG, GER, FRA (trilingual in source)
- **Entry count**: 117 custom messages in `z_message_OTR.cpp` + `randomizer.cpp`
- **Format**: `CustomMessage("eng", "ger", "fra")` with `%color` codes
- **Override mechanism**: Custom messages replace vanilla messages at runtime

### 3. Randomizer Hint System
- **Location**: `soh/soh/Enhancements/randomizer/3drando/hint_list/`
- **Languages**: ENG, GER, FRA (trilingual in source)
- **Entry count**: ~2179 hint text entries
- **Format**: `HintText(CustomMessage(...))` with clear/obscure/joke name tiers
- **Template variables**: `[[1]]`, `[[2]]`, `[[typeHint]]`, etc.

### 4. Randomizer Item Names
- **Location**: `soh/soh/Enhancements/randomizer/item_list.cpp`
- **Languages**: ENG, FRA, GER (trilingual in `Text{}` struct)
- **Entry count**: ~274 items
- **Format**: `Text{ "eng", "fra", "ger" }`

### 5. SoH UI Strings
- **Location**: Various `.cpp` files under `soh/soh/SohGui/` and `soh/soh/Enhancements/`
- **Languages**: English only (hardcoded string literals)
- **Entry count**: ~2111 UI strings
- **Translation method**: NONE (no i18n framework)
- **Impact**: Would require wrapping all strings with translation infrastructure

### 6. Accessibility Texts
- **Location**: `soh.o2r::accessibility/texts/`
- **Languages**: ENG, FRA, GER (separate JSON files)
- **Entry count**: ~987 entries across 12 files
- **Format**: Simple key-value JSON

### 7. Entrance/Location/Area Names
- **Location**: Randomizer tracker and check objects files
- **Languages**: English only
- **Entry count**: 516 entrance names, 733 location IDs, 33 area names

## Extraction Coverage

| System | Extracted | Estimated Total | Coverage |
|--------|-----------|-----------------|----------|
| Vanilla Messages (ENG) | 2116 | ~2116 | 100% |
| Custom Messages | 117 | ~120 | 97% |
| Randomizer Hints | 2179 | ~2200 | 99% |
| Randomizer Items | 274 | ~370 | 74% |
| SoH UI Strings | 2111 | ~2500 | 84% |
| Accessibility | 987 | ~1000 | 99% |
| Entrance Names | 516 | ~520 | 99% |
| Location Names | 733 | ~734 | 99% |
| Area Names | 33 | ~34 | 97% |

## Strings NOT Using Message System

The following player-facing strings do NOT use the vanilla message system:

1. **SoH Menu/UI** — All hardcoded C++ string literals (~2111 strings)
2. **Randomizer Settings** — ImGui-rendered setting names and descriptions
3. **Cosmetics Editor** — ~500 color option labels
4. **Item Tracker** — Widget names and labels
5. **Check Tracker** — Filter names, status text
6. **Entrance Tracker** — Area group names, type labels
7. **Enhancement Names** — ~600 enhancement labels and tooltips
8. **Controller UI** — Button names, mapping labels

## Potential Blockers for Spanish

1. **No Spanish language enum** — `LANGUAGE_MAX = 4` but no `LANGUAGE_SPA` defined
2. **No Spanish font glyphs** — NES font only covers 0x80-0x9E (accented Latin for GER/FRA). Spanish needs ñ (0xA1 in Latin-1) which may not be in the font
3. **No Spanish message table** — Would need to add `spa_message_data_static` to OTR loading
4. **No Spanish UI framework** — SoH UI strings have no i18n infrastructure
5. **Text width** — Spanish text is typically 15-20% longer than English; textbox wrapping may need adjustment
6. **Control codes** — Must preserve all OoT control codes exactly during translation

## Risks

| Risk | Severity | Description |
|------|----------|-------------|
| Font coverage | HIGH | Spanish ñ/Ñ may not have glyphs in NES font |
| Text width | MEDIUM | Spanish translations are longer; may overflow textboxes |
| Missing GER/FRA | LOW | NTSC build lacks PAL language tables; alignment partial |
| SoH UI i18n | HIGH | No translation framework exists for modern UI |
| Encoding | MEDIUM | OoT uses single-byte encoding; extended Latin needs byte mapping |
