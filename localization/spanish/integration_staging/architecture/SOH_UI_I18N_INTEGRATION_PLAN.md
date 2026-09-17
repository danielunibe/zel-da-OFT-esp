# SOH UI I18N INTEGRATION PLAN (ES-419)

**Project**: Ship of Harkinian (Couch Edition)  
**Task**: L03A — Spanish Integration Staging + Font Encoding Package  
**Focus**: Architecture for integrating player-facing UI without hardcoded string substitution  

---

## 1. Overview and Problem Statement

Ship of Harkinian's PC interface is rendered using Dear ImGui, with enhancements, menus, and options distributed across `soh/soh/SohGui/` and `soh/soh/Enhancements/`.
In phase L02B/L02C, all 1,673 player-facing UI strings were categorized, translated, and verified in:
`packages/soh_ui/soh_ui_catalog_es_419.json` (1,621 unique keys)

Directly hardcoding Spanish strings into C++ source files is an anti-pattern that creates merge conflicts with upstream Shipwright releases. A clean, modular catalog-lookup architecture must be employed in **L03B**.

---

## 2. Catalog Architecture & Memory Model

The staged artifact `packages/soh_ui/soh_ui_catalog_es_419.json` provides a key-value mapping:

```json
{
  "SOH_UI::soh::soh::SohGui::SohMenuSettings_cpp::L222": {
    "english": "Language",
    "spanish": "Idioma",
    "future_i18n_key": "UI.SETTINGS.LANGUAGE",
    "category": "CONFIGURATION"
  }
}
```

### Proposed C++ I18n Abstraction:
```cpp
namespace Ship::I18n {
    // Returns the localized Spanish string if LANGUAGE_ESP is active, else returns default English string.
    const char* Get(const char* key, const char* defaultEnglish);
    
    // Convenience macro for ImGui labels
    #define _(text) Ship::I18n::Get(#text, text)
}
```

---

## 3. Separation of Player-Facing Labels vs Internal CVars

A critical finding in Task L02C:
- **Player-Facing Strings** (1,673 occurrences): Menu titles, button text, checkbox labels, tooltips, slider names, audio cues. These ARE localized.
- **Internal CVar Keys** (e.g. `gSettings.Languages`, `gEnhancements.TextSpeed`): These MUST REMAIN in ASCII English. Localizing CVar keys would corrupt `shipofharkinian.json` serialization and reset player configurations upon upgrade.

Example of correct separation:
```cpp
// CORRECT:
UIWidgets::EnhancementCheckbox(_("Text Speed"), CVAR_ENHANCEMENT("TextSpeed"));

// INCORRECT (FORBIDDEN):
UIWidgets::EnhancementCheckbox(_("Text Speed"), CVAR_ENHANCEMENT("VelocidadDeTexto"));
```

---

## 4. Accessibility Subsystem (TTS / Screen Reader)

The 110 accessibility strings resolved in L02C reside in:
- `soh/soh/Enhancements/tts/tts.cpp` (speech cues: "ver", "entrer", "abrir", scene names)
- `scenes_eng` (all 73 dungeon and area names for blind navigation)
- `filechoose_eng` (save slot status and file management speech labels)

In L03B, `tts.cpp` hooks directly into the catalog, ensuring the screen reader speaks canonical Spanish locations (`"Interior del Gran Árbol Deku"`, `"Templo del Agua"`, `"Cueva de los Dodongos"`, etc.) whenever `LANGUAGE_ESP` is selected.
