# OCZ-RTX-REPAIR — recibo técnico

Fecha: 2026-10-13  
Estado general: **PARTIAL** — el código y el build Release están listos, pero la certificación con el juego en GPU no pudo ejecutarse porque falta el runtime de prueba.

## Cambios aplicados

- **REPAIR-01 — activación DXR:** el backend permite DXR solo para `HYBRID_RT` (2) y `PATH_TRACED_EXPERIMENTAL` (3), con adaptador compatible y TLAS válida. `CLASSIC` y `ENHANCED` conservan raster. Las variables de entorno no promueven el perfil; se omiten efectos cuando faltan recursos y se restaura el estado de raster.
- **REPAIR-02 — compositor de superficies:** Hybrid RT y Path Traced usan el fullscreen composite por defecto; `COUCH_DXR_SURFACE_COMPOSITE=0` permite desactivarlo de forma explícita. El pase valida el tamaño y los recursos del fotograma, enlaza sus descriptores y señales DXR, y conserva la escena raster cuando la composición no puede ejecutarse. La copia raster de captura mantiene una identidad distinta de la salida DXR.
- **REPAIR-03 — cobertura:** la política HLSL y el compositor comparten una máscara válida. Los materiales opacos no dependen de su canal alfa; los cutout conservan el umbral de descarte; los materiales alpha-blend/additive permanecen en raster salvo los materiales PBR transmisivos admitidos. Señales no finitas se rechazan y vuelven al pixel raster. La ruta normal no fuerza la cobertura a `1.0`.
- **REPAIR-04 — framebuffer:** la ruta de escena HDR recibe el composite antes de ACES; el resultado SDR se resuelve al swapchain antes del dibujo de UI. Las capturas distinguen escena HDR, composite, post-ACES y SDR pre-Present. La captura automática observa el backbuffer y el resultado de `Present`, pero no certifica scanout físico.
- **Harness y seguridad:** backend y perfil quedan explícitos por ejecución; el harness comprueba el backend y perfil activos, y adjunta evidencia del fotograma para TLAS, dispatch, señales y composite. El verificador exige las ocho etapas, sus metadatos y el mismo fotograma, calcula diferencias entre etapas y exige `saveIntegrity`. Se conserva la comprobación de hashes de OTR y de saves originales, con el save temporal aislado. Corregí además el ejecutable predeterminado del launcher: el build genera `soh.exe`, no `soh_dxr_harness.exe`.

`SaveManager.cpp` exige `COUCH_DXR_SAVE_ROOT` con el harness activo y falla si falta o resuelve al directorio Save normal. El launcher crea un directorio Save nuevo dentro del run, sin copiar una partida personal.

La corrección del ejecutable predeterminado quedó cubierta por una regresión en `tests/test_ocz_rtx_hotfix_02.py`. No se editaron recursos originales, ROM, archivos de idioma ni la salida estable V03.1.1. No se inició el juego durante estas verificaciones.

## Archivos de implementación relacionados

- Activación, estado y composición DXR: `source/shipwright/libultraship/include/fast/backends/DxrPipelinePolicy.h`, `DxrSequencePolicy.h`, `DxrShadowRenderer.h`, `DxrSurfaceRenderer.h`, `gfx_direct3d12.h`; `source/shipwright/libultraship/src/fast/backends/DxrAccelerationStructureManager.cpp`, `DxrShadowRenderer.cpp`, `DxrSurfaceRenderer.cpp`, `gfx_direct3d12.cpp`, `gfx_direct3d12_framebuffer.cpp`, `gfx_dxgi.cpp`; `source/shipwright/libultraship/src/fast/shaders/dxr_shadow.hlsl`.
- Perfiles y política de materiales: `source/shipwright/libultraship/include/fast/MaterialRegistry.h` y `source/shipwright/libultraship/src/fast/MaterialRegistry.cpp`.
- Harness y guardado aislado: `source/shipwright/soh/soh/Enhancements/scene-harness/SceneTestHarness.cpp`, `source/shipwright/soh/soh/SaveManager.cpp`, `source/shipwright/scripts/scene-harness/run-scene.ps1`, `verify-run.ps1` y `scenarios/scenarios.v1.json`.
- Regresiones: `tests/dxr_hotfix_02/CMakeLists.txt`, `tests/dxr_hotfix_02/policy_tests.cpp` y `tests/test_ocz_rtx_hotfix_02.py`.

## Build y pruebas

