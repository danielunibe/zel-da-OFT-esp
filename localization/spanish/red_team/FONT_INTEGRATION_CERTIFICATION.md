# Font Integration Certification Report (ES-419)

**Project**: Ship of Harkinian (Couch Edition) — Ocarina of Time PC  
**Canonical Development Root**: `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV`  
**Target Locale**: `es-419` (Neutral Latin American Spanish)  
**Corpus Analyzed**: 7,652 Master Strings (Main Game + Randomizer + SoH UI)  
**Font Integration Readiness**: **`READY_WITH_CHANGES`**  

---

## 1. Executive Summary

The Red-Team audited the font architecture documented in `SPANISH_GLYPH_AUDIT.md` against the real character frequency distribution of the finalized L02C corpus.

The vanilla N64 font table (`fontTbl[140]` in `z_kanfont.c`, covering bytes `0x20` to `0xAB`) natively supports ASCII and select Western European accented characters, but exhibits critical deficiencies for standard Spanish:
1. **`ñ` and `Ñ`**: Byte `0xA1` is hardcoded to `gMsgCharA1ButtonCTex` (N64 C Button glyph), leaving no default slot for `ñ`.
2. **`¡` and `¿`**: Pixel widths exist in `CustomMessageManager.cpp`, but textures are missing from `fontTbl`.
3. **Acute Accents (`í`, `ó`, `ú`)**:
   - `í` currently maps to `0x91` (the same byte as `á`), resulting in incorrect acute `á` rendering.
   - `ó` maps to `0x9A` (renders as circumflex `ô`).
   - `ú` maps to `0x9C` (renders as grave `ù`).

---

## 2. Character Frequency & Slot Allocation Matrix

From the audited L02C corpus (`SPANISH_CHARACTER_FREQUENCY_L02C.csv`):

| Glyph | Unicode | Corpus Frequency | Current Vanilla Slot | Proposed Remapped Slot | Source Texture Action | Status |
|---|---|---|---|---|---|---|
| **¡** | `U+00A1` | 2,481 | None | `0x83` (formerly `Ä`) | Inject inverted exclamation glyph | **READY_WITH_CHANGES** |
| **í** | `U+00ED` | 1,435 | `0x91` (conflicts with á) | `0x99` (formerly `ï`) | Inject acute i glyph | **READY_WITH_CHANGES** |
| **ó** | `U+00F3` | 939 | `0x9A` (renders as ô) | `0x9A` (overwrite texture) | Replace circumflex texture with acute ó | **READY_WITH_CHANGES** |
| **ñ** | `U+00F1` | 748 | `0xA1` (conflicts with C) | `0x92` (formerly `â`) | Inject eñe glyph | **READY_WITH_CHANGES** |
| **¿** | `U+00BF` | 743 | None | `0x85` (formerly `È`) | Inject inverted question mark glyph | **READY_WITH_CHANGES** |
| **ú** | `U+00FA` | 494 | `0x9C` (renders as ù) | `0x9D` (formerly `û`) | Inject acute u glyph | **READY_WITH_CHANGES** |
| **Á** | `U+00C1` | 117 | `0x80` (renders as À) | `0x80` (overwrite texture) | Replace grave texture with acute Á | **READY_WITH_CHANGES** |
| **Ú** | `U+00DA` | 8 | None | `0x8C` (formerly `Ù`) | Inject acute Ú glyph | **READY_WITH_CHANGES** |
| **Í** | `U+00CD` | 7 | None | `0x89` (formerly `Ï`) | Inject acute Í glyph | **READY_WITH_CHANGES** |
| **Ó** | `U+00D3` | 3 | None | `0x8A` (formerly `Ô`) | Inject acute Ó glyph | **READY_WITH_CHANGES** |
| **Ñ** | `U+00D1` | 2 | None | `0x81` (formerly `Î`) | Inject capital Eñe glyph | **READY_WITH_CHANGES** |

---

## 3. Font Plan Viability & Architectural Boundary

1. **Slots Availability**:
   - There are 16 unused French/German glyph slots in the `0x80..0x9E` range.
   - Spanish requires exactly 11 slots for full, uncompromised orthography.
   - **Result**: The remapping strategy is mathematically sound and has 5 spare slots remaining.

2. **Strict Preservation of Diacritics**:
   - The Red-Team strictly enforces that NO vowels have their accents stripped (`í` will NOT be flattened to `i`).
   - `ñ` will NOT be substituted with `n`.
   - `¡` and `¿` will NOT be deleted.

3. **Codex Alignment**:
   - Codex is actively working on `glyph_workspace` to build the runtime texture injector and custom message manager hooks.
   - No source code in `source/shipwright` was modified during L02C.
   - **Verdict**: **`READY_WITH_CHANGES`** (Font architecture certified; ready for font texture generation in integration phase).
