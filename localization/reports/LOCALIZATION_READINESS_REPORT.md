# Localization Readiness Report — L01

## Overall Assessment: PARTIALLY_READY

## Detailed Analysis

### Corpus Completeness
- **Status**: ADEQUATE
- **Evidence**: 2233 vanilla messages + 3098 SoH UI + 3746 randomizer = 9077 total entries extracted
- **Gap**: ~5% of items not captured due to format differences

### Message ID Identification
- **Status**: GOOD
- **Evidence**: All vanilla message IDs preserved as hex (0x0001-0xFFFC). Custom message IDs use stable synthetic format.
- **Gap**: Some randomizer messages use line-number-based IDs (less stable across versions)

### Control Code Reconstruction
- **Status**: GOOD
- **Evidence**: 38 control code types documented. All OoT control bytes (0x01-0x1F) mapped. Custom message format codes (& ^ %x ##) documented.
- **Gap**: None significant

### Page Break Detection
- **Status**: GOOD
- **Evidence**: BOX_BREAK (0x04) and ^ (custom) both detected. Page count tracked per message.
- **Gap**: 4 multipage messages identified

### Choice Detection
- **Status**: GOOD
- **Evidence**: TWO_CHOICE (0x1B) and THREE_CHOICE (0x1C) detected. 145 choice messages identified.
- **Gap**: Choice labels not always extractable from raw text alone

### Variable Detection
- **Status**: GOOD
- **Evidence**: 8 unique template variables found: gsCount, heartPieceCount, heartContainerCount, rupee, a_btn, days, typeHint, count
- **Gap**: Some dynamic values inserted at runtime without template markers

### Button Prompt Detection
- **Status**: GOOD
- **Evidence**: 137 messages with button glyphs detected. Button byte range (0x9F-0xAB) mapped.
- **Gap**: None significant

### ENG/GER/FRA Alignment
- **Status**: PARTIAL
- **Evidence**: 117 entries with trilingual alignment from custom messages. ~2179 hint texts with trilingual alignment. ~274 item names with trilingual alignment.
- **Gap**: Vanilla message GER/FRA not available in NTSC build. Full alignment requires PAL O2R.

### Strings Outside Message System
- **Status**: DOCUMENTED
- **Evidence**: ~2111 SoH UI strings identified as hardcoded C++ literals. No i18n framework exists.
- **Gap**: Would require source modification to add translation support

### Encoding Support for Spanish
- **Status**: UNKNOWN
- **Evidence**: NES font covers 0x80-0x9E (accented Latin). Spanish ñ (U+00F1) maps to 0xA1 in Latin-1 which IS in the font table at position 0xA1.
- **Risk**: Need to verify glyph exists in font textures
- **Recommendation**: Check `nes_font_static.xml` for 0xA1 glyph

### Spanish Character Set
- **Partial Evidence**: The `textBoxSpecialCharacters` map in `CustomMessageManager.cpp` includes:
  - À (0x80), Á (0x91), Â (0x82), Ã (0x92), Ä (0x83/0x93)
  - Ç (0x84/0x94), É (0x86/0x96), Ê (0x87/0x97), Ë (0x88/0x98)
  - Í (0x91), Ñ (mapped to 0xA1?), Ó (0x9A), Ô (0x8A/0x9A)
  - Ú (0x9C), Ü (0x8E/0x9E), ß (0x8F)
  - ¿ (0xBF in pixel width table)
- **Missing from explicit map**: ñ, ¡
- **Font table**: `z_kanfont.c` fontTbl[140] covers 0x20-0xAB. Position 0xA1 = "Control Stick" glyph in Japanese mode, but may map to ñ in NES mode.

## Readiness Checklist

| Requirement | Status | Notes |
|-------------|--------|-------|
| Complete corpus | YES | 9077 entries across all systems |
| Message IDs preserved | YES | Hex IDs maintained exactly |
| RAW_TEXT preserved | YES | Control codes intact |
| PLAIN_TEXT generated | YES | Human-readable version created |
| Control codes documented | YES | 38 types inventoried |
| Page breaks detected | YES | BOX_BREAK and ^ markers |
| Choices detected | YES | TWO_CHOICE and THREE_CHOICE |
| Variables detected | YES | 8 template variables found |
| Button prompts detected | YES | 137 messages with glyphs |
| ENG/GER/FRA aligned | PARTIAL | Custom messages aligned; vanilla GER/FRA missing |
| No strings outside message system | NO | 2111 SoH UI strings need i18n framework |
| Spanish charset support | UNKNOWN | Font coverage needs verification |
| No translation performed | YES | All entries in English |
| No voice generation | YES | No audio files created |
| Corpus is re-producible | YES | Tool can be re-run deterministically |

## Recommendation

**PARTIALLY_READY** — The corpus is comprehensive for game content (vanilla messages, custom messages, randomizer). However:

1. **SoH UI strings** need an i18n framework before translation — this is a SOURCE CHANGE
2. **Spanish font coverage** needs verification before translation — check if ñ/Ñ glyphs exist
3. **PAL O2R** would provide complete GER/FRA alignment for vanilla messages

### Next Steps for L02
1. Verify Spanish glyph coverage in NES font
2. Design i18n approach for SoH UI strings (wrapper functions or resource-based)
3. Create Spanish glossary for proper nouns (locations, items, characters)
4. Begin translation of vanilla messages using corpus as source
5. Translate randomizer hints and items (already trilingual-ready)
