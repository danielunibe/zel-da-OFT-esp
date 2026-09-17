# TASK 03.7C — SDL Input Reception Fix

Fecha: 2026-09-15

## Alcance

Se trabajó únicamente en `tools/hardware_validation` y `reports/TASK03_7`. No se modificaron Shipwright source, runtime, live project ni saves. Task 04 no fue iniciado. El BAT principal no fue reescrito.

## Cambios implementados

- `ControllerValidation.cpp` ahora habilita explícitamente `SDL_GameControllerEventState(SDL_ENABLE)` y `SDL_JoystickEventState(SDL_ENABLE)` y registra ambos resultados.
- La selección se filtra por `SDL_JoystickInstanceID`. Los eventos de otro dispositivo se registran como rechazados y no avanzan la prueba.
- La pantalla previa de A combina `SDL_CONTROLLERBUTTONDOWN` con `SDL_GameControllerGetButton`, usando edge detection para la transición 0 → 1.
- `waitButton()` combina evento y polling lógico; una pulsación accidental se muestra como diagnóstico y no como FAIL.
- `waitAxis()` combina `SDL_CONTROLLERAXISMOTION` y `SDL_GameControllerGetAxis`.
- Se registra polling raw mediante `SDL_JoystickGetButton` con el índice raw que cambie; no se modifican mappings.
- Se registran eventos de controller, joystick, ejes, conexión y desconexión en `SDL_EVENT_TRACE.log` con timestamp, tipo, `which`, botón/eje, valor y aceptación por instance ID.
- Se registran versión SDL compilada/runtime, mapping, nombre, GUID, path, vendor/product embebidos en GUID, instance ID y conteos en los reportes solicitados.
- Se creó `SDLInputDiagnostic.exe` y `RUN_INPUT_DIAGNOSTIC.bat` para observar botones, ejes y eventos sin una secuencia guiada.

## Evidencia de compilación

- `ControllerValidation.exe`: recompilado correctamente con MSVC x64 y SDL estático existente.
- `SDLInputDiagnostic.exe`: compilado correctamente con MSVC x64 y SDL estático existente.
- SDL compilado: `2.32.10`.
- SDL runtime: `2.32.10`.

## Evidencia de smoke sin entrada física

El diagnóstico independiente abrió correctamente:

- Control: `Xbox Series X Controller`.
- Instance ID: `0`.
- Valores iniciales observados: botones `0`, ejes `0`.
- El loop de polling permaneció activo y refrescando continuamente.

El tester principal abrió correctamente el mismo mando y mostró el diagnóstico vivo antes del gate de A. No se simuló ni se envió ninguna pulsación física; por tanto no se afirma todavía que A haya sido recibido.

## Estado de la evidencia física

`SDL_EVENT_TRACE.log` fue creado y permanece sin eventos porque durante el smoke no se presionaron botones ni se movieron sticks. Esto es esperado y no clasifica aún `SDL_EVENT_DELIVERY_ISSUE`, `SDL_GAMECONTROLLER_MAPPING_ISSUE` ni ausencia total de input.

La clasificación queda pendiente de la prueba de Daniel:

- si cambia `SDL_GameControllerGetButton` pero no llega evento: `SDL_EVENT_DELIVERY_ISSUE`;
- si cambia raw pero no GameController: `SDL_GAMECONTROLLER_MAPPING_ISSUE`;
- si ninguna capa cambia: continuar con la investigación de instancia/intercepción usando la evidencia registrada.

## Resultados detectados en smoke

```text
GAMECONTROLLER_EVENTS_ENABLED=1
JOYSTICK_EVENTS_ENABLED=1
CONTROLLER_INSTANCE_ID=0
CONNECTION=UNKNOWN
```

`UNKNOWN` se conserva porque la ruta SDL observada no permite afirmar USB o Bluetooth de forma fiable.

STATUS:
READY_FOR_PHYSICAL_INPUT_DIAGNOSTIC
