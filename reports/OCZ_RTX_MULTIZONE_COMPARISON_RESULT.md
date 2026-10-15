# OCZ RTX — Harness con comparación visual en cinco vistas

Fecha: 2026-10-14.

## Resultado

**Implementado y ejecutado:** comparador con/sin contribución DXR, deslizador, superposición, alternancia, overlay de diferencias y selección de efectos. Se completaron diez ejecuciones ocultas: `NORMAL` con controles GPU y `HIT_MISS` para cinco vistas de cuatro zonas del juego.

**PASS del alcance técnico:** capturas del mismo fotograma, contribución visible medible, intersecciones primarias con payload válido, parámetros de salida compartidos e integridad del harness. **PARTIAL en corrección visual global y calidad artística:** una diferencia no demuestra automáticamente que cada reflejo o sombra sea físicamente correcto.

## Comparación en la GPU

Se producen cinco salidas SDR en la lista de comandos que dibuja la escena:

1. Raster antes de las pasadas DXR.
2. Composición DXR completa.
3. Raster después de las sombras.
4. Composición sin reflejos/transmisión, conservando la emisión.
5. Composición sin adición de iluminación indirecta.

Las cinco salidas usan el shader de framebuffer de producción: FP16 lineal, ACES, niebla de escena y salida aproximada gamma 2.2. El intérprete publica esos parámetros antes del hook DXR y la pasada final los reutiliza. Se comparan geometría, cámara, profundidad, luz, resolución y solicitud del mismo fotograma. El HUD se dibuja después y queda excluido de ambos lados del par.

«Sin DXR» significa sin contribución de esas pasadas en la imagen. El lado con DXR se ejecuta en el mismo fotograma; esta prueba no mide FPS de un juego con DXR completamente desactivado ni cambia el perfil a Classic.

## Medidas finales

Cada imagen tiene 1280 × 720 píxeles. El porcentaje cuenta píxeles donde al menos un canal cambia más de dos códigos RGB. La media es la diferencia absoluta de los tres canales, en escala de 0 a 255.

| Vista | Píxeles con cambio > 2 códigos | Cambio RGB medio | Intersecciones primarias del fotograma completo |
|---|---:|---:|---:|
| Bosque Kokiri | 13,88 % | 0,988 / 255 | 92,87 % |
| Lago Hylia, agua | 55,25 % | 5,828 / 255 | 72,54 % |
| Templo del Tiempo, interior | 45,22 % | 6,344 / 255 | 81,67 % |
| Campo de Hyrule, día | 46,51 % | 1,700 / 255 | 71,27 % |
| Campo de Hyrule, noche | 34,55 % | 1,421 / 255 | 71,27 % |

El porcentaje de intersecciones tiene como denominador todo el fotograma; el fondo/cielo cuenta como miss. No es eficiencia sobre geometría solamente. No hay píxeles verdes sin payload de closest hit en las cinco ejecuciones de diagnóstico.

La observación de que Kokiri parecía casi igual es consistente con su cambio medio pequeño. El lago y el templo hacen más fácil localizar la contribución. Los umbrales y las diferencias son medidas descriptivas, no una certificación perceptual o artística.

La imagen «con DXR» del comparador coincide exactamente con la imagen final del juego en el rectángulo central evaluado en las cinco vistas. El rectángulo es `[0,2; 0,25]` a `[0,7; 0,8]` en coordenadas normalizadas. La coincidencia de todo el buffer es menor porque la captura final también contiene HUD y overlays de cinemática posteriores. No se presenta ese rectángulo como una comprobación de todos los píxeles.

## Visor y archivos

- [Visor autónomo](dxr_comparison/comparison-snapshot.html): contiene las 25 imágenes, empaquetadas como PNG sin pérdida. Se comprobó que los píxeles RGB empaquetados coinciden con las previsualizaciones GPU. Puede abrirse en un navegador sin servidor.
- [Dirección del visor local](dxr_comparison/viewer.server.json): visor de resultados que consulta nuevas capturas cada dos segundos. Solo escucha en `127.0.0.1`.
- [Verificación final](dxr_comparison/final-verification.json).
- [Resultados por ejecución](dxr_comparison/suite.status.json).
- [Guía de ejecución y controles](../tools/DXR_COMPARISON_README.md).
- [Verificación del visor](dxr_comparison/viewer-verification.json): selección de zonas/efectos, deslizador, extremos de opacidad, overlay y exportación PNG comprobados en el navegador.
- [Captura del visor final](dxr_comparison/viewer-final-lake-overlay.jpg).
- [PNG exportado desde el control del visor](dxr_comparison/viewer-export-lake-optics-overlay.png): 1280 × 720; diagnóstico de óptica con ampliación ×4.

