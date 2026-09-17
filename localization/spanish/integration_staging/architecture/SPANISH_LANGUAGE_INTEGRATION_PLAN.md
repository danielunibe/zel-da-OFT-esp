# SPANISH LANGUAGE INTEGRATION PLAN (ES-419)

**Project**: Ship of Harkinian (Couch Edition)  
**Task**: L03A — Spanish Integration Staging + Font Encoding Package  
**Target Locale**: `es-419` (Neutral Latin American Spanish)  
**Status**: ARCHITECTURAL SPECIFICATION (READ-ONLY STAGING; NO SOURCE MODIFICATION IN L03A)  

---

## 1. Executive Overview

Ship of Harkinian natively implements a multi-language architecture inherited from the PAL GameCube / Master Quest release, supplemented by PC port enhancements in `libultraship` and `soh`. Currently, four language slots are defined in `soh/include/z64.h`:

```c
typedef enum {
    LANGUAGE_ENG, // 0 - English (Default)
    LANGUAGE_GER, // 1 - German
    LANGUAGE_FRA, // 2 - French
    LANGUAGE_JPN, // 3 - Japanese (NTSC text / Kanji table)
    LANGUAGE_MAX  // 4
} Language;
```

This document establishes the safe, non-breaking architectural strategy for adding `LANGUAGE_ESP` (or `LANGUAGE_LATAM_ES`) in phase **L03B**.

---

## 2. Language Identifier Definition

### Proposed Enum Identifier:
```c
typedef enum {
    LANGUAGE_ENG, // 0
    LANGUAGE_GER, // 1
    LANGUAGE_FRA, // 2
    LANGUAGE_JPN, // 3
    LANGUAGE_ESP, // 4 - Neutral Latin American Spanish (ES-419)
    LANGUAGE_MAX  // 5
} Language;
```

### Rationale for `LANGUAGE_ESP`:
1. **Naming Convention Alignment**: Matches 3-letter uppercase convention used throughout the codebase (`ENG`, `GER`, `FRA`, `JPN`).
2. **Standard Portability**: Recognized across Nintendo internal tables and decompilation conventions.
3. **No Collision with G02 / Codex**: Codex G02 is working on controller button glyphs (`glyph_workspace` and `z_kanfont.c`) and does not touch language enums.

---

## 3. Subsystem Impact Analysis

### 3.1. Engine Message Routing (`z_message_PAL.c` / `SohMenuSettings.cpp`)
- `messageTables` in `SohMenuSettings.cpp` maps pointers to each language's message table:
  ```cpp
  static const std::array<MessageTableEntry**, LANGUAGE_MAX> messageTables = {
      &sEngMessageEntryTablePtr,
      &sGerMessageEntryTablePtr,
      &sFraMessageEntryTablePtr,
      &sJpnMessageEntryTablePtr,
      &sSpaMessageEntryTablePtr, // New L03B entry
  };
  ```
- In `z_message_PAL.c`, message table lookup is indexed by `gSaveContext.language`. When `gSaveContext.language == LANGUAGE_ESP`, messages route to `sSpaMessageEntryTablePtr`.

### 3.2. Font Selection & Glyph Rendering (`z_kanfont.c`)
- Stock font table `fontTbl[140]` contains 140 glyph texture pointers (`0x20..0xAB`).
- In L03B, `fontTbl` will provide Spanish glyphs at internal indices `0x80..0x9E`.
- Because the 11 Spanish glyphs (`ñ, Ñ, ¡, ¿, í, ó, ú, Á, Í, Ó, Ú`) replace unused French/German characters in `0x80..0x9E`, there is **zero collision** with ASCII (`0x20..0x7E`) or Button Glyphs (`0x9F..0xAB`).

### 3.3. GUI Language Selector (`SohMenu.h` / `SohMenuSettings.cpp`)
- `sLanguageOptions` in `SohMenu.h` receives an additional entry:
  ```cpp
  { LANGUAGE_ENG, "English" },
  { LANGUAGE_GER, "German" },
  { LANGUAGE_FRA, "French" },
  { LANGUAGE_JPN, "Japanese" },
  { LANGUAGE_ESP, "Español (Latinoamérica)" }, // L03B
  ```

---

## 4. Default Language Policy

1. **Existing Installations**:
   - The user's language setting is stored in `shipofharkinian.json` via CVar `gSettings.Languages`.
   - Existing users retain their configured language (`LANGUAGE_ENG` if unchanged).
2. **New Couch Edition Users**:
   - Initial boot checks whether `gSettings.Languages` has been initialized.
   - If uninitialized, Couch Edition presents a language selection prompt or defaults to system locale.
   - **Spanish is NEVER forced globally** onto users who prefer English.

---

## 5. Backward & Forward Compatibility

- If `LANGUAGE_ESP` (value 4) is written to `global.sav`, and the user later opens the save in an unpatched version of Ship of Harkinian where `LANGUAGE_MAX == 4`:
  - `SaveManager.cpp:2700`:
    ```cpp
    if (gSaveContext.language >= LANGUAGE_MAX) {
        gSaveContext.language = CVarGetInteger(CVAR_SETTING("Languages"), LANGUAGE_ENG);
    }
    ```
  - The engine automatically clamps the value and defaults to `LANGUAGE_ENG` safely without crash, assertion, or save corruption.
