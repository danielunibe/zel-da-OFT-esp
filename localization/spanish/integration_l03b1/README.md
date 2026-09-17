# TASK L03B1 — SPANISH MAIN-GAME TEXT INTEGRATION
## Couch Edition Localization Workspace (`integration_l03b1`)

**Project**: Ocarina of Time PC — Couch Edition  
**Phase**: Task L03B1 — Spanish Main-Game Text Integration  
**Status**: **`WAITING_FOR_G02_CLOSURE`** (CONCURRENCY GATE ACTIVE)  
**Target Locale**: Neutral Latin American Spanish (`es-419`)  
**Scope**: Main Game Dialogue & Message Box Text (2,233 Messages)

---

### 1. Concurrency Gate Status: G02 Active Partial

In strict compliance with **Section 3 (Concurrency Gate — G02)** of the task mandate, an audit of the glyph subsystem was conducted:

1. **`glyph_workspace/g02c/G02_FINAL_CLOSURE.md`**:
   - Status: `PARTIAL pending human validation`.
   - Requires physical Xbox LT-as-Z confirmation and dual-screenshot visual verification on 3440x1440 display.
2. **`glyph_workspace/g02c/HUMAN_VALIDATION_RESULTS.csv`**:
   - All 10 acceptance tests marked `PENDING / NOT_TESTED`.
3. **Source Tree Ownership**:
   - G02/G02B/G02C holds active uncommitted modifications in:
     - `soh/src/code/z_kanfont.c`
     - `soh/include/glyph_resolver.h`
     - `soh/soh/Enhancements/glyphs/GlyphResolver.cpp`
     - `soh/assets/custom/textures/buttons/LTBtn.ppm`

**Architectural Decision**:  
Because G02 is `ACTIVE_PARTIAL`, touching `SOURCE_ROOT` (especially `z_kanfont.c`) would violate the concurrency barrier and cause source collisions.  
Per Section 3 and Section 61, this workspace:
- Prepares the complete, exact, reproducible source patch set under `patches/`.
- Reconciles the font slot integration architecture to guarantee **zero regression** for German and French.
- Validates the staged main game package (`validate_spanish_package.py` PASS).
- Documents the pilot message matrix, layout test queue, and save hashes.
- Holds source modification strictly staged, awaiting G02 closure.

---

### 2. Workspace Directory Structure

```
integration_l03b1/
├── README.md                                             # This concurrency gate charter
├── architecture/
│   └── IMPLEMENTED_SPANISH_MESSAGE_PIPELINE.md           # End-to-end runtime message & font pipeline
├── font/
│   ├── SPANISH_FONT_REMAP_SAFE.csv                       # Language-aware font slot map protecting GER/FRA
│   └── bitmaps/                                          # 16x16 I4 font textures for the 11 Spanish glyphs
├── patches/
│   ├── 01_z64_language_enum.patch                        # LANGUAGE_ESP enum addition in z64.h
│   ├── 02_soh_menu_language_selector.patch               # Spanish option in SohMenu.h & SohMenuSettings.cpp
│   ├── 03_z_kanfont_spanish_glyphs.patch                 # Language-aware font dispatch in z_kanfont.c
│   ├── 04_z_message_pal_spanish_routing.patch            # Message table routing in z_message_PAL.c
│   ├── 05_z_message_otr_spanish_table.patch              # sSpaMessageEntryTable loading in z_message_OTR.cpp
│   └── 06_custom_message_manager_spanish.patch          # CustomMessageManager Spanish support
├── reports/
│   ├── L03B1_SOURCE_CHANGES.csv                          # Exhaustive inventory of planned source changes
│   ├── FONT_INTEGRATION_RESULT.md                        # GER/FRA regression analysis & safe slot design
│   ├── LANGUAGE_COMPATIBILITY_MATRIX.csv                 # Multi-language compatibility verification
│   ├── PILOT_MESSAGE_MATRIX.csv                          # 15 selected representative pilot messages
│   ├── RUNTIME_LAYOUT_RESULTS.csv                        # Layout evaluation on high-risk dialogue
│   ├── SAVE_HASHES_PRE.csv                               # SHA-256 baseline of build-test save files
│   ├── SAVE_HASHES_POST.csv                              # Post-validation save verification
│   └── L03B1_FINAL_REPORT.md                             # Comprehensive final report
└── prestate/
    └── z_kanfont.c.pre_l03b1                             # Snapshot of z_kanfont.c with G02 modifications
```

---

### 3. Immediate Next Step

Once Codex G02 completes its human validation session and files `G02_FINAL_CLOSURE.md` as `CLOSED_PASS`, the patch set in `patches/` can be applied cleanly in seconds with zero merge conflicts.
