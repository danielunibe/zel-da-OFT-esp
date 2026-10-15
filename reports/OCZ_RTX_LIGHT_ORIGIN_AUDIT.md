# OCZ RTX — Origen de la iluminación del Templo del Tiempo

Fecha: 2026-10-14.

## Resultado y alcance

**Diagnóstico: el historial confirma fallos de procedencia. En el código actual se conectó una ruta de luz direccional por los triángulos exactos de las ventanas; la iluminación que llega al piso sigue sin certificar y no existe una fuente de área finita por ventana.**

Se revisaron el checkout canónico, 36 cabeceras de escena/habitación extraídas en modo lectura del `oot.o2r` del runtime aislado, las listas de dibujo y vértices originales de las ventanas y los registros de la ejecución `20261014_114637_702_TEMPLE_INTERIOR_NORMAL`. El registro y la captura son evidencia histórica del código anterior a esta corrección. Después del diagnóstico se corrigió la procedencia de la iluminación en el código y el verificador para impedir que una sonda por actor sustituya la luz nativa de escena. En esta continuación se generó DXIL del HLSL; no se compiló ni abrió la aplicación y no se hicieron pruebas GPU.

La comparación con/sin DXR anterior conserva su alcance: muestra diferencias y ejecución de las pasadas. `HIT_MISS` observa intersecciones de rayos primarios con geometría. Ninguna de esas comprobaciones certifica que la iluminación provenga de las ventanas o que los materiales reciban energía correcta. El verificador original de este run ya indicaba `productiveGameLight=false`, `SAME_FRAME_MISMATCH` y resultado general `PARTIAL`.

Datos reproducibles: [TEMPLE_LIGHT_ORIGIN_STATIC.json](lighting_origin/TEMPLE_LIGHT_ORIGIN_STATIC.json). Analizador: [audit_temple_light_origins.py](../tools/audit_temple_light_origins.py).

### Corrección de código hecha después de la auditoría

- `Lights_BindAll` inicia una captura nativa por fotograma; `BeginLightingFrame` conserva esa captura y reinicia solo la sonda Fast3D.
- `interpreter.cpp` ya no promueve luces por dibujo/actor al snapshot global que consume DXR.
- Las sombras/rayos y el composite de materiales usan la dirección nativa capturada. El composite ya no toma la sonda que cambia por actor.
- El sincronizador y la evidencia final del harness consultan el registro del mismo `capture_frame`. La coincidencia requiere una luz direccional válida y una entrega CPU coherente.

Estos cambios de la aplicación están **sin compilar ni ejecutar**. No certifican que la imagen resultante sea correcta.

## Preparación específica del Templo para comparación visual

En una extensión posterior de código fuente, se asociaron recursos concretos a materiales y se preparó el contrato del harness:

- La lista `tokinoma_room_1DL_002A58` usa `tokinoma_sceneTex_011FA0` en la geometría plana del piso (`tokinoma_room_1Vtx_002508`, Y local entre -40 y 0). La regla la clasifica como mosaico cerámico con reflectividad moderada; no como metal ni espejo perfecto.
- Las texturas `object_toki_objects_Tex_002DC0` y `003DC0` se asignaron a vidrio para las variantes de ventana de niño y adulto. `Tex_0035C0` se asignó al haz translúcido como material emisivo. **Ese haz sigue siendo un efecto artístico: la regla no crea energía física por sí sola desde la abertura hacia el piso.**
- `gFairyWingTex` recibe emisión suave y la luz puntual `LIGHT_POINT_GLOW` de Navi usa el promedio de sus colores interior/exterior actuales. El resultado DXR requiere comprobarse en el frame capturado.
- Se añadió `ShadowAngularRadius`, separado del número de muestras. El DXIL embebido se regeneró desde el HLSL actual, incluida la ruta de portal descrita más abajo; falta compilar la aplicación y comprobarla en ejecución.
- `TEMPLE_INTERIOR` exige coincidencia de luz direccional en el mismo fotograma, comparación DXR off/on y revisión de piso, ventanas, Navi y penumbra. Al ejecutar el escenario, el harness habilita la captura off/on y el verificador genera una imagen de overlay al 50% y una diferencia con ganancia 4. Las puertas visuales siguen en `NOT_VERIFIED`; el código de paso direccional por los paneles exactos de ventana está implementado, pero su gate de radiancia permanece `NOT_CERTIFIED` hasta obtener la captura.

