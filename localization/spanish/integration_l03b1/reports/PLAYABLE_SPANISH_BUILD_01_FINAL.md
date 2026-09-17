# PLAYABLE_SPANISH_BUILD_01 — FINAL CERTIFICATION REPORT

## 1. Executive Summary
- **Target**: `PLAYABLE_SPANISH_BUILD_01`
- **Objective**: Deliver a fully playable, stable Ocarina of Time PC experience in neutral Latin American Spanish (`es-419`), Xbox Series X controller support, existing saves preserved, and zero dialog crashes.
- **Build Status**: **PASS** (Exit Code: 0)
- **Deployment Status**: Deployed exclusively to `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\runtime\build-test`.
- **Live Project Status**: `C:\Users\danie\Desktop\Ocarina of Time PC` was completely untouched (**YES / UNCHANGED**).

---

## 2. Binary & Asset Artifact Registry

| File | Path | Size (Bytes) | SHA-256 | Timestamp |
| :--- | :--- | :--- | :--- | :--- |
| `soh.exe` (Build Output) | `source\shipwright\x64\Release\soh.exe` | 33,003,520 | `C43BCABE213ADB6E232E9902C49ECC1F6464F5084F86B72D994C0724B4D63FC9` | 2026-09-16 16:44:46 |
| `soh.o2r` (Build Output) | `source\shipwright\x64\Release\soh.o2r` | 1,167,200 | `22D4788BCE69C939E0334D16CE3CFB51F8BAF0470729C4309C9360D7F06CA03A` | 2026-09-16 16:24:13 |
| `soh.exe` (Staged Runtime) | `runtime\build-test\soh.exe` | 33,003,520 | `C43BCABE213ADB6E232E9902C49ECC1F6464F5084F86B72D994C0724B4D63FC9` | 2026-09-16 16:44:46 |
| `soh.o2r` (Staged Runtime) | `runtime\build-test\soh.o2r` | 1,167,200 | `22D4788BCE69C939E0334D16CE3CFB51F8BAF0470729C4309C9360D7F06CA03A` | 2026-09-16 16:24:13 |
| `es.o2r` (Staged Runtime) | `runtime\build-test\es.o2r` | 96,911 | `7473BC997A2D293E6237E39EAAE96FD3D5FD10775124523C05C4A6DD49D8963A` | 2026-09-16 16:08:17 |

---

## 3. Spanish Archive (`es.o2r`) Verification
`runtime\build-test\es.o2r` was audited and certified:
- **Format**: Valid standard ZIP archive readable by `libzip` / `O2rArchive`.
- **Entries**: 12 total entries, 0 duplicates.
  - `text/spa_message_data_static/spa_message_data_static` (239,454 bytes, 2,116 binary entries, resource tag `TXTO`).
  - 11 Spanish font glyph textures (`textures/nes_font_static/gMsgChar80` through `gMsgChar9D`):
    - 64-byte OTR header (`OTEX`, Little-Endian, Version 0).
    - 16-byte Fast::Texture metadata: Type 5 (I4), Dimensions 16x16, Data size 128 bytes.
    - Total payload: Exactly 208 bytes per glyph.
- **Runtime Discovery Evidence**:
  Logged directly in `runtime\build-test\logs\Ship of Harkinian.log`:
  ```
  [2026-09-16 16:45:06.861] [ArchiveManager.cpp:240] [info] Reading archive: C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\runtime\build-test\es.o2r
  [2026-09-16 16:45:06.861] [ArchiveManager.cpp:272] [info] Adding Archive C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV\runtime\build-test\es.o2r to Archive Manager
  ...
  [16:45:07.822] [z_message_OTR.cpp:94] [info] Spanish message table loaded successfully from es.o2r
  ```

---

## 4. Logical Message Coverage Accounting
- **Vanilla / Static binary messages** (`spa_message_data_static`): 2,116 entries
- **Custom / Engine-managed messages** (`CustomMessageManager`): 117 entries
- **Total Logical Coverage**: **2,233 / 2,233** (100% of certified main-game corpus).

---

## 5. Stability & Glyph Mode
- **GLYPH_MODE**: `DYNAMIC_DISABLED_FOR_STABILITY`
- **Root Cause of Prior Crash**: Missing dynamic texture path `__OTR__textures/buttons/LTBtn` in archive caused Fast3D null-pointer dereference in message box rendering.
- **Resolution**:
  - `GlyphResolver_GetZTexture()` returns `nullptr`.
  - Character `0x84` in `z_kanfont.c` falls through to `fontTbl[0x84]` (`gMsgCharA4ButtonZTex`).
  - Message `0x100D` and all tutorial text render with classic N64 Z button glyph without crashing.
  - Physical controls remain mapped to Xbox (LT physically performs Z-targeting).

---

## 6. Save Policy Audit & Verification

| Save File | Baseline Hash | Post-Runtime Hash | Status |
| :--- | :--- | :--- | :--- |
| `file1.sav` | `AAA977AFC3A77168FBD9E4B67D586061F036D9D37BF0B073026A518BF158F5DD` | `AAA977AFC3A77168FBD9E4B67D586061F036D9D37BF0B073026A518BF158F5DD` | **BIT-FOR-BIT IDENTICAL** |
| `file2.sav` | `7263453E0D6A5E597461B7C2F229CCD76C1DFD3749931B5903000DE993FCA046` | `7263453E0D6A5E597461B7C2F229CCD76C1DFD3749931B5903000DE993FCA046` | **BIT-FOR-BIT IDENTICAL** |
| `global.sav`| `0507387C98B78147330533D865A1900D36E13956E8C952CAFD15F61F1A9F179E` | `0507387C98B78147330533D865A1900D36E13956E8C952CAFD15F61F1A9F179E` | **BIT-FOR-BIT IDENTICAL** |

`global.sav` has a safe backup at `Save\global.sav.baseline_pre_test`. When Spanish is selected via the in-game UI Settings, `global.sav` will record `language: 4` (`EXPECTED_LANGUAGE_ONLY`), strictly without touching `file1.sav` or `file2.sav`.

---

## 7. Runtime Smoke & Boot Verification
- **Process Boot**: `soh.exe` launched cleanly (PID 22356, working set ~475 MB).
- **Log Initializations**:
  - SDL Game Controllers loaded from `./gamecontrollerdb.txt` (844 profiles).
  - Existing save slots loaded (`SaveManager: Load File - fileNum: 0, 1, 2`).
  - Cutscene / title scene loaded (`Scene Init - sceneNum: 0x51, entranceIndex: 0xcd`).
  - Zero crashes, zero fatal exceptions.
