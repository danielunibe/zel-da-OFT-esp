# Report L02B — Ship of Harkinian UI Classification, Translation & Future i18n Catalog

**Project**: Ship of Harkinian (Couch Edition) — Ocarina of Time PC  
**Canonical Development Root**: `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV`  
**Target Locale**: `es-419` (Spanish Latin America - Neutral)  
**Corpus**: `LOCALIZATION_ROOT/corpus/soh_ui_strings_eng.jsonl` -> `SPANISH_ROOT/corpus/soh_ui_strings_es_419.jsonl`  

---

## 1. Static Analysis & Classification Breakdown

The extraction in L01 yielded 3,098 raw strings from SoH C++ source code and accessibility json files. As mandated by Section 18 of the specification, blind translation of these strings is prohibited because many represent internal C++ identifiers, CVar registrations, debug tools, and non-English test files.

Through C++ AST line inspection against `SOURCE_ROOT/shipwright`, all 3,098 entries were classified:

| Classification | Count | Description & Handling |
|---|---|---|
| **PLAYER_FACING** | **1,673** | User-facing menus, settings labels, dropdown options, button prompts, cosmetic tags, and English screen-reader accessibility strings. **100% translated into es-419 with future i18n keys.** |
| **INTERNAL_IDENTIFIER** | **725** | CVar names, dotted configuration paths (`TimeSavers.*`, `Menu.*`, `Link.*`), and engine macros. Kept in original syntax to avoid runtime breaks. |
| **FALSE_POSITIVE** | **658** | French and German accessibility files (`*_fra.json`, `*_ger.json`) inadvertently captured during extraction. Flagged and preserved as non-canonical references. |
| **DEBUG_ONLY** | **41** | Developer tools, memory viewers, and collision visualizers from `SohMenuDevTools.cpp`. Preserved untranslated for development stability. |
| **FORMAT_STRING** | **1** | Null-delimited internal byte token (`Off\0Set\0Game\0\0`). Preserved verbatim. |
| **UNKNOWN** | **0** | All strings successfully resolved. |
| **Total Raw Entries** | **3,098** | |

---

## 2. Future i18n Architecture & Key Generation

To prepare Ship of Harkinian for seamless modern i18n integration without modifying C++ source during L02B, each player-facing string was assigned a hierarchical, collision-free key:

- **Settings**: `SETTINGS.GENERAL.<KEY>`, `SETTINGS.GRAPHICS.<KEY>`, `SETTINGS.AUDIO.<KEY>`
- **Controller**: `CONTROLLER.<KEY>` (e.g. `CONTROLLER.DEADZONE`, `CONTROLLER.SENSITIVITY`)
- **Enhancements**: `ENHANCEMENTS.GAMEPLAY.<KEY>` (e.g. `ENHANCEMENTS.GAMEPLAY.FASTER_PAUSE_MENU`)
- **Cosmetics**: `COSMETICS.<KEY>` (e.g. `COSMETICS.KOKIRI_TUNIC`, `COSMETICS.EQUIPMENT`)
- **Accessibility**: `ACCESSIBILITY.FILECHOOSE.<KEY>`, `ACCESSIBILITY.KALEIDOSCOPE.<KEY>`, `ACCESSIBILITY.SCENES.<KEY>`, `ACCESSIBILITY.MISC.<KEY>`
- **Randomizer Tracker**: `RANDOMIZER.UI.<KEY>`

Total stable i18n keys generated: **1,328 unique keys** cataloged in `soh_ui_i18n_catalog_es_419.json` and mapped in `soh_ui_i18n_mapping.csv`.

---

## 3. Xbox Input Terminology Compliance

All controller-facing strings adhere to `XBOX_INPUT_TERMINOLOGY.csv`:
- "Botón A", "Botón B", "Botón X", "Botón Y"
- "Gatillo Izquierdo (LT)", "Gatillo Derecho (RT)", "Botón L (LB)", "Botón R (RB)"
- "Palanca Izquierda" (Control Stick / Analog Stick), "Palanca Derecha"
- "Cruz Digital" (D-Pad), "Flecha Arriba", "Flecha Abajo", "Flecha Izquierda", "Flecha Derecha"
- Action verbs: "Presiona [glyph]", "Mantén presionado [glyph]", "Mueve [glyph]"

---

## 4. Accessibility Translation Subsystem

The 329 English accessibility entries were localized into natural Latin American Spanish:
- `filechoose_eng.json` (27 entries): File slots, copying, deleting, character selection, stereo/mono audio settings.
- `kaleidoscope_eng.json` (207 entries): Health readout ("Salud - $0 Corazones"), rupee counts, quest medallions, inventory slots, dungeon floors.
- `scenes_eng.json` (73 entries): Screen-reader scene names for all Hyrule areas and dungeons.
- `misc_eng.json` (22 entries): Timers, button labels, and system announcements.

---

## 5. Artifacts Generated
- `SPANISH_ROOT/qa/SOH_UI_CLASSIFICATION.csv`
- `SPANISH_ROOT/corpus/soh_ui_i18n_catalog_es_419.json`
- `SPANISH_ROOT/corpus/soh_ui_i18n_mapping.csv`
- `SPANISH_ROOT/corpus/soh_ui_strings_es_419.jsonl`