La aplicación **no se compiló, el juego no se ejecutó y no se hicieron pruebas GPU**. El overlay, las diferencias y los resultados por material todavía no existen para esta revisión. No se declara que las sombras, la reflexión del mosaico, la luz de ventanas o el brillo de Navi funcionen visualmente.

## 1. La ejecución anterior entregó una dirección DXR incorrecta en el fotograma comparado

En `harness-backend.log`, línea 604, el registro `COUCH-DXR-LIGHT-SYNC` del **fotograma GPU 277** contiene:

| Dato | Valor |
|---|---|
| Dirección observada en Fast3D | `(0.000000, 0.887994, -0.459854)` |
| Dirección seleccionada para iluminación DXR | `(-0.379880, -0.683783, -0.623002)` |
| Diferencia angular | `108.70558 grados` |
| Misma generación de captura y entrega | `sameFrame=1` |
| Coincidencia de direcciones | `match=NO` |

No es una comparación entre fotogramas distintos. Entre los 273 fotogramas del Templo con registro en este archivo, 230 tienen `match=NO`. Es un conteo de ese run, no una estadística de todas las escenas o sesiones.

La dirección observada coincide con la normalización de `(0, 112, -58)` del ajuste original del Templo, que también contiene ambiente `(50,50,50)`, luz principal `(200,200,200)` y secundaria `(80,80,80)`. Estos valores son parámetros artísticos N64; no contienen una posición, área ni potencia física de una ventana.

**Matiz:** el probe del intérprete no demuestra por sí solo una fuente física. Además, `NoteGameDirectionalProbe` reemplaza direcciones distintas sin aplicar siempre la selección global por luminancia que anuncia su comentario. En este fotograma la referencia coincide con el ajuste original; no debe usarse el probe como única autoridad universal.

Los contadores `state.frames` y `game_frame` del harness tienen bases diferentes. El analizador relaciona la evidencia mediante `capture_frame`, sin equiparar esos contadores de juego.

## 2. Se borraba la captura original antes de consumirla; corregido en código

Ruta confirmada por código:

Antes del cambio, `Graph_Update` preparaba el dibujo y `Lights_BindAll(..., NULL)` capturaba luces originales, pero `Interpreter::StartFrame` después llamaba a `BeginLightingFrame`, que borraba el snapshot completo antes de DXR.

Ahora el bind de mundo inicia una captura nueva del juego con `CouchBeginNativeLightCapture` y enseguida registra sus luces originales. `BeginLightingFrame` ya no borra esa captura: reinicia las sondas por dibujo y marca qué snapshot nativo consume el fotograma de render. Así, la interpolación puede ejecutar varias listas sin reemplazar la fuente de luz por una observación de actor. Es una corrección de flujo de datos en código; todavía no está compilada ni comprobada en ejecución.

Referencias: `soh/src/code/graph.c:487`, `soh/src/code/z_play.c:1528`, `soh/src/code/z_lights.c:175`, `libultraship/src/fast/Fast3dWindow.cpp:314`, `libultraship/src/fast/interpreter.cpp:4949`, `libultraship/src/fast/MaterialRegistry.cpp:496`.

## 3. Una luz puntual podía convertirse en una falsa luz global; separada de la ruta productiva

`Actor_Draw` sigue enlazando luces en el espacio del actor y `Lights_BindPoint` puede convertir una luz puntual en un vector Fast3D relativo a ese actor. Esa observación ya no llama a `CaptureSceneLighting`: `interpreter.cpp` solo la conserva en los campos `gameProbe` de diagnóstico. La ruta productiva usa las direcciones, colores, ambiente y posiciones/radios capturados antes de la conversión.

El registro del run anterior conserva una dirección que no coincide con el ajuste nativo del Templo, pero no identifica la fuente concreta que produjo el candidato. El código actual bloquea esa promoción; la causa histórica del valor preciso sigue sin poder atribuirse a un actor sin instrumentación de identidad de fuente. No se atribuye a Navi ni a otro actor sin ese dato.

