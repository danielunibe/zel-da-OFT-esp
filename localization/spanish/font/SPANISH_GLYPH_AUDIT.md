# Spanish Glyph Audit — Ship of Harkinian 9.1.1

## Font System Overview

- **Font table**: `fontTbl[140]` in `z_kanfont.c` — covers bytes 0x20–0xAB
- **Character map**: `textBoxSpecialCharacters` in `CustomMessageManager.cpp` — UTF-8 → byte mapping
- **Pixel widths**: `pixelWidthTable` in `CustomMessageManager.cpp` — glyph width for layout
- **Button glyphs**: 0x9F–0xAB (A, B, C, L, R, Z, C-Up, C-Down, C-Left, C-Right, Z-Target, ControlStick, ControlPad)

## Complete Byte Mapping

### 0x20–0x7F: ASCII (100% Spanish-compatible)
Standard Latin alphabet, digits, punctuation. All present in fontTbl.

### 0x80–0x9E: Accented Latin (CJK/Extended Latin)
| Byte | FontTbl Glyph | Needed for ES? | Status |
|------|--------------|----------------|--------|
| 0x80 | À | Yes | ✅ Available |
| 0x81 | Î | No | ⚠️ Unused (wrong label in fontTbl — maps to `î` in textBoxSpecialCharacters) |
| 0x82 | Â | No | ⚠️ Unused |
| 0x83 | Ä | No | ⚠️ Unused |
| 0x84 | Ç | Rare (Catalan) | ✅ Available |
| 0x85 | È | No | ⚠️ Unused |
| 0x86 | É | **Yes** | ✅ Available |
| 0x87 | Ê | No | ⚠️ Unused |
| 0x88 | Ë | No | ⚠️ Unused |
| 0x89 | Ï | No | ⚠️ Unused |
| 0x8A | Ô | No | ⚠️ Unused |
| 0x8B | Ö | No | ⚠️ Unused |
| 0x8C | Ù | No | ⚠️ Unused |
| 0x8D | Û | No | ⚠️ Unused |
| 0x8E | Ü | **Yes** (Pingüino, cigüeña) | ✅ Available |
| 0x8F | ß | No | ⚠️ Unused |
| 0x90 | à | No | ⚠️ Unused |
| 0x91 | á | **Yes** | ✅ Available |
| 0x92 | â | No | ⚠️ Unused |
| 0x93 | ä | No | ⚠️ Unused |
| 0x94 | ç | Rare | ✅ Available |
| 0x95 | è | No | ⚠️ Unused |
| 0x96 | é | **Yes** | ✅ Available |
| 0x97 | ê | No | ⚠️ Unused |
| 0x98 | ë | No | ⚠️ Unused |
| 0x99 | ï | No | ⚠️ Unused |
| 0x9A | ô | No | ⚠️ Unused |
| 0x9B | ö | No | ⚠️ Unused |
| 0x9C | ù | No | ⚠️ Unused |
| 0x9D | û | No | ⚠️ Unused |
| 0x9E | ü | **Yes** | ✅ Available |

### 0x9F–0xAB: Button Glyphs (NOT character slots)
| Byte | Glyph | Spanish Reuse? |
|------|-------|---------------|
| 0x9F | A button | Keep |
| 0xA0 | B button | Keep |
| 0xA1 | C button | **CONFLICT** — ñ needs this slot |
| 0xA2 | L button | Keep |
| 0xA3 | R button | Keep |
| 0xA4 | Z button | Keep |
| 0xA5 | C-Up | Keep |
| 0xA6 | C-Down | Keep |
| 0xA7 | C-Left | Keep |
| 0xA8 | C-Right | Keep |
| 0xA9 | Z-Target | Keep |
| 0xAA | Control Stick | Keep |
| 0xAB | Control Pad | Keep |

## Critical Findings