El deslizador y el overlay responden inmediatamente sobre las imágenes recibidas. El visor incorpora cada zona cuando termina su captura. No es una transmisión continua del juego.

## Capturas A/B

### Lago Hylia

![Lago, sin DXR, con DXR y diferencia medida x4](scene_harness/20261014_114506_443_LAKE_HYLIA_DAY_NORMAL/comparison-summary.png)

### Templo del Tiempo

![Templo, sin DXR, con DXR y diferencia medida x4](scene_harness/20261014_114637_702_TEMPLE_INTERIOR_NORMAL/comparison-summary.png)

Las dos primeras columnas son capturas GPU. Solo la tercera amplifica la diferencia ×4 para diagnosticarla. Las imágenes comparadas no han sido retocadas para incrementar el efecto.

## Correcciones del harness

- Se generalizó el analizador de rayos: valida cada entrada, escena, habitación y vista contra el registro de esa ejecución. Se eliminó la restricción fija a Kokiri.
- Se añadieron día y noche en Hyrule y una cámara fija para incluir agua en el lago.
- La vista se fija antes de construir las matrices de dibujo. La lógica y los temporizadores de cámara pueden continuar; una transición no puede sustituir la vista del fixture.
- El bloqueo del jugador se aplica también antes y después del dibujo, porque la física y los golpes nocturnos se procesan después de `MainStart`.
- El Templo del Tiempo se corrigió a habitación 1. El índice de aparición 0 de la entrada no representa la habitación.
- Los controles SDR se guardan antes de cerrar el ack de las ocho etapas originales. No se reemplaza el circuito de captura existente.
- Los hashes del lanzador/verificador usan SHA-256 de .NET para no depender del autoload de `Get-FileHash` en PowerShell lanzado desde Python/MSBuild.
- La comparación comparte los parámetros reales de niebla con la salida normal del juego.

Las ejecuciones fallidas de cámara, habitación y estado nocturno se conservan. Las primeras comparaciones que usaban niebla cero se conservan como históricas y quedaron sustituidas por la tanda final, sin convertirlas en PASS de la salida actual.

## Identidad y límites

- Compilación Release final: código de salida 0. [Registro](../build/OCZ_RTX_HOTFIX_02_RELEASE/multizone-production-tone-build-final.log).
- Ejecutable probado: SHA-256 `cef068dd24b5c168aeeee4bef32bf9eabc6de9adb0f1ca9e31d2b27c4cc343e6`.
- [Paridad de fuente](OCZ_RTX_MULTIZONE_SOURCE_PARITY.json): PASS; 44 archivos de Shipwright y 57 de LibUltraShip coinciden en fuente, overlay y espejo de compilación. El runtime coincide con el ejecutable construido.
- ABI del shader 2, payload de reflexión de 120 bytes, constantes de fotograma de 1424 bytes. Fuente HLSL `00f4299f01edf8ebdc6121e5190078a788fd00b8a2218360503dc270932a99b8`.
- Diez procesos de juego cerrados al terminar. Partidas temporales y comprobaciones de integridad de partidas/O2R pasadas.
- Ventanas ocultas, sin foco y sin `Present`. No se realizó una prueba del monitor físico.
- Todas las señales requeridas son finitas y no se registró pérdida del dispositivo en la tanda final.
- `HIT_MISS` prueba intersecciones primarias. No es un conteo completo de todos los rayos secundarios de reflexión, transmisión, GI o sombra.
- Los estados generales del harness continúan `PARTIAL`, porque su verificador original no certifica arte o presentación física. La verificación adicional tiene alcance de comparación GPU por vista.
- La composición de sombras sigue siendo una aproximación híbrida de la contribución de la luz principal.

La cobertura actual es de cinco vistas. Áreas con lava, focos puntuales, niebla distinta, superficies alfa especiales y otras cámaras permanecen pendientes de cobertura; no se certifica todo el juego a partir de estas capturas.
