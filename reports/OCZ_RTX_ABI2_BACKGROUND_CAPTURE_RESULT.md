# OCZ RTX — Capturas GPU en segundo plano de la fuente corregida

Fecha: 2026-10-14. Petición: comprobar rayos y sombras mediante capturas en segundo plano.

## Resultado

**PASS dentro del alcance de esta vista de Kokiri:** hay intersecciones primarias reales con datos de material y una contribución medible de sombras DXR a la imagen. La captura normal tiene 1280 × 720 píxeles. No se mostró la ventana, no se tomó el foco y no se llamó a `Present`.

Esto confirma funcionamiento en la GPU y aplicación a la imagen capturada. La corrección visual de todas las escenas, los reflejos de todos los materiales y la calidad artística de un remaster continúan **PARTIAL / sin certificación global**. No se realizó una comprobación del monitor físico.

## Identidad compilada

- Ejecutable probado: `runtime/visual-acceptance-20261014-023309/soh.exe`.
- SHA-256: `fd1719c47eca057a606f78c9283eb3a1a1d2b7c97579b1f07ee0a9142d466fdd`.
- GPU registrada: NVIDIA GeForce RTX 3070 Ti Laptop GPU; Direct3D 12 con Debug Layer habilitada.
- ABI CPU/GPU: 2; payload de reflexión: 120 bytes; constantes de fotograma: 1424 bytes.
- SHA-256 de la fuente HLSL cargada: `00f4299f01edf8ebdc6121e5190078a788fd00b8a2218360503dc270932a99b8`.
- [Paridad actual](OCZ_RTX_ABI2_BACKGROUND_SOURCE_PARITY.json): los 43 archivos de Shipwright y 57 de LibUltraShip coinciden entre fuente canónica, overlay y espejo de compilación. El ejecutable probado coincide con el compilado.
- Compilación Release: código de salida 0; [registro](../build/OCZ_RTX_HOTFIX_02_RELEASE/abi2-background-capture-build-fixed.log).

La primera compilación falló porque `Get-FileHash` no estaba disponible en el PowerShell lanzado por MSBuild. El generador ahora usa SHA-256 de .NET para comprobar que la fuente no cambió durante la generación. Se conservan el registro fallido y los ejecutables anteriores en copias `soh-before-abi2-background-capture.exe`.

## Medidas de la GPU

| Comprobación | Medida actual | Alcance |
|---|---:|---|
| Intersecciones primarias con payload válido | 856.208 píxeles; 92,9045 % del fotograma completo | El rojo incluye fondo o miss; el porcentaje no es eficiencia sobre geometría solamente. |
| Verde sin payload de closest hit | 0 | No se cuentan salidas verdes sin datos reales de material. |
| Sombras, antes/después de su pasada | 65.154 píxeles modificados; 63.200 oscurecidos con delta > 0,001 | Máximo cambio RGB lineal: 0,30712890625. Ningún píxel se iluminó por esta pasada. |
| Reflejos/transmisión, control desactivado | 455.094 píxeles modificados | Control del mismo fotograma y señales; conserva la emisión. Muchos cambios son pequeños; no demuestra por sí solo corrección física de cada reflejo. |
| Iluminación indirecta, control desactivado | 770.575 píxeles modificados | 0 píxeles oscurecidos por la adición de GI con el umbral del analizador. |
| Valores no finitos | 0 | Todas las señales requeridas son finitas. |

Se comparan buffers de la misma solicitud y fotograma, antes del tonemapping. Los controles de composición se generaron en la misma lista de comandos GPU. El diagnóstico HIT_MISS es otra ejecución con la misma cámara y estado de escenario; no se usa para atribuir diferencias de color entre ejecuciones.

## Capturas

### Imagen normal del juego

![Captura GPU normal](scene_harness/20261014_105426_083_BASELINE_KOKIRI_NORMAL/captures/final_sdr_pre_present_frame277_opaque_preview.png)

Esta imagen procede del buffer SDR final, antes de la presentación, con la previsualización opaca guardada por el capturador.

### Intersecciones de rayos

![HIT_MISS](scene_harness/20261014_105604_710_BASELINE_KOKIRI_HIT_MISS/captures/final_sdr_pre_present_frame277_opaque_preview.png)

Verde: intersección primaria real y payload de material válido. Rojo: miss o fondo. El HUD se dibuja después del diagnóstico y conserva sus colores.

### Contribución de las sombras

![Contribución medida de sombras](scene_harness/20261014_105426_083_BASELINE_KOKIRI_NORMAL/shadow-contribution-measured.png)

Este gráfico numérico se calcula a partir de los dos buffers HDR capturados. Muestra dónde la pasada de sombras redujo el RGB lineal. Usa escala logarítmica para visualizar cambios pequeños; no representa los colores finales del juego. La fórmula y la escala se guardan en el [archivo de procedencia](scene_harness/20261014_105426_083_BASELINE_KOKIRI_NORMAL/shadow-contribution-measured.provenance.json).

## Evidencia y límites

- [Aplicación a la imagen, controles GPU](scene_harness/20261014_105426_083_BASELINE_KOKIRI_NORMAL/visual-acceptance.json): `PASS_SCOPED_VISUAL_APPLICATION`.
- [Intersecciones de rayos](scene_harness/20261014_105604_710_BASELINE_KOKIRI_HIT_MISS/primary-ray-analysis.json): `PASS_SCOPED_GPU_OBSERVATION`.
- [Presentación de NORMAL](scene_harness/20261014_105426_083_BASELINE_KOKIRI_NORMAL/presentation.evidence.jsonl) y [HIT_MISS](scene_harness/20261014_105604_710_BASELINE_KOKIRI_HIT_MISS/presentation.evidence.jsonl): `SKIPPED_BACKGROUND`, ventana visible `false`, foreground `false`.
- Ambos juegos de capturas están completos, coinciden en fotograma dentro de cada ejecución y no contienen fallos del verificador. Sin pérdida del dispositivo GPU registrada.
- Ambos procesos de prueba fueron detenidos. Las comprobaciones de partidas y archivos O2R pasaron; se usaron partidas temporales del harness.
- El estado general del harness permanece `PARTIAL`: su verificador general no consume los controles independientes de composición para certificar arte, ni esta captura oculta puede certificar el monitor. No se cambió ese estado para presentar una aceptación global.
- La composición de sombras sigue siendo una aproximación híbrida de la contribución de la luz principal. No se presenta como una solución físicamente exacta por material.

No se ejecutaron pruebas visibles, captura del escritorio ni otros escenarios en esta petición.
