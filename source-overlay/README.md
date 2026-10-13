# SOURCE-OVERLAY: SNAPSHOTS ESTABLE Y DE DESARROLLO

`shipwright/` y `libultraship/` en la raíz preservan el overlay del release estable V03.1.1. La base de desarrollo más reciente está separada en `development/shipwright/` y `development/libultraship/`; su estado y límites están documentados en `../docs/development/CURRENT_CANONICAL_SNAPSHOT.md`.

---

## Commits de Referencia Upstream Fijados
- **Shipwright:** `4aaad850bd5540cd77c2d83f3ad348d3b38605b2` (Versión 9.1.1 Bravo)
- **LibUltraShip:** `17a0b7939bd05f5e617cef89457ca43774fc9a9f`

---

## Modo de Uso

Para aplicar este overlay directamente sobre el árbol fuente de upstream clonado:

```powershell
pwsh -File apply_source_overlay.ps1
```

O si el código fuente upstream se encuentra en una ruta personalizada:

```powershell
pwsh -File apply_source_overlay.ps1 -TargetSourceDir "C:\Ruta\A\source\shipwright"
```

Para aplicar el snapshot de desarrollo actual (solo sobre un checkout limpio con las mismas revisiones base), usa desde la raíz del repositorio:

```powershell
pwsh -File tools/apply_development_overlay.ps1
```

---

## Estructura de Archivos Preservados

### 1. Shipwright (`source-overlay/shipwright/`)
- `CMakeLists.txt`: Configuración de build con directivas de localización y compilación.
- `soh/assets/custom/textures/buttons/LTBtn.i4.png`: Textura de botón LT para interfaz de mando.
- `soh/include/glyph_resolver.h`: Cabecera del resolutor de glifos y caracteres extendidos.
- `soh/include/z64.h`: Extensiones de estructuras del motor del juego.
- `soh/soh/Enhancements/FileSelectEnhancements.cpp`: Mejoras visuales en selector de archivos.
- `soh/soh/Enhancements/boss-rush/BossRush.cpp`: Adaptación de diálogos.
- `soh/soh/Enhancements/custom-message/CustomMessageManager.*`: Administrador de mensajes en español.
- `soh/soh/Enhancements/glyphs/GlyphResolver.cpp`: Lógica de resolución e inyección de glifos.
- `soh/soh/OTRGlobals.cpp`, `ResourceManagerHelpers.cpp`, `ShipUtils.cpp`: Enrutamiento y carga de recursos.
- `soh/soh/SohGui/SohMenu.h`, `SohMenuSettings.cpp`: Integración del menú de selección de idioma y perfiles.
- `soh/soh/z_message_OTR.cpp`: Enrutamiento OTR.
- `soh/src/code/z_kanfont.c`, `z_parameter.c`: Relleno y renderizado de fuentes.
- `soh/src/code/z_message_PAL.c`: **Hotfix crítico canónico** con `sSpaMessageEntryTablePtr` para enrutamiento exacto de español.
- `soh/src/overlays/...`: Overlays de interfaz de pausa, selector de nombres y guardado.

### 2. LibUltraShip (`source-overlay/libultraship/`)
- `include/fast/MaterialRegistry.h`: Registro de materiales y canalización PBR.
- `include/fast/MaterialRulesPilot.h`: Reglas compiladas de asignación de materiales.
- `include/fast/backends/gfx_direct3d_common.h`, `gfx_rendering_api.h`, `interpreter.h`: Interfaces gráficas extendidas.
- `src/fast/MaterialRegistry.cpp`: Implementación de captura y clasificación de llamadas de dibujado de materiales.
- `src/fast/backends/gfx_direct3d11.cpp`: Backend Direct3D 11 Enhanced con mapeo de tonos ACES y niebla volumétrica.
- `src/fast/interpreter.cpp`: Intérprete gráfico de Fast3D.
- `src/ship/Context.cpp`, `ResourceManager.cpp`: Carga de recursos y montaje de archivos `.o2r`.
