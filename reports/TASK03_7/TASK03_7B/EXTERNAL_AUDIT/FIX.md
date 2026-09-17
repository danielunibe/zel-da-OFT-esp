# TASK03.7B — External Audit Fix

Fecha: 2026-09-15

Alcance: `tools/hardware_validation/ControllerValidation.cpp` y el boot trace del launcher. No se modificaron Shipwright source, runtime, live project, saves ni se inició Task 04. No se realizó validación física completa.

## Hallazgos y estado

1. **Right stick leído como left stick** — FOUND: `sampleStick` no recibía ejes explícitos en el uso anterior. FIXED: ahora recibe `SDL_CONTROLLER_AXIS_LEFTX/LEFTY` o `SDL_CONTROLLER_AXIS_RIGHTX/RIGHTY`; RIGHT_CENTER, RIGHT_FULL_RANGE y diagonales leen RIGHTX/RIGHTY. VERIFIED: revisión del source y recompilación exitosa.

2. **Conexión hardcodeada como Bluetooth** — FOUND: console, log y JSON afirmaban Bluetooth. FIXED: `connectionType()` usa únicamente la ruta SDL cuando contiene evidencia USB/Bluetooth; en otro caso devuelve `UNKNOWN`. VERIFIED: smoke observado reportó `CONNECTION,UNKNOWN` y `Conexion: UNKNOWN`.

3. **overall_result y user_confirmations hardcodeados** — FOUND: JSON emitía `PASS` y `true`. FIXED: se calculan desde los estados reales; `PASS` sólo con todos los estados requeridos PASS, `FAIL` ante FAIL/UNSUPPORTED y `PARTIAL` ante UNVERIFIED. VERIFIED: cálculo presente en source recompilado.

4. **Errores de rumble ignorados** — FOUND: el resultado de `rumbleConfirmation()` no se conservaba. FIXED: cada pulso usa `RumbleResult`; error SDL queda `UNSUPPORTED` y afecta el resultado global. VERIFIED: llamadas y agregación revisadas.

5. **Inicio por A podía descartar BUTTONDOWN** — FOUND: se dependía también del estado mantenido y el `break` sólo salía del bucle interno. FIXED: `bool started = false`; `SDL_CONTROLLERBUTTONDOWN` de A establece el flag y termina correctamente el bucle exterior. VERIFIED: revisión del source.

6. **Diagnóstico UX de botones inesperados** — FOUND: una pulsación diferente se ignoraba funcionalmente. FIXED: muestra `Esperado`, `Detectado` y `Accion`; registra `ACCIDENTAL` y continúa sin marcar FAIL. VERIFIED: revisión del source.

7. **Diagonales del right stick no eran pruebas individuales** — FOUND: se tomaba una muestra agregada. FIXED: UP_RIGHT, UP_LEFT, DOWN_RIGHT y DOWN_LEFT se validan individualmente con abs(X/Y) >= threshold y signos correctos; se guardan X/Y. VERIFIED: revisión del source y compilación.

8. **Rango de sticks descartado / confundido con centro** — FOUND: LEFT_FULL_CIRCLE se descartaba y el JSON reutilizaba centro. FIXED: se conservan `left_center_stats`, `left_range_stats`, `right_center_stats`, `right_range_stats` y `right_diagonal_stats`. VERIFIED: claves JSON generadas por source.

9. **Drift no independiente** — FOUND: sólo se representaba el drift izquierdo. FIXED: se calcula y serializa `drift.left` y `drift.right` a partir de centros independientes. VERIFIED: revisión del source.

10. **Selección silenciosa del primer controller** — FOUND: se elegía el primer SDL GameController. FIXED: se enumeran todos; con varios se solicita un índice al usuario. VERIFIED: smoke enumeró el control detectado y no abrió silenciosamente una segunda opción.

11. **Estados físicos hardcodeados** — FOUND: resultados JSON fijos. FIXED: entradas, sticks, diagonales, rumble y confirmaciones usan PASS, FAIL, UNVERIFIED o UNSUPPORTED según ejecución. VERIFIED: revisión del source.

12. **BAT sin boot trace** — FOUND: no registraba diagnóstico previo al EXE. FIXED: se añadió `reports/TASK03_7/launcher_boot.log` con timestamp, SCRIPT_DIR, EXE, existencia e inicio; salida visible y flujo síncrono se conservaron. VERIFIED: el log contiene las cinco líneas requeridas.

13. **Suposición de SDL2.dll faltante** — FOUND: no existe evidencia para afirmar DLL faltante. FIXED: no se añadió esa afirmación ni se copiaron DLL; se mantuvo el enlace estático SDL existente. VERIFIED: compilación y arranque SDL exitosos.

14. **Rebuild** — FOUND: el ejecutable previo no correspondía a esta corrección. FIXED: `ControllerValidation.exe` recompilado desde el source corregido con MSVC x64 y SDL2 estático existente. VERIFIED: compilación terminó sin errores; timestamp actualizado.

15. **Smoke test sin simular A** — FOUND: requería comprobar la secuencia inicial. FIXED: no se simularon entradas. VERIFIED: BAT mostró los encabezados, `Buscando controles...`, `Control detectado: Xbox One Controller` y quedó esperando en `Presiona A para comenzar la prueba`; el proceso fue detenido manualmente después de comprobar esa frontera.

16. **Informe de auditoría** — FOUND: faltaba el entregable 3.7B solicitado. FIXED: este archivo fue creado en `reports/TASK03_7/TASK03_7B/EXTERNAL_AUDIT/FIX.md`. VERIFIED: archivo presente.

17. **Límites de alcance** — FOUND: riesgo de extender el cambio a otras áreas. FIXED: sólo se modificaron el source del tester, su EXE recompilado, el BAT y este informe. VERIFIED: no se inició Task 04 ni se tocó Shipwright source, runtime, live project o saves.

## Estado de cierre

La herramienta queda compilada y lista para prueba del usuario. La validación física permanece pendiente y no se certifica aquí.

STATUS:
READY_FOR_USER_TEST
