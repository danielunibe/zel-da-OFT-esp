# Comparación DXR del harness

El harness produce pares A/B desde la misma lista de comandos GPU. Ambos lados tienen la misma geometría raster, cámara, profundidad, resolución, tonemapping y niebla de la escena. El HUD se dibuja después y está excluido del par.

## Qué compara

| Par del visor | Contribución aislada |
|---|---|
| Todas las pasadas DXR | Imagen después del compositor frente al raster anterior a las sombras DXR. |
| Sombras | Raster después de las sombras frente al raster anterior. |
| Reflejos / transmisión | Composición completa frente al control sin óptica. La emisión se conserva. |
| Iluminación indirecta | Composición completa frente al control sin adición de GI. |

«Sin DXR» describe la imagen sin contribución de esas pasadas. El harness también ejecuta el lado con DXR para comparar el mismo fotograma; no es un cambio a Classic ni una medida de rendimiento de DXR desactivado.

## Escenarios

- `BASELINE_KOKIRI`: vista de aparición normal.
- `LAKE_HYLIA_DAY`: vista fija del lago que incluye agua.
- `TEMPLE_INTERIOR`: Templo del Tiempo, entrada `0x053`, escena `0x43`, habitación **1**. El índice de aparición 0 no es el índice de habitación.
- `HYRULE_FIELD_DAY`: exterior de Hyrule de día.
- `HYRULE_FIELD_NIGHT`: el mismo exterior a una hora nocturna.

El registro está en `source/shipwright/scripts/scene-harness/scenarios/scenarios.v1.json`. Una correspondencia declarada no basta: el runtime debe alcanzar su escena y habitación, y mantener cámara y jugador estables durante 30 fotogramas.

## Ejecutar

Desde la raíz del proyecto, después de compilar y comprobar que el runtime aislado contiene ese ejecutable:

```powershell
py -3 tools/run_dxr_comparison_suite.py --runtime runtime/visual-acceptance-20261014-023309
```

La suite ejecuta `NORMAL` con los controles A/B y luego `HIT_MISS` por cada zona. Lanza una sola instancia de juego a la vez. Usa el lanzador existente con `-Background`, partidas temporales, audio nulo, D3D12, perfil híbrido 2 y calidad 3. El lanzador cierra su propio proceso en `finally`.

Para seleccionar zonas:

```powershell
py -3 tools/run_dxr_comparison_suite.py --runtime runtime/visual-acceptance-20261014-023309 --scenarios LAKE_HYLIA_DAY TEMPLE_INTERIOR
```

## Visor

```powershell
py -3 tools/serve_dxr_comparison.py
```

La consola informa `VIEWER_URL`. Su dirección y PID se guardan en `reports/dxr_comparison/viewer.server.json`. Solo sirve resultados y PNG por `127.0.0.1`; no tiene endpoints para ejecutar el juego o modificar archivos. Se cierra después de dos horas sin solicitudes.

El visor permite:

- Deslizador entre ambas imágenes.
- Superposición con opacidad ajustable.
- Alternancia visual cada medio segundo.
- Overlay de las diferencias, con ampliación identificada.
- Diferencia absoluta por canal.
- Selección de zona y contribución.
- Exportación PNG de la comparación mostrada.

En la parte superior, el panel **Ventanas, mosaico y luz de Navi** lee directamente el contrato `TEMPLE_INTERIOR` de `source/shipwright/scripts/scene-harness/scenarios/scenarios.v1.json`. Expone la ruta de luz nativa por los paneles exactos, los materiales asignados y el estado de cada puerta visual. La ruta portal figura en código; la llegada de luz al mosaico, los reflejos, Navi y la penumbra permanecen pendientes hasta capturar la revisión compilada. Las imágenes A/B ya guardadas pueden pertenecer a otro ejecutable y no certifican ese cambio.

Para conservar una revisión sin servidor:

```powershell
py -3 tools/export_dxr_comparison_snapshot.py
```

El archivo `reports/dxr_comparison/comparison-snapshot.html` incluye las 25 imágenes PNG sin pérdida y los controles del visor. Se abre en un navegador sin conexión y no consulta nuevos resultados.

Los controles responden sobre las capturas recibidas. El visor consulta resultados cada dos segundos y añade una zona cuando su captura queda completa. **No es una transmisión continua del juego ni una prueba de FPS.**

## Evidencia

Cada ejecución mantiene:

- Las ocho etapas originales y los dos controles HDR de composición.
- Cinco imágenes SDR renderizadas con el shader de framebuffer de producción y los parámetros reales de niebla.
- Metadatos de escena, habitación, vista, fotograma y solicitud por cada imagen.
- `comparison-analysis.json`, con hashes y diferencias en códigos RGB de salida.
- `comparison-summary.png`: sin DXR, con DXR y diferencia amplificada ×4. Las dos primeras imágenes conservan sus colores capturados; la tercera es un diagnóstico.
- `primary-ray-analysis.json`, para la ejecución `HIT_MISS`.
- Registro de ventana oculta, ausencia de `Present`, limpieza del proceso e integridad de partidas/O2R.

`analyze_dxr_comparison.py` rechaza imágenes de solicitudes distintas y controles que no tengan el marcador de parámetros de salida compartidos. Las primeras comparaciones con niebla cero se conservan como históricas y deben repetirse con la fuente final.

## Interpretar los resultados

Se cuentan cambios por encima de 0, 2, 8, 16 y 32 códigos RGB. Esos umbrales describen magnitud; no son una certificación perceptual. Una diferencia numérica o un mapa muy amplificado no demuestra una mejora artística. `HIT_MISS` comprueba intersecciones primarias con payload válido y no certifica cada rayo secundario de reflexión o sombra.

El resultado `PASS_SCOPED_SAME_FRAME_GPU_COMPARISON` confirma integridad de la comparación. La calidad de los materiales, la corrección física de los reflejos y el remaster completo requieren revisión de las escenas capturadas y más cobertura.
