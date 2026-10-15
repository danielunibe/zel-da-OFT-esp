# OCZ-RTX-HOTFIX-02 — Resultado de reparación y validación

Fecha: 2026-10-13

## Resumen de estado

| Área | Resultado | Evidencia / límite |
|---|---|---|
| Reparaciones estáticas | PASS | Política de perfiles, evidencia DXR por fotograma, contratos de captura, secuencia V1/V2 y observación de Present actualizados. |
| Build Release aislado | PASS | CMake configure y generación de OTR concluyeron; build finalizó con código 0. |
| Regresiones HOTFIX-02 | PASS | Python: 6/6; CTest: 1/1. |
| Aplicación limpia de parches | PASS | La prueba existente de V03.1.1 terminó correctamente; ambos parches de desarrollo aplicaron sobre worktrees limpios tras la reexportación. |
| Runtime GPU / harness | NOT_RUN | No existe runtime/build-test ni oot.o2r en el paquete. El build produjo soh.o2r solo en staging; no se fabricó un paquete de juego. |
| CAPTURE_COMPLETE | NOT_RUN | No se ejecutaron las ocho capturas en un runtime real. |
| DXR_VISUAL_VERIFIED | NOT_RUN / PARTIAL | No hay evidencia de captura en GPU ni prueba causal de ablación. No se declara éxito visual. |
| Presentación física | NOT_VERIFIABLE | Se observa HRESULT de Present y backbuffer previo a Present; no se certifica scanout físico. |
| Integridad formal de partidas | NOT_RUN | No se abrió runtime ni se tocaron partidas. No se ejecutó una auditoría de hashes de saves para este hotfix. |
| Idioma y recursos originales | PRESERVADOS | No se editaron recursos originales ni archivos de localización como parte de la validación; generación de OTR limitada al staging aislado. |

## Las seis correcciones

1. **Política única de perfil DXR.** Se centralizó la clasificación de perfiles en helpers de política/materiales. Hybrid RT y Path Traced Experimental tienen identificadores permitidos explícitos; Classic y Enhanced siguen separados. Se retiró el uso inconsistente de hybridRtProfile y se corrigió la referencia de ForensicLog que inicialmente impedía compilar.
2. **Ejecución DXR sin falsos positivos.** La evidencia ahora distingue solicitud, compatibilidad del adaptador, TLAS válida, dispatch, señales producidas y composición para el mismo fotograma. mDxrPassesRunThisFrame se deriva de trabajo acreditado, no de entrar al camino DXR antes de validar la TLAS.
3. **PASS del harness basado en contenido.** La finalización de capturas valida ocho archivos, identidad de solicitud/fotograma, dimensiones, formato, integridad/legibilidad PNG y señales finitas aplicables. CAPTURE_COMPLETE queda separado de DXR_VISUAL_VERIFIED. La segunda etiqueta requiere evidencia causal de contribución y no se obtiene de contadores, archivos o diferencias arbitrarias de píxeles.
4. **Identidad de captura correcta.** La copia de escena raster se etiqueta RASTER_SCENE_COLOR_COPY; la captura final anterior a Present se identifica FINAL_SDR_PRE_PRESENT. Se conservan las barreras/readback correspondientes para la señal a observar; una textura raster no se denomina sombreado DXR.
5. **Truth Mode V1 y V2.** COUCH_DXR_TRUTH_SEQUENCE=1 selecciona V1 por sí solo. V2, la precedencia de modos, las ventanas por etapa y la terminación explícita se conservan/definen en la política de secuencia. Las pruebas cubren selección, límites y terminación.
6. **Backbuffer separado de presentación.** La captura se declara previa a Present. EndFrame registra el HRESULT de Present después de la llamada y las observaciones de backbuffer disponibles, sin añadir esperas GPU. La ruta marca physicalScanout=NOT_VERIFIABLE.

## Archivos y exportación

Cambios principales en el checkout funcional source/shipwright:

