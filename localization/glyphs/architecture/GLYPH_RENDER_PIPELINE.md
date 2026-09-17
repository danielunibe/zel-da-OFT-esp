# Glyph render pipeline

## Flujo actual observado

1. El texto se decodifica en `soh/src/code/z_message_PAL.c`.
2. Los controles de mensaje se manejan en el switch de `Message_DrawTextChar`.
3. Los finales, flecha de elección y triángulo se cargan mediante `Font_LoadMessageBoxIcon`.
4. `soh/src/code/z_kanfont.c` copia desde `msgStaticTbl` al `font->iconBuf`; es una tabla estática de recursos, no una consulta al dispositivo actual.
5. El icono se dibuja con `gSPTextureRectangle`; la geometría depende de `R_TEXTBOX_ICON_*` y de offsets por idioma.

## Rutas separadas

- Mensajes: pipeline N64/alfabeto + icon buffer + Fast3D. Es la ruta prioritaria para un token dinámico.
- HUD: ruta de interfaz del juego y texturas de acciones; debe preservarse la legibilidad de C, R, L y Z.
- Ocarina: lógica y colores específicos dentro de `z_message_PAL.c`; no es un simple alias de un glyph de texto.
- SoH UI: `soh/soh/Enhancements/controls/InputViewer.cpp` carga texturas `textures/buttons/*.png` a través del ResourceManager y las pinta con ImGui. Es un sistema de iconos existente, pero separado del renderer de mensajes.

## Pipeline propuesto

`acción semántica N64 -> mappings activos del puerto -> descriptor físico (botón/axis/dirección/trigger) -> familia de dispositivo -> asset glyph -> renderer consumidor -> fallback N64`.

El resolver debe devolver un descriptor estable, no una textura directamente. Así el mismo descriptor puede servir al mensaje, HUD, Ocarina y SoH UI, con escalas y paletas específicas por consumidor.

## Decisiones de compatibilidad

- El byte/control code vanilla debe seguir funcionando sin resolver dinámico.
- El glyph dinámico debe ser opcional y fallar a la tabla clásica.
- No se debe inferir `C-Up` de “right stick” sin conservar dirección explícita.
- LT requiere distinguir trigger/axis de botón digital y declarar una política de umbral.

