# Device glyph architecture

## Contrato semántico

La aplicación debe hablar en acciones N64 (`A`, `B`, `C_UP`, `C_DOWN`, `C_LEFT`, `C_RIGHT`, `L`, `R`, `Z`, `START`, `STICK`, `DPAD`), no en nombres Xbox. El contrato de Couch Edition es: Left Stick→Control Stick, A→A, B→B, LT→Z, LB→L, RB→R, Menu/Start→Start y Right Stick por dirección→C correspondiente. Cámara libre está fuera de alcance.

## Evidencia de input disponible

`Controller` expone botones, Left/Right Stick y todos los mappings. `ControllerButtonMapping` expone tipo de mapping, bitmask, nombre físico y nombre de dispositivo. `ControllerAxisDirectionMapping` expone dirección y valor normalizado. Ship incluye `SDLAxisDirectionToButtonMapping`, por lo que un axis direction puede representar un prompt tipo botón; esto no equivale todavía a una política de glyph para trigger.

`ConnectedPhysicalDeviceManager` expone dispositivos SDL conectados y `SohInputEditorWindow.cpp` permite agregar/editar mappings y activar/desactivar dispositivos por puerto. Esto sustenta hot rebinding y múltiples controllers, sujeto a resolver por puerto.

## Descriptor recomendado

`SemanticAction`, `InputSourceKind` (`BUTTON`, `AXIS_DIRECTION`, `TRIGGER`), `PhysicalDeviceFamily` (`N64_CLASSIC`, `XBOX_LAYOUT`, `GENERIC_GAMEPAD`, `KEYBOARD`, `MOUSE`), `port`, `mappingId`, `displayName`, `threshold` y `fallbackGlyph`.

El resolver debe priorizar el mapping activo del puerto, luego familia reconocida, y finalmente fallback N64. No debe basarse sólo en el nombre comercial del dispositivo: SDL puede entregar nombres variables y el mapping efectivo puede ser rebindeado.

## Asset strategy

Recomendación: atlas/registry lógico por familia, con assets originales N64 como fallback. Mantener aliases semánticos (`C_UP`) separados de assets físicos (`xbox_rs_up`). Evitar duplicar PNG por cada mensaje. La primera entrega puede usar assets raster existentes de SoH UI para validar layout, pero el renderer de mensajes necesita una representación compatible con `font->iconBuf` o una ruta equivalente documentada.