- libultraship/include/fast/backends/DxrPipelinePolicy.h y DxrSequencePolicy.h
- libultraship/include/fast/MaterialRegistry.h y headers de renderers DXR
- libultraship/src/fast/backends/gfx_direct3d12.cpp, gfx_dxgi.cpp, DxrShadowRenderer.cpp y DxrSurfaceRenderer.cpp
- soh/soh/Enhancements/scene-harness/SceneTestHarness.cpp
- scripts/scene-harness/run-scene.ps1, verify-run.ps1 y scenarios/scenarios.v1.json
- tests/dxr_hotfix_02/CMakeLists.txt, tests/dxr_hotfix_02/policy_tests.cpp y tests/test_ocz_rtx_hotfix_02.py

La exportación de desarrollo contiene 42 archivos Shipwright y 57 libultraship. Se comprobaron los tamaños y SHA-256 del overlay y de los dos parches: cero discrepancias. Los cambios locales preexistentes del checkout se conservaron; el manifiesto es un snapshot de ese estado de desarrollo y no implica que cada archivo enumerado haya sido creado por HOTFIX-02.

Checkout funcional: source/shipwright, base Shipwright 4aaad850bd5540cd77c2d83f3ad348d3b38605b2 y libultraship 17a0b7939bd05f5e617cef89457ca43774fc9a9f. No se alteró la salida estable V03.1.1.

## Compilación

Build aislado bajo build/OCZ_RTX_HOTFIX_02_RELEASE. Se creó source_clean excluyendo los directorios build/x64 existentes. GenerateSohOtr finalizó con código 0 y generó soh.o2r de 1,184,426 bytes dentro de source_clean. El build Release finalizó con RELEASE_BUILD_RETRY_EXIT=0 y produjo:

- Ejecutable: build/OCZ_RTX_HOTFIX_02_RELEASE/source_clean/x64/Release/soh.exe
- Tamaño: 34,278,400 bytes
- SHA-256: FEB608390D9EE1436389C0B3E2328D6F661A660251598CDF0231DCDBFFD5A72B

MSVC mostró advertencias de conversión y retorno en fuentes existentes del motor; el build no reportó errores. El árbol vcpkg fijado que esperaba el helper no estaba presente. CMake hizo bootstrap de vcpkg desde el commit e456309491fd875487bf02b8a0dca76c1b1b00cc; el build pasó, con esa diferencia de versión como límite de reproducibilidad exacta.

## Pruebas ejecutadas

- python -m unittest discover -s tests -p test_ocz_rtx_hotfix_02.py -v: 6/6 PASS.
- ctest --test-dir build/OCZ_RTX_HOTFIX_02_POLICY -C Release --output-on-failure: 1/1 PASS.
- python tests/test_clean_patch_apply.py: PASS para Shipwright y libultraship sobre worktrees limpios.
- Los dos parches de desarrollo reexportados pasaron git apply --check y git apply en worktrees limpios.
- Los scripts PowerShell run-scene.ps1 y verify-run.ps1 pasaron el análisis sintáctico.
- git diff --check de los archivos del hotfix: PASS. El diff global aún señala un whitespace preexistente en libultraship/src/fast/backends/gfx_direct3d11.cpp:940; no se modificó durante este trabajo.

## Bloqueos y límites restantes

La máquina tiene una NVIDIA GeForce RTX 3070 Ti Laptop GPU, pero el paquete carece de runtime/build-test y de oot.o2r. Por eso no se lanzó soh.exe ni el harness; DXR, composición y las ocho capturas quedan NOT_RUN. El OTR generado en el staging es soh.o2r y no sustituye el asset ausente. No se certificó presentación física ni integridad de partidas mediante ejecución: no se tocó ningún save.

Queda una carpeta parcial de staging anterior, build/OCZ_RTX_HOTFIX_02_RELEASE/source, sin uso para el build exitoso; se compiló desde source_clean. La revisión automática de seguridad rechazó la eliminación recursiva de esa carpeta, así que se conserva como scratch local ignorado. No afecta source/shipwright, runtime, saves ni el ejecutable generado.