- `cmake --build build/OCZ_RTX_HOTFIX_02_RELEASE/cmake --config Release --target soh`: **PASS**, código de salida 0. El ejecutable aislado es `build/OCZ_RTX_HOTFIX_02_RELEASE/source_clean/x64/Release/soh.exe` (34,278,400 bytes; SHA-256 `FEB608390D9EE1436389C0B3E2328D6F661A660251598CDF0231DCDBFFD5A72B`). Los doce archivos C++/HLSL de reparación cotejados coinciden con la copia fuente compilada.
- `Generate-DxrShadowBytecode.ps1` con DXC (`-T lib_6_5 -HV 2021`): **PASS**. El bytecode generado desde el HLSL actual coincide con `cmake/generated/fast/backends/dxr_shadow_cso.h` del build.
- Los shaders del compositor embebidos en C++ compilaron con FXC (`vs_5_0` y `ps_5_0`): **PASS**. FXC emitió advertencias X3577 sobre `isnan`; la inspección del DXBC generado con las opciones de compilación normales confirma que las instrucciones de comparación siguen presentes. No hubo errores de shader.
- `python -m unittest discover -s tests -p test_ocz_rtx_hotfix_02.py -v`: **PASS**, 7/7.
- `ctest --test-dir build/OCZ_RTX_HOTFIX_02_POLICY -C Release --output-on-failure`: **PASS**, 1/1.
- `python tests/test_clean_patch_apply.py`: **PASS**; ambos parches aplican sobre worktrees limpios en las bases fijadas.
- Análisis sintáctico de `run-scene.ps1` y `verify-run.ps1`: **PASS**.
- Reexportación del snapshot: **PASS**, 42 archivos Shipwright y 57 LibUltraShip; manifiesto con 0 discrepancias SHA-256/tamaño. Los parches exportados conservan las bases `4aaad850bd5540cd77c2d83f3ad348d3b38605b2` y `17a0b7939bd05f5e617cef89457ca43774fc9a9f`.
- El árbol vcpkg fijado que esperaba el helper no estaba presente; la configuración aislada usó el commit `e456309491fd875487bf02a8b0dca76c1b1b00cc`. El build pasó, con esa diferencia como límite de reproducibilidad exacta.

El snapshot de desarrollo conserva otros cambios locales ya existentes: los 99 archivos del manifiesto no se atribuyen todos a esta reparación.

## Gates

| Gate | Estado | Evidencia y límite |
|---|---|---|
| **GATE-A — Compilación** | **PASS** | Build Release exitoso; DXC compiló el DXR HLSL, FXC compiló VS/PS del composite y las fuentes de reparación coinciden con el staging compilado. |
| **GATE-B — Ejecución DXR** | **BLOCKED** | `nvidia-smi -L` detectó una NVIDIA GeForce RTX 3070 Ti Laptop GPU. El preflight se detuvo con `HARNESS_RUNTIME_NOT_FOUND`: no existe `runtime/build-test` ni su `oot.o2r`; `soh.o2r` solo está en el staging aislado del build. No se lanzaron backend, TLAS, ray dispatch ni compositor en juego. |
| **GATE-C — Imagen** | **NOT_RUN** | No hay capturas GPU reales ni comparación visual; DXR visible, geometría y supervivencia de la señal hasta Present no están certificados. |
| **GATE-D — Harness** | **BLOCKED** | No se generaron las ocho capturas ni `evidence.json` de una ejecución; `saveIntegrity` no se pudo comprobar durante runtime. El preflight falló antes de arrancar el juego, así que no se leyó ni escribió ningún save. |
| **GATE-E — Regresión** | **PARTIAL** | Pasan las pruebas de política, parches y scripts. No se ejecutaron regresiones jugables Classic/Enhanced, HUD/controles, idioma, saves ni pruebas de `DEVICE_REMOVED`/`DEVICE_HUNG`. |

`DXR_VISUAL_VERIFIED` permanece **NOT_RUN**. No se generaron capturas sintéticas ni se declaró éxito visual. No se publica una nueva versión estable.

## Overlay y parches

- Overlay: `source-overlay/development/shipwright/` y `source-overlay/development/libultraship/`.
- Parches: `patches/development/shipwright_9_1_1_development.patch` y `patches/development/libultraship_1_3_1_development.patch`.
- Manifiesto: `docs/development/SNAPSHOT_FILES.json`.
- Recibo: `reports/OCZ_RTX_REPAIR_RESULT.md`.
