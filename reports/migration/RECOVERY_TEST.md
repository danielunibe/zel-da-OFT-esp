# REPORTE DE PRUEBA REAL DE RECUPERABILIDAD (RECOVERY TEST)
## Proyecto: Ocarina of Time PC — Couch Edition

**Fecha de Validación:** 17 de septiembre de 2026  
**Resultado Global:** **RECOVERY_TEST = PASS**  
**Metodología:** Verificación determinista en entorno aislado y validación estricta de parches contra upstream limpio.

---

## 1. Validación de Parches de Producción Canónicos contra Upstream Limpio
Se crearon worktrees temporales limpios y desacoplados para simular la clonación inicial desde los repositorios de Harbour Masters en sus commits fijados exactos:

### A. Shipwright Upstream (Commit `4aaad850bd5540cd77c2d83f3ad348d3b38605b2` - Bravo 9.1.1)
- **Comando:** `git apply --check --verbose patches/shipwright_couch_edition_v03_1_1.patch`
- **Resultado:** **EXIT CODE = 0 (100% LIMPIO)**
- **Archivos verificados sin conflicto:**
  - `.gitignore`
  - `CMakeLists.txt`
  - `soh/assets/custom/textures/buttons/LTBtn.i4.png`
  - `soh/include/glyph_resolver.h`
  - `soh/include/z64.h`
  - `soh/soh/Enhancements/FileSelectEnhancements.cpp`
  - `soh/soh/Enhancements/boss-rush/BossRush.cpp`
  - `soh/soh/Enhancements/custom-message/CustomMessageManager.cpp`
  - `soh/soh/Enhancements/custom-message/CustomMessageManager.h`
  - `soh/soh/Enhancements/debugger/MessageViewer.cpp`
  - `soh/soh/Enhancements/glyphs/GlyphResolver.cpp`
  - `soh/soh/OTRGlobals.cpp`
  - `soh/soh/ResourceManagerHelpers.cpp`
  - `soh/soh/ShipUtils.cpp`
  - `soh/soh/SohGui/SohMenu.h`
  - `soh/soh/SohGui/SohMenuSettings.cpp`
  - `soh/soh/z_message_OTR.cpp`
  - `soh/src/code/game.c`
  - `soh/src/code/z_kanfont.c`
  - `soh/src/code/z_message_PAL.c` (Hotfix de enrutamiento en español)
  - `soh/src/code/z_parameter.c`
  - `soh/src/overlays/actors/ovl_En_Mag/z_en_mag.c`
  - `soh/src/overlays/gamestates/ovl_file_choose/z_file_choose.c`
  - `soh/src/overlays/gamestates/ovl_file_choose/z_file_nameset_PAL.c`
  - `soh/src/overlays/misc/ovl_kaleido_scope/z_kaleido_map_PAL.c`
  - `soh/src/overlays/misc/ovl_kaleido_scope/z_kaleido_scope_PAL.c`

### B. LibUltraShip Upstream (Commit `17a0b7939bd05f5e617cef89457ca43774fc9a9f`)
- **Comando:** `git apply --check --verbose patches/libultraship_couch_edition_v03_1_1.patch`
- **Resultado:** **EXIT CODE = 0 (100% LIMPIO)**
- **Archivos verificados sin conflicto:**
  - `include/fast/MaterialRegistry.h`
  - `include/fast/MaterialRulesPilot.h` (1.407 reglas compiladas)
  - `include/fast/backends/gfx_direct3d_common.h`
  - `include/fast/backends/gfx_rendering_api.h`
  - `include/fast/interpreter.h`
  - `src/fast/MaterialRegistry.cpp`
  - `src/fast/backends/gfx_direct3d11.cpp` (Pipeline Enhanced & ACES)
  - `src/fast/interpreter.cpp`
  - `src/ship/Context.cpp`
  - `src/ship/resource/ResourceManager.cpp`

---

## 2. Validación de Integridad de Componentes Canónicos
Ejecución de suite `tests/validate_recovery.py`:
- [x] Ausencia de archivos con copyright de Nintendo (`oot.o2r`, `*.z64`, `*.n64`, `*.v64`).
- [x] Parches canónicos íntegros y parches obsoletos en `archive/legacy-patches/` con `DO_NOT_APPLY.md`.
- [x] `source-overlay/` completo y operativo con `apply_source_overlay.ps1`.
- [x] Colección completa de `material_intelligence_01` (CSVs de clasificación, PBR defaults, reglas JSON).
- [x] Ecosistema de localización español ES-419 preservado íntegramente.
- [x] Plantilla `runtime-template/` con instrucciones legales y recurso `mods/es.o2r`.
- [x] Documentación de recuperación (`docs/RECOVERY_FROM_ZERO.md`, `docs/PROJECT_STRUCTURE.md`).

---

## 3. Conclusión
El repositorio `zel-da-OFT-esp` contiene de forma autosuficiente, determinista y auditable todos los elementos necesarios para reproducir el desarrollo exacto de Couch Edition V03.1.1 en cualquier equipo nuevo.
```text
RECOVERY_TEST = PASS
```