Referencias: `soh/src/code/z_actor.c:2741`, `soh/src/code/z_lights.c:117-140`, `libultraship/src/fast/interpreter.cpp:288`, `libultraship/src/fast/interpreter.cpp:459`, `libultraship/src/fast/MaterialRegistry.cpp:583-615`.

## 4. Las ventanas y los haces visibles son recursos de dibujo

El actor `Bg_Toki_Hikari`, parámetros `0`, dibuja:

| Recurso | Función confirmada |
|---|---|
| `object_toki_objects_DL_007E20` | Ventanas en la variante de Link niño; textura `Tex_002DC0` y 24 vértices |
| `object_toki_objects_DL_007EE0` | Efecto translúcido asociado, con textura `Tex_0035C0`, scroll y 62 vértices |
| `object_toki_objects_DL_008190` | Ventanas en la variante adulta; textura `Tex_003DC0` |

Las tres listas desactivan explícitamente `G_LIGHTING`. El efecto infantil cambia el alfa primario entre 127 y 204. `BgTokiHikari_Init/Update/Draw` no registra luces puntuales ni áreas emisoras: el aspecto brillante se obtiene mediante geometría, textura, color y mezcla. La ruta DXR actual marca los dos recursos de vidrio de ventana como portales de visibilidad para la luz direccional nativa; el actor con parámetros `1` es el efecto de apertura de la Puerta del Tiempo y debe tratarse aparte.

Esto permite ver haces en el raster sin que exista una fuente espacial equivalente en DXR. Añadir reflectividad o reducir rugosidad no crea esa fuente.

Referencias: `soh/src/overlays/actors/ovl_Bg_Toki_Hikari/z_bg_toki_hikari.c:85-105`; comandos y offsets binarios completos en el JSON.

### Posiciones obtenidas de los recursos originales

El actor de ventanas de la habitación 1 tiene posición `(-1,-39,1761)`, rotación cero y escala 1. Esa misma pose aparece en las cabeceras alternativas analizadas que incluyen el actor. Aplicando su transformación a la geometría se obtienen:

| Ventana | Centro de sus límites en mundo `(X,Y,Z)` | Plano |
|---|---|---|
| 0 | `(511,456,1521)` | `X=511` |
| 1 | `(511,456,2001)` | `X=511` |
| 2 | `(-515,461,2001)` | `X=-515` |
| 3 | `(-515,461,1521)` | `X=-515` |
| 4 | `(4,542,2400)` | `Z=2400` |

Son **anclas geométricas en unidades del juego**, no posiciones de lámparas ni centros de emisión calibrados. Las ventanas laterales tienen límites de altura de 280 unidades y anchura de 80; la posterior tiene 120 por 122. Las laterales tienen forma pentagonal, por lo que sus rectángulos de límites no deben usarse como aperturas completas.

Para una fuente espacial se necesita conservar el polígono, su orientación hacia la sala, transmisión, radiancia exterior y visibilidad. Crear cinco luces puntuales en estos centros daría otra aproximación visual, sin reproducir la entrada de luz de una ventana.

## 5. El respaldo del entorno no distingue este interior

La cabecera original contiene `indoors=1`, `skybox_id=0`. Las habitaciones contienen `sun_moon_disabled=1`. Esto desactiva su representación visible; **no significa que el juego original carezca de luces direccionales para sombrear actores**.

Sin embargo, `MissReflection` en `dxr_shadow.hlsl:1249-1270` construye un cielo procedural y dos lóbulos de luz a partir de ambiente y direccionales cuando no obtiene color de un fondo raster válido. `SceneLightingSnapshot` no incluye una política de interior, un entorno identificado ni aperturas de ventanas. El shader no sabe si el rayo salió por una ventana o si simplemente no encontró geometría capturada.

**Confirmado:** falta esa distinción en el contrato. **Sin medir en esta revisión:** cuántos rayos del Templo reciben ese respaldo y su contribución exacta al error visual. La diferencia GI del comparador no identifica por sí sola el origen de cada contribución.

## 6. El color capturado no siempre es un albedo limpio

