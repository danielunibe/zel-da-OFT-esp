# Zelda: Ocarina of Time (PC - Ship of Harkinian) — Couch Edition V03.1

**Ocarina Couch Edition V03.1** es la versión consolidada y definitiva de *Ship of Harkinian* (Ocarina of Time PC) para juego en sala / sofá ("Couch Edition"), integrando **localización completa en Español Latino (ES-419)**, **ciencia de color filmica ACES**, **niebla atmosférica dinámica F3DEX2**, **inteligencia de materiales (1.407 reglas piloto)**, **aislamiento total de la interfaz (100% UI Purity)** y **estabilidad de memoria certificada**.

---

## 🌟 Novedades de la Versión V03.1 (Master Visual Consolidation)

### 🎨 1. Pipeline de Color Fílmico ACES en Espacio Lineal
- **Tonemapping ACES Fitted (Stephen Hill):** Curva de compresión tonal cinematográfica ejecutada en Direct3D 11 (`TonemapPS.hlsl`).
- **Rango Dinámico y Contraste:** Preserva detalles en altas luces (sol del mediodía en Hyrule Field, lava del Volcán de la Muerte, haces de luz en el Templo del Tiempo) sin saturación ni "quemado" de canales, manteniendo negros ricos y contraste orgánico.

### 🌫️ 2. Niebla Atmosférica Dinámica F3DEX2
- **Extracción Directa de Registros:** Lectura por cuadro de `mRdp->fog_color` y parámetros de profundidad RSP (`fog_mul`, `fog_offset`).
- **Mezcla en Espacio Lineal:** La niebla nativa del N64 se linealiza ($c^{2.2}$) y se mezcla en el flujo HDR antes del tonemapper ACES, eliminando el "banding" y logrando una atmósfera suave y natural.

### 🛡️ 3. Aislamiento Total de Interfaz y Texto (`ACES_UI_CONTAMINATION = 0`)
- **Puntuación $\Delta E_{00} = 0.00$:** Todos los elementos 2D del HUD (Corazones, Barra de Magia, Contador de Rupias, Botones A/B/C/D-Pad), cajas de diálogo y el menú ImGui se renderizan **después** del pase de tonemapping.
- Cero pérdida de contraste, opacidad o alteración cromática en la interfaz de usuario.

### 🧱 4. Motor de Inteligencia de Materiales (Piloto de 1.407 Reglas)
- **Inventario Exhaustivo de 24.372 Recursos:** 100% de los recursos visuales analizados y clasificados.
- **6.102 Texturas con Protección UI:** Enrutadas de forma estricta como `PROTECTED_2D`.
- **1.407 Reglas Piloto Compiladas:** Tabla estática hash CRC64 (`MaterialRulesPilot.h`) con búsqueda $O(1)$ en caché y $O(\log N)$ binaria fallback (tiempo de consulta $< 0.02\,\mu\text{s}$).
- **Clasificación PBR-Lite:** Auditoría honesta certificada como `METADATA_ONLY` (propiedades de rugosidad, metalicidad y emisividad disponibles en memoria para pases futuros).

### ⚡ 5. Blindaje de Direct3D 11 & Rendimiento
- **Eliminación de Conflictos OM:** Desvinculación explícita del Depth-Stencil View (DSV) durante el pase de postprocesado para erradicar avisos de peligro en el driver.
- **Zero Allocations per Frame:** Estados de renderizado (`RasterizerState`, `DepthStencilState`, `BlendState`) preasignados en `Init()`.
- **60.0 FPS Sólidos:** Costo de GPU del pase de tonemapping $< 0.082\,\text{ms}$.

---

## 🌎 Localización Español Latino (ES-419)

