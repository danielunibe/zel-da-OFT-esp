# MASTER PRESTATE REPORT — V03.1 MASTER CONSOLIDATION
Date: 2026-09-17 02:19:18
Target: Ocarina of Time PC — Couch Edition
Pinned Shipwright: 9.1.1 Copper Bravo (4aaad850bd5540cd77c2d83f3ad348d3b38605b2)
Pinned libultraship: 17a0b7939bd05f5e617cef89457ca43774fc9a9f

## Baseline Invariants
- RC1.1 Core Correctness: PASS (Spanish ES-419, font widths, message parsing, custom message concat/equality, LT->Z, RS->C)
- Live Root: STRICTLY READ-ONLY (C:\Users\danie\Desktop\Ocarina of Time PC)
- Upstream Migration: PROHIBITED (No 9.2.x, No develop branch)
- RTX / DXR: NOT IMPLEMENTED in this gate (Readiness architecture preserved)

## Modified Visual Source Files (Pre-State)
| File | SHA256 | Size (bytes) |
| --- | --- | --- |
| source\shipwright\libultraship\include\fast\MaterialRegistry.h | 517AF553DAA3E1F6FDC243E32F969617640F29E5A2403055BBCCBCA6B37F567C | 2291 |
| source\shipwright\libultraship\src\fast\MaterialRegistry.cpp | DB59785FCF0D25B757A2517A9C0D58AEF59C129F16032673BA5AC98943699C2F | 7302 |
| source\shipwright\libultraship\src\fast\interpreter.cpp | 9ADEC81B7A3C22C7F3EC978F53F0EB61C89FD9BC0E4F0D52C78DD8DE5E4417ED | 182700 |
| source\shipwright\libultraship\src\fast\backends\gfx_direct3d11.cpp | 487C48CE96BF748A05AF84201C1E6C3F1D94B898B6EB313B04982A03B0F0A02C | 68908 |
| source\shipwright\libultraship\include\fast\backends\gfx_direct3d_common.h | 9CABFF0575238D3C0FD12CA5CE6116B6E977A0C065C341C95EA06170947E907A | 8471 |
| source\shipwright\libultraship\include\fast\backends\gfx_rendering_api.h | 126B3113B3773C4A39C105E2A52FDBF86672AB7C349D069141B69775FD6E6B67 | 3887 |
| source\shipwright\libultraship\src\ship\Context.cpp | 654D14BA5C8A5C025636FF58733D7B21C4151BC15C23438ED9F848F7581E571C | 15747 |
| source\shipwright\libultraship\src\ship\resource\ResourceManager.cpp | 14F05B4EA6A938DE2E647B1074FEF6AE37CB1D327ACFFBEE856E15B63927F4E1 | 20239 |
## Status Summary
- Prestate captures stored in: visual_workspace/v03_1_master/prestate/
- V03 candidate backed up in: visual_workspace/v03_1_master/runtime_backup/pre_master_v03/
- Lock Status: FREE / NO AGENT CONFLICT