Para dibujos con `G_LIGHTING`, el intérprete calcula ambiente y luz directa en `d->color` (`interpreter.cpp:1631-1640`). Después registra ese color en `GeometryCapture` (`1744-1750`). El gestor DXR copia ese dato al registro de vértice (`DxrAccelerationStructureManager.cpp:228`, `509`).

El shader forma `baseColor = sampledMaterialColor * vertexColor * material.baseColor` (`dxr_shadow.hlsl:546`) y vuelve a evaluar ambiente y luz directa (`1488-1537`). Por tanto, en esos dibujos un color ya iluminado se reutiliza como base de una segunda iluminación. Las texturas o colores con iluminación artística incorporada requieren también una política explícita.

Este problema puede alterar energía y respuesta de los materiales incluso después de corregir la dirección de las luces. Las listas concretas de ventanas desactivan `G_LIGHTING`; no se les atribuye esta segunda iluminación de vértice sin más evidencia.

## 7. Reglas de material añadidas; falta la fuente física de las ventanas

En la auditoría original no había reglas exactas para `object_toki_objects_Tex_002DC0`, `0035C0` y `003DC0`. Después se añadieron reglas de código fuente que asignan vidrio a las dos variantes de ventana y emisión suave al overlay del haz. También se asignó cerámica reflectante a `tokinoma_sceneTex_011FA0`, usada por la lista de suelo `tokinoma_room_1DL_002A58`. Son mapeos estáticos por recurso: aún falta confirmar qué variante se dibuja en el frame DXR y qué contribución llega a cada receptor.

La emisión del haz visible no equivale a una fuente física ubicada en el exterior. El código posterior sí permite que la visibilidad de la luz direccional nativa cruce los triángulos de los paneles exactos; no añade una fuente de área, transmisión tintada ni radiancia de cielo por ventana. No se debe deducir iluminación física de su brillo en una captura.

Las ventanas se dibujan con una lista opaca en el raster. El código usa sus recursos de vidrio exactos para dejar continuar los rayos de visibilidad y mantiene las intersecciones de cámara; falta confirmar en ejecución que esos draws estén presentes en TLAS y que los receptores reciban radiancia a través de ellos.

Referencias: `libultraship/src/fast/MaterialRegistry.cpp:83-147`, `219-240`; recursos resueltos por CRC64 en el JSON.

### Implementación de código añadida después de esta auditoría

Las reglas exactas del mosaico, ambos vidrios, el overlay del haz y las alas de hada ahora pueden prevalecer sobre una clasificación genérica del draw; el indicador interno de esa excepción se elimina antes de publicar el registro al shader. Esto hace efectiva la asignación semántica prevista cuando el recurso exacto coincide, pero todavía no confirma qué variante aparece en una captura ni que el mosaico reciba luz de la ventana.

El contrato `TEMPLE_WINDOW_MOSAIC_NAVI_V1` del scene harness solicita una pareja DXR apagado/encendido del mismo fotograma. Al verificar una captura válida, `verify-run.ps1` genera la mezcla de 50 %, una diferencia amplificada y un visor HTML intermitente. La generación queda pendiente hasta ejecutar una captura autorizada; ningún visor ni resultado visual se produjo en esta revisión.

Se agregó el control de radio angular de sombra suave al código HLSL y se regeneró `dxr_shadow_cso.h` desde esa fuente. El paso de luz direccional por ventanas está implementado como portal de visibilidad, no como fuente de área. No se compiló el juego ni se ejecutó una prueba gráfica; el contrato del harness mantiene la transferencia ventana→piso explícitamente sin certificar.

## Orden de corrección