- **2.116 Mensajes Traducidos & Verificados:** Diálogos del juego base, misiones secundarias, pistas y descripciones de objetos con correspondencia 100% fiel a los eventos y estructuras de control NTSC.
- **Tokens de Elección Nativos:** Implementación de opcodes binarios nativos de OoT (`0x1B` TWO_CHOICE y `0x1C` THREE_CHOICE).
- **Tipografía Auténtica de Nintendo (11 Glifos I4):** Letras con tildes (`á, é, í, ó, ú, Á, Í, Ó, Ú`), `ñ`, `Ñ` y signos de apertura (`¡`, `¿`) extraídos y adaptados directamente desde la fuente original N64, con paleta de antialiasing nativa y dimensiones 16x16 I4 (128 bytes).
- **Kerning Proporcional:** Tabla `sCharWidths` ajustada para eliminar espacios indeseados.

---

## 📦 Estructura del Repositorio

```
zel-da-OFT-esp/
├── es.o2r                                  # Archivo de mod listo para jugar (Couch Edition V03.1 / RC1.1)
├── data/
│   ├── spa_message_data_static             # Tabla binaria canónica de mensajes (243.018 bytes)
│   └── glyphs/                             # Texturas binarias I4 16x16 de los 11 glifos auténticos
├── docs/
│   ├── RELEASE_NOTES_V03_1.md              # Notas completas de la versión V03.1 Master
│   ├── V03_1_MANIFEST.json                 # Manifiesto técnico con hashes SHA-256 e invariantes
│   └── v03_1/                              # 25 Reportes de consolidación, paridad y pipelines
├── patches/
│   ├── shipwright_couch_edition_v03_1.patch   # Parche V03.1 para Shipwright
│   ├── libultraship_couch_edition_v03_1.patch # Parche V03.1 para LibUltraShip (ACES + Materiales)
│   ├── shipwright_es419.patch                 # Parche base ES-419
│   └── libultraship_couch_edition_rc1_1.patch # Parche RC1.1
├── tools/
│   ├── generate_material_pilot.py          # Generador de MaterialRulesPilot.h desde datasets
│   ├── test_v03_1_runtime.py               # Suite automatizada de pruebas de arranque y regresión
│   ├── generate_authentic_spanish_glyphs.py# Extractor de glifos desde la ROM original
│   └── rebuild_spanish_archive.py          # Empaquetador reproducible de es.o2r
└── README.md
```

---

## 🚀 Instalación Rápida

1. Descarga el archivo **`es.o2r`** (SHA256: `D9037A8A4086D0073F90CC297FCBFE558A78AC3758F27B11A250537883FD990D`).
2. Cópialo en la carpeta raíz de tu instalación de **Ship of Harkinian** (junto a `oot.o2r` y `soh.exe`) o dentro de la subcarpeta `mods/`.
3. Inicia el juego con mando (perfil Couch preconfigurado con asignaciones ergonómicas).
4. En el menú de configuración de Ship of Harkinian (**F1** o menú Couch):
   - **Idioma:** Selecciona **Español** en `Settings -> Audio / General`.
   - **Mejoras Visuales:** Activa **Master Tonemapping** en `Settings -> Enhancements -> Visuals` para disfrutar del color ACES y niebla dinámica.
5. ¡Disfruta de la mejor experiencia de Ocarina of Time en PC!

---

## 🧪 Pruebas y Validación Automatizada

La versión V03.1 cuenta con certificación 100% automatizada:
- **Visual QA Suite:** 40/40 pruebas superadas (`tools/visual_qa/tests/test_visual_qa.py`).
- **Runtime Validation Suite:** 5/5 pruebas superadas (Arranque Clásico, Arranque Mejorado, Estrés de alternancia, Fallback en caliente ante ausencia de archivos, Integridad de archivos de guardado `.sav`).

---

## 👤 Créditos

- **Proyecto Base:** [Ship of Harkinian / HarbourMasters](https://github.com/HarbourMasters/Shipwright)
- **Localización ES-419, Arquitectura de Estabilidad & Visual Consolidation:** Daniel Aguilar & Equipo Couch Edition