### 1. Ñ/ñ — NO GLYPH SLOT AVAILABLE
- **Byte 0xA1** is assigned to `gMsgCharA1ButtonCTex` (C button glyph)
- No free slot exists in 0x80–0x9E for ñ
- The `pixelWidthTable` has `ñ` (7px) and `Ñ` (9px) entries, but no fontTbl slot
- The `textBoxSpecialCharacters` map has no entry for ñ or Ñ

**Resolution options:**
1. **Remap an unused accented character** — Replace one of the 15 unused slots (Î, Â, Ä, È, Ê, Ë, Ï, Ô, Ö, Ù, Û, à, â, ä, ë, ï, ô, ö, ù, û) with ñ/Ñ
2. **Add new font slot(s)** — Extend fontTbl to include ñ/Ñ (requires font texture creation)
3. **Substitute** — Replace ñ with "n" in translations (lossy, not recommended)

**Recommended**: Remap 0x81 (Î → Ñ) and 0x92 (â → ñ). These French characters are not needed for Spanish LATAM.

### 2. ¡/¿ — PARTIAL SUPPORT
- `pixelWidthTable`: ¡ (5px) and ¿ (7px) — widths defined
- `fontTbl`: No entry at corresponding positions — these characters have NO glyph
- `textBoxSpecialCharacters`: No mapping for ¡ or ¿

**Resolution**: Add ¡/¿ to fontTbl by remapping unused slots (e.g., 0x83 → ¡, 0x85 → ¿)

### 3. Accented Characters — FULLY AVAILABLE
All Spanish acute accents are present:
- á (0x91) ✅, é (0x96) ✅, í (mapped via á replacement needed), ó (via ô remap), ú (via ù remap)
- Actually: á(0x91), é(0x96) are in fontTbl directly
- í, ó, ú: The pixelWidthTable has entries but no fontTbl slot — they use the special character map via CustomMessageManager

**Wait — correction**: Looking at `textBoxSpecialCharacters`:
- `á` → 0x91 (fontTbl: gMsgChar91LatinSmallLetterAWithAcuteTex) ✅
- `é` → 0x96 (fontTbl: gMsgChar96LatinSmallLetterEWithAcuteTex) ✅
- `í` → 0x91 (MAPPED TO á) — **BUG: í maps to á's byte**
- `ó` → 0x9A (fontTbl: gMsgChar9ALatinSmallLetterOWithCircumflexTex) — renders as ô, not ó
- `ú` → 0x9C (fontTbl: gMsgChar9CLatinSmallLetterUWithGraveTex) — renders as ù, not ú
- `ü` → 0x9E (fontTbl: gMsgChar9ELatinSmallLetterUWithDiaeresisTex) ✅

**This means**: í, ó, ú are either missing or render as wrong glyphs. Only á, é, ü render correctly.

### 4. Full Spanish Character Support Summary

| Character | UTF-8 | Needed | Available Slot | Renders Correctly |
|-----------|-------|--------|---------------|-------------------|
| á | U+00E1 | Yes | 0x91 | ✅ Yes |
| é | U+00E9 | Yes | 0x96 | ✅ Yes |
| í | U+00ED | Yes | 0x91 (BUG) | ❌ Renders as á |
| ó | U+00F3 | Yes | 0x9A (BUG) | ❌ Renders as ô |
| ú | U+00FA | Yes | 0x9C (BUG) | ❌ Renders as ù |
| ü | U+00FC | Yes | 0x9E | ✅ Yes |
| ñ | U+00F1 | **Yes** | 0xA1 (CONFLICT) | ❌ No slot |
| Ñ | U+00D1 | Yes | None | ❌ No slot |
| ¡ | U+00A1 | Yes | None | ❌ No slot |
| ¿ | U+00BF | Yes | None | ❌ No slot |

## Required Font Modifications

### Minimum for Spanish LATAM
Remap 4 unused accented slots to missing characters:

| Current Byte | Current Glyph | New Glyph | Reason |
|-------------|--------------|-----------|--------|
| 0x81 | î/I-circumflex | Ñ | Needed for Ñ (capital ñ) |
| 0x83 | ä/A-diaeresis | ¡ | Needed for inverted exclamation |
| 0x85 | è/E-grave | ¿ | Needed for inverted question |
| 0x92 | â/a-circumflex | í | Needed for í (renders correctly) |
| 0x94 | ç/c-cedilla | ó | Needed for ó (renders correctly) |
| 0x9A | ô/o-circumflex | ñ | Needed for ñ (renders correctly) |
| 0x9C | ù/u-grave | ú | Needed for ú (renders correctly) |

### Updated textBoxSpecialCharacters (Proposed)
```cpp
static const std::unordered_map<std::string, char> textBoxSpecialCharacters = {
    { "À", 0x80 }, { "Ñ", 0x81 }, { "Â", 0x82 }, { "¡", 0x83 }, { "Ç", 0x84 }, { "¿", 0x85 }, { "É", 0x86 },
    { "Ê", 0x87 }, { "Ë", 0x88 }, { "Ï", 0x89 }, { "Ô", 0x8A }, { "Ö", 0x8B }, { "Ù", 0x8C }, { "Û", 0x8D },
    { "Ü", 0x8E }, { "ß", 0x8F }, { "à", 0x90 }, { "á", 0x91 }, { "í", 0x92 }, { "ä", 0x93 }, { "ó", 0x94 },
    { "è", 0x95 }, { "é", 0x96 }, { "ê", 0x97 }, { "ë", 0x98 }, { "ï", 0x99 }, { "ñ", 0x9A }, { "ö", 0x9B },
    { "ú", 0x9C }, { "û", 0x9D }, { "ü", 0x9E }
};
```

### Updated fontTbl (Proposed)
```c
// Replace these entries in fontTbl[140]:
gMsgChar81LatinCapitalLetterIWithCircumflexTex,  // → gMsgChar81LatinCapitalLetterNTex (Ñ)
gMsgChar83LatinCapitalLetterAWithDiaeresisTex,   // → gMsgChar83InvertedExclamationMarkTex (¡)
gMsgChar85LatinCapitalLetterEWithGraveTex,       // → gMsgChar85InvertedQuestionMarkTex (¿)
gMsgChar92LatinSmallLetterAWithCircumflexTex,    // → gMsgChar92LatinSmallLetterIAcuteTex (í)
gMsgChar94LatinSmallLetterCWithCedillaTex,       // → gMsgChar94LatinSmallLetterOAcuteTex (ó)
gMsgChar9ALatinSmallLetterOWithCircumflexTex,    // → gMsgChar9ALatinSmallLetterNTex (ñ)
gMsgChar9CLatinSmallLetterUWithGraveTex,         // → gMsgChar9CLatinSmallLetterUAcuteTex (ú)
```

### New Font Textures Needed
7 new glyph textures must be created (16×16 NES-style):
1. `gMsgChar81LatinCapitalLetterNTex` — Ñ
2. `gMsgChar83InvertedExclamationMarkTex` — ¡
3. `gMsgChar85InvertedQuestionMarkTex` — ¿
4. `gMsgChar92LatinSmallLetterIAcuteTex` — í
5. `gMsgChar94LatinSmallLetterOAcuteTex` — ó
6. `gMsgChar9ALatinSmallLetterNTex` — ñ
7. `gMsgChar9CLatinSmallLetterUAcuteTex` — ú

## Impact on L02 Translation

- **Without font changes**: á, é, ü render correctly. í, ó, ú, ñ, Ñ, ¡, ¿ will render incorrectly or not at all.
- **With proposed remapping**: All 10 Spanish characters supported. French/German characters lost (acceptable for es-419).
- **Button glyphs**: Unaffected — 0x9F–0xAB range untouched.

## Recommendation

**BLOCKER**: Font modification required before translation output can be used in-game. The L02 translation work can proceed using the `textBoxSpecialCharacters` byte mapping, but the font textures must be updated before integration.

**Priority**: Create the 7 new font textures and update `fontTbl` + `textBoxSpecialCharacters` as a separate source change task (NOT part of L02 — L02 is read-only on source).
