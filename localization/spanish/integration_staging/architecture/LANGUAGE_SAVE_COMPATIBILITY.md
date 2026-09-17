# LANGUAGE AND SAVE COMPATIBILITY ARCHITECTURE (`global.sav`)

**Project**: Ship of Harkinian (Couch Edition)  
**Task**: L03A — Spanish Integration Staging + Font Encoding Package  
**Focus**: Compatibility analysis of `global.sav` and `SaveContext` for `LANGUAGE_ESP`  

---

## 1. Context and Problem Statement

Task S01 established that `global.sav` is a critical binary state file managed by `SaveManager.cpp`. Corrupting or improperly altering struct offsets within `SaveContext` invalidates player save data, audio configurations, and game progression.

This document verifies the exact serialization mechanics of `language` in `SaveContext` and demonstrates that introducing `LANGUAGE_ESP` carries **zero risk of save corruption**.

---

## 2. Storage and Memory Layout

In `soh/include/z64save.h`:
```c
typedef struct {
    /* 0x0000 */ ...
    /* 0x1409 */ u8 language; // PAL 0: English; 1: German; 2: French; 3: Japanese
    /* 0x140A */ ...
} SaveContext;
```

### Key Observations:
1. **Size**: `language` is stored as an unsigned 8-bit integer (`u8`, range `0..255`).
2. **Fixed Offset**: It occupies 1 byte at fixed struct offset `0x1409`.
3. **No Struct Growth**: Adding enum value `LANGUAGE_ESP = 4` does NOT alter the size of `u8 language` (it remains 1 byte). Therefore, struct alignment, padding, and total sizeof(`SaveContext`) remain 100% identical.

---

## 3. Serialization and Deserialization Pipeline

In `soh/soh/SaveManager.cpp`:

### Save Loading:
```cpp
// Line 1322
gSaveContext.language = CVarGetInteger(CVAR_SETTING("Languages"), LANGUAGE_ENG);

// Line 2700 - Boundary Enforcement:
if (gSaveContext.language >= LANGUAGE_MAX) {
    gSaveContext.language = CVarGetInteger(CVAR_SETTING("Languages"), LANGUAGE_ENG);
}
```

### Save Writing:
When saving the file choose state or global configuration:
- `gSaveContext.language` is written as a single byte (`4` for `LANGUAGE_ESP`).
- `CVAR_SETTING("Languages")` is written to `shipofharkinian.json` as integer `4`.

---

## 4. Cross-Version Compatibility Scenarios

| Scenario | Behavior in Engine | Risk / Impact |
|---|---|---|
| **Old Save (`global.sav` with language 0..3) loaded in Couch Edition** | Engine loads value 0..3 without modification. User's language preference is preserved. | **ZERO RISK (SAFE)** |
| **New Save (`LANGUAGE_ESP` = 4) loaded in Couch Edition** | Value 4 is valid (`4 < LANGUAGE_MAX` where `LANGUAGE_MAX = 5`). Spanish messages and UI are loaded. | **ZERO RISK (SAFE)** |
| **New Save (`LANGUAGE_ESP` = 4) loaded in Vanilla SoH (where `LANGUAGE_MAX` = 4)** | Line 2700 detects `4 >= 4` and safely resets `gSaveContext.language` to `LANGUAGE_ENG` (0). | **ZERO RISK (SAFE FALLBACK)** |
| **Corrupted Save byte (> 4)** | Boundary check clamps any invalid value to `LANGUAGE_ENG`. | **ZERO RISK (RESILIENT)** |

---

## 5. File Name Character Set Compatibility

In `SaveManager.cpp:530-540`:
`gSaveContext.ship.filenameLanguage` controls the character set used on the file creation keyboard (`NAME_LANGUAGE_PAL` vs `NAME_LANGUAGE_NTSC`).
For `LANGUAGE_ESP`:
- It continues using `NAME_LANGUAGE_PAL`, which provides standard Latin letters and accented characters.
- No modifications to file naming structs or save header signatures are required.