1. **Conservar las definiciones nativas por fotograma del juego y transportarlas al render.** Implementado para las propiedades que expone `LightInfo` (tipo, posición/radio, color y dirección), junto con el ambiente y las dos direcciones globales. Falta conservar un identificador de fuente y comprobar cambios de escena en ejecución.
2. **Impedir la promoción de luces relativas a actores a direccionales globales.** Corregido en código: la sonda Fast3D por dibujo es solo diagnóstica; la ruta productiva conserva el snapshot nativo.
3. **Registrar el contexto de interior y el entorno autorizado.** La ausencia de intersección no debe implicar cielo exterior. Identificar la ruta de entrada por aperturas antes de añadir radiancia exterior.
4. **Representar las ventanas con geometría y una política de iluminación explícitas.** Implementado en fuente: los triángulos de los paneles identificados dejan pasar la luz direccional DXR y los demás oclusores conservan su función. La transmisión RGB y la fuente de área finita no están modeladas. Mantener separados el efecto artístico del haz y la energía que llega al suelo.
5. **Separar albedo, tintes artísticos e iluminación previa.** Se añadieron asignaciones de material por recurso para mosaico, vidrio, overlay de haz y alas de Navi. La separación entre albedo raster e iluminación previa sigue sin resolverse ni validarse.
6. **Corregir el criterio de procedencia del harness.** Corregido en `verify-run.ps1` y en la evidencia final de `SceneTestHarness.cpp`: ambos seleccionan el registro de `capture.ack.capture_frame`, en vez de aceptar el último registro global que puede pertenecer a otro fotograma. La coincidencia de dirección no certifica iluminación física.

La validación posterior debe relacionar un rayo de luz con **fuente identificada → punto de apertura → visibilidad → material receptor → contribución**. La oclusión de una apertura debe afectar únicamente a la iluminación que entra por ella; una luz local no debe girar la iluminación del Templo entero. El diagnóstico actual no declara esas condiciones logradas.

## Corrección de fuente por portal DXR (código; runtime pendiente)

Se añadió una ruta de luz solar a través de los paneles reales de las ventanas del Templo:

- Las reglas exactas de `Tex_002DC0` y `Tex_003DC0` publican `MATERIAL_FLAG_DXR_WINDOW_PORTAL` en el registro DXR. El flag sobrevive el empaquetado de materiales en el bit 5, separado de textura, candidato de alfa y escritura de profundidad.
- `AnyHitReflection` deja continuar los rayos de visibilidad directa (`probeMode=2`) al atravesar esos paneles. `AnyHitShadow` hace lo mismo en rayos de sombra, pero conserva el panel como intersección en el rayo de cámara que coteja la profundidad raster (`probeMode=1`). Los límites triangulados de cada panel y la geometría de marco, pared y techo siguen controlando el paso.
- La dirección y el color proceden de la luz direccional nativa de escena ya capturada. El rayo sale del material receptor hacia esa luz; si el camino cruza un panel identificado y alcanza el exterior sin otro oclusor, la contribución directa llega al receptor. No se generan luces puntuales a partir de los centros de las cajas de ventana ni se usa el overlay artístico como emisor.
- El contrato `TEMPLE_WINDOW_MOSAIC_NAVI_V1` ahora describe esta ruta y deja `window_to_floor_radiance` en `NOT_CERTIFIED` hasta una captura real y revisión. El bytecode DXIL fue regenerado desde el HLSL modificado; la aplicación no se compiló.

Esta ruta modela un portal de luz direccional, no una fuente de área con penumbra a escala de cada ventana. Las ventanas se tratan como paneles transparentes sin pérdida ni tinte en los rayos de visibilidad; el tono de luz continúa viniendo de la luz nativa de escena. La forma y distribución visibles en piso, el mosaico, Navi y la suavidad final requieren todavía una captura DXR del mismo fotograma. En esta continuación no se abre ni se prueba el juego, por lo que no se certifica el resultado visual.

## Reproducción estática

Desde la raíz del proyecto:

```powershell
py -3 tools/audit_temple_light_origins.py `
  --archive runtime/visual-acceptance-20261014-023309/oot.o2r `
  --run reports/scene_harness/20261014_114637_702_TEMPLE_INTERIOR_NORMAL `
  --output reports/lighting_origin/TEMPLE_LIGHT_ORIGIN_STATIC.json
```

El analizador abre el archivo en modo lectura, comprueba los tamaños completos de las estructuras decodificadas, resuelve referencias de recursos por el CRC64 del proyecto y escribe evidencia fuera del runtime. No modifica assets, guardados ni configuración del usuario. El JSON conserva hashes de las cabeceras y del archivo analizado; no certifica selección de assets alternativos en una nueva ejecución.
