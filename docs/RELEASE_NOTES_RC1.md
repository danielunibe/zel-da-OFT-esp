# Ocarina of Time PC — Couch Edition
## Release Candidate 1 (`OCARINA_COUCH_EDITION_RC1`)
**Date:** September 16, 2026  
**Integration Lead:** Gemini 3.8 High Flash (Master Consolidation Orchestrator)

---

### 1. Overview
`OCARINA_COUCH_EDITION_RC1` represents the master convergence of three parallel workstreams:
- **Gemini Workstream**: P0 Crash Hardening, ES-419 Spanish Localization Integration, Message Decoder Capacity Protection, Language Index Array Bounds Safety, and Fast3D Unsafe Lookup Mitigation.
- **Kilo Workstream**: V02 Visual Foundation introducing dual Visual Profiles (`CLASSIC` and `ENHANCED`), ACES filmic tonemapping post-process, MaterialRegistry draw-call instrumentation, and atmospheric fog depth power curve.
- **OpenCode Workstream**: External Visual QA verification toolkit (40/40 test suite).

All components have been merged cleanly, compiled in Release configuration without errors (Exit Code 0), and validated through automated boot testing and save integrity analysis.

---

### 2. Subsystem Integrations & Invariants

#### A. Stability Hardening
1. **Fast3D Masked Texture Safe Lookups**:
   - Replaced all 13 unchecked ternary map lookups (`mMaskedTextures.find(key)->second.replacementData`) in `libultraship/src/fast/interpreter.cpp` with iterator-checked validations (`if (it != mMaskedTextures.end())`).
   - Unchecked map dereferences remaining: **0**.
2. **G_SETTIMG / OtrSignatureCheck Hardening**:
   - Protected raw pointer validation in `ResourceManager.cpp` using `VirtualQuery` memory page commit checks and SEH `__try ... __except` bounds handlers to prevent `0xC0000005` violations.
3. **Message Decoder Capacity**:
   - Replaced magic bound `190` with strict compile-time capacity checks (`CAN_SRC_READ`, `CAN_DST_WRITE`, `CAN_DST_WRITE_WIDE`, `CAN_CHARTEX_WRITE`) in `z_message_PAL.c`.

#### B. Localization (ES-419)
1. **Language Index Safety**:
   - Sourced and applied `LANGUAGE_ARRAY_INDEX(lang)` across `z_file_choose.c`, `z_file_nameset_PAL.c`, and `z_kaleido_scope_PAL.c`.
   - Remaining unsafe language array index uses: **0**.
2. **Spanish Dialogue Archive (`es.o2r`)**:
   - Contains 2,116 validated static Spanish messages (`spa_message_data_static`) with 0 literal choice markers and 0 gameplay control mismatches against NTSC reference.
   - 11 authentic 16x16 I4 128-byte OTEX font glyph textures (`Á, Ñ, ¡, ¿, Í, Ó, Ú, ñ, í, ó, ú`).
3. **Graceful English Fallback**:
   - Confirmed through negative isolation testing that absence of `es.o2r` defaults smoothly to English without crashing.

#### C. Visual Foundation (V02)
1. **Classic Profile (`VisualProfile = 0`)**:
   - Preserves original SoH presentation without tonemapping alteration, custom material responses, or atmospheric curves.
2. **Enhanced Profile (`VisualProfile = 1`)**:
   - Activates ACES filmic tonemapping fullscreen pass via Direct3D11.
   - Activates smoother atmospheric fog depth power curve (`powf(normalized, 0.85f)`).
   - Activates `MaterialRegistry` draw-call classification.

#### D. Controller & Display
1. **Xbox Controller Layout**:
   - Movement: Left Stick.
   - C-Buttons: Right Stick directions.
   - Z-Targeting: Physical Left Trigger (`LT` -> N64 `Z`).
   - Action / Attack: Preserved.
   - Dialogue glyphs: Authentic N64 button glyphs used for 100% crash safety.
2. **Ultrawide & Display**:
   - Resolution: 3440x1440 (21:9).
   - MatchRefreshRate: Enabled (Target 85 Hz intact).

---

### 3. Artifact Hashes (RC1 Verification Ledger)

| Artifact | Size (Bytes) | SHA256 |
|---|---|---|
| `runtime/build-test/soh.exe` | 33,020,416 | `5ee136fb0ee048b85d129658f703a1edfcb9a4f956c7c1644e785d6e0a02765c` |
| `runtime/build-test/soh.o2r` | 1,167,200 | `518977d24f25f5ccd28907a1651ec820bd6564c7c367c91f1cfa61dbab0611eb` |
| `runtime/build-test/es.o2r` | 99,610 | `d9037a8a4086d0073f90cc297fcbfe558a78ac3758f27b11a250537883fd990d` |
| `runtime/build-test/oot.o2r` | 33,482,668 | `6fc0211fd1ea450fc41aa0991c0abfd73ff61b81db39039033edfdb2653d9e4b` |

---

### 4. Save Integrity
All pre-existing user save files remain 100% untouched and identical:
- `file1.sav`: `NOT_PRESENT`
- `file2.sav`: `7263453e0d6a5e597461b7c2f229ccd76c1dfd3749931b5903000de993fca046` (**Identical**)
- `file3.sav`: `97445ef31f9c7dd6be4b087ec1d54c82ef2a4a4700febbeb2ac544d1327bc033` (**Identical**)
- `global.sav`: `b05f42d1646df6f408a734125b73c675e1ee1725c0fb767cfd1bfbc32af4beee` (**Identical**)

---

### 5. Live Project Isolation
`LIVE_ROOT` (`C:\Users\danie\Desktop\Ocarina of Time PC`) was strictly treated as read-only. Zero files were modified, overwritten, or removed.
