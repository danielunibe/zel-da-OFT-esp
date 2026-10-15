# OCZ RTX — Correcciones de fuente para sombras, reflejos y presentación

Fecha: 2026-10-14. Este documento registra la fase **SOLO CÓDIGO** solicitada antes de las pruebas.

Estado: **correcciones implementadas y revisión estática terminada; compilación y validación posteriores pospuestas**. Este documento no convierte el código nuevo en una certificación visual del juego.

Actualización posterior: el usuario autorizó capturas en segundo plano. La compilación y los resultados GPU actuales están en [OCZ_RTX_ABI2_BACKGROUND_CAPTURE_RESULT.md](OCZ_RTX_ABI2_BACKGROUND_CAPTURE_RESULT.md); los estados pospuestos de este documento corresponden a la fase histórica.

## Correcciones

| Problema localizado en fuente | Corrección implementada |
|---|---|
| La máscara de sombras reconstruía un punto de profundidad y desplazaba el rayo en dirección a la luz. | Intersección primaria desde el plano cercano, correspondencia con la profundidad raster y desplazamiento sobre la normal del triángulo. |
| Las normales de relieve y ondas podían colocar reflejos, transmisión o GI dentro de la geometría. | Normal geométrica separada de la normal de iluminación. Orígenes colocados en el lado correspondiente a la dirección del rayo; rechazo de lóbulos que cruzan el horizonte geométrico. |
| Los reflejos productivos terminaban a 256 unidades, compartiendo el parámetro del diagnóstico de distancia. | Rangos propios: reflexión y transmisión, 10,000 unidades por defecto; GI, 256. La escala de `HIT_DISTANCE` conserva 256. Los rangos tienen controles de valores finitos. |
| Agua y vidrio mezclaban Fresnel en raygen y volvían a aplicar su peso en el compositor. | Mezcla RGB de reflexión/transmisión una vez en raygen; el compositor aplica la fuerza de transmisión. La fuerza de reflexión sigue funcionando en superficies opacas y transmisivas. |
| La reflexión interna total reutilizaba el origen y el modo de un rayo transmitido. | Dirección reflejada, origen en el lado geométrico correcto, modo de reflexión y manejo de vectores inválidos. |
| Un miss de reflexión podía muestrear geometría raster visible como entorno. | El muestreo de entorno raster requiere profundidad de cielo; los demás misses usan el entorno de iluminación de la escena. |
| Las ondas del agua usaban ejes de cámara. | Base tangente en espacio del mundo y conversión al espacio de la TLAS. |
| La composición multiplicativa de sombras reducía el color raster completo sin acotar su parte de luz principal. | Intensidad acotada por la proporción de la luz principal frente a luz ambiental y secundaria. Es una aproximación del perfil híbrido, no una separación exacta de términos de iluminación por material. |
| `WM_DPICHANGED` publicaba dimensiones exteriores después de recibir `WM_SIZE`. | El backend conserva dimensiones de cliente obtenidas con `GetClientRect`. |
| El capturador obtenía tamaños y posiciones mediante llamadas de PowerShell con contexto DPI separado. | Conversión de ambas esquinas del cliente a píxeles físicos y captura en una sola llamada nativa, con restauración del contexto DPI. No se redimensiona la imagen para hacer coincidir una comparación. |
| El ack de las ocho etapas cerraba la solicitud antes de guardar los dos controles de composición. | Los controles opcionales se guardan primero. Sus recursos se generan una sola vez en el fotograma de captura. |
| Los contratos CPU/GPU nuevos podían confundirse con bytecode anterior. | ABI 2, payload de reflexión de 120 bytes y constantes de 1424 bytes dentro del stride de 1536. El generador etiqueta el bytecode con ABI y SHA-256 de fuente; ambos pipelines comprueban la ABI y registran la identidad cargada. |

La normal geométrica determina dónde empieza un rayo; la normal de detalle determina cómo responde la iluminación. El uso de normales geométricas para evitar autointersecciones sigue el principio documentado por [NVIDIA](https://developer.nvidia.com/blog/solving-self-intersection-artifacts-in-directx-raytracing/). El capturador usa la conversión entre espacios lógico y físico definida por [Microsoft](https://learn.microsoft.com/es-es/windows/win32/api/winuser/nf-winuser-logicaltophysicalpointforpermonitordpi).

## Archivos principales

- `source/shipwright/libultraship/src/fast/shaders/dxr_shadow.hlsl`
- `source/shipwright/libultraship/src/fast/backends/DxrSurfaceRenderer.cpp`
- `source/shipwright/libultraship/src/fast/backends/DxrShadowRenderer.cpp`
- `source/shipwright/libultraship/include/fast/backends/DxrAccelerationStructureManager.h`
- `source/shipwright/libultraship/include/fast/backends/DxrSurfaceRenderer.h`
- `source/shipwright/libultraship/src/fast/backends/gfx_direct3d12.cpp`
- `source/shipwright/libultraship/src/fast/backends/gfx_dxgi.cpp`
- `source/shipwright/scripts/Generate-DxrShadowBytecode.ps1`
- `source/shipwright/scripts/scene-harness/capture-client.ps1`
- `source/shipwright/scripts/scene-harness/run-scene.ps1`
- `tools/analyze_visual_acceptance.py`

El analizador exige controles y SDR del mismo fotograma/solicitud, dimensiones coherentes, señales finitas, integridad y cierre del proceso. La comparación de escritorio requiere captura en píxeles físicos, ventana visible, foreground y DWM completado. La falta de evidencia produce un resultado de fallo explícito.

## Estado de los artefactos y validación

- Las pruebas anteriores corresponden a ejecutables anteriores a las últimas modificaciones de fuente.
- La ejecución visible anterior midió aplicación de sombras, óptica e indirecta sobre el mismo fotograma, pero su captura de escritorio tenía dimensiones incorrectas. Ese resultado permanece fallido; no se reutiliza como certificación de estas correcciones.
- Desde la instrucción de concentrarse en código no se ejecutaron nuevas compilaciones, shaders, pruebas, lanzamientos ni capturas.
- El header de bytecode empaquetado y los ejecutables existentes todavía representan la compilación anterior. Debe regenerarse DXIL antes de usar esta fuente con la ABI 2.
- El `CMakeLists.txt` canónico ya exige DXC, depende del shader y del generador y prioriza su header generado. La siguiente compilación normal regenerará ese artefacto; una inclusión manual del header antiguo no satisface la nueva comprobación de ABI.
- El overlay y los parches de desarrollo contienen el código actual. No se ejecutó una comprobación de aplicación de parches ni paridad contra el espejo de compilación en esta fase.

La siguiente fase es regenerar/compilar y validar Kokiri, agua e interior, seguida de la comparación de presentación. El acabado artístico global de un remaster y la inspección física del panel siguen requiriendo su evidencia propia.
