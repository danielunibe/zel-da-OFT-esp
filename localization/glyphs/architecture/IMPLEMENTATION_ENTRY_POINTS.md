# Implementation entry points

| Área | Archivo / símbolo | Estado G01 |
|---|---|---|
| Mensaje | `soh/src/code/z_message_PAL.c`, `Message_DrawTextChar` | Entrada central para un nuevo token; no tocar en G01 |
| Icono mensaje | `soh/src/code/z_kanfont.c`, `Font_LoadMessageBoxIcon` | Loader estático actual |
| Formato | `soh/include/message_data_fmt.h` | Punto para reservar/definir control code, sujeto a compatibilidad |
| HUD | `soh/src/code/z_hud.c` y rutas de interface | Auditar consumidores antes de sustituir iconos |
| Ocarina | `soh/src/code/z_message_PAL.c`, `Message_HandleOcarina` y estado de notas | Renderer/lógica especial; piloto separado |
| SoH UI | `soh/soh/Enhancements/controls/InputViewer.cpp` | Texturas por nombre y render ImGui |
| Rebinding UI | `soh/soh/Enhancements/controls/SohInputEditorWindow.cpp` | Query/edit de mappings y dispositivos |
| Controller | `libultraship/include/ship/controller/controldevice/controller/Controller.h` | Aggregator de botones/sticks |
| Button mappings | `.../ControllerButton.h`, `.../ControllerButtonMapping.h` | Query por id, tipo y device |
| Axis mappings | `.../ControllerStick.h`, `.../ControllerAxisDirectionMapping.h` | Query por dirección y valor |
| SDL | `.../mapping/sdl/` | Familias de mappings y axis-to-button |

Riesgo principal: no añadir una segunda fuente de verdad para bindings. El resolver debe leer las estructuras Ship existentes y ser consumido por cada renderer.

