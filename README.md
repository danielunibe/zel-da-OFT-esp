# Zelda: Ocarina of Time (PC - Ship of Harkinian) — Couch Edition V03.1.1

**Ocarina Couch Edition V03.1.1** es la versión consolidada y probada en tiempo de ejecución de *Ship of Harkinian* (Ocarina of Time PC) para juego en sala / sofá ("Couch Edition"), integrando **localización completa en Español Latino (ES-419)**, **pipeline de color fílmico ACES en Direct3D 11**, **niebla atmosférica dinámica F3DEX2**, **inteligencia de materiales (1.407 reglas compiladas)**, **aislamiento total de la interfaz** y **estabilidad certificada**.

---

## 🌟 Arquitectura Visual y Perfiles en Tiempo de Ejecución

### 🎨 Perfil Visual Canónico (`gEnhancements.Graphics.VisualProfile`)
El motor cuenta con un interruptor canónico y unificado para el perfil visual:
- **Classic (0):** Renderizado bit-a-bit idéntico a Nintendo 64 / upstream Shipwright 9.1.1 puro (`tonemapPassCount = 0`).
- **Enhanced (1):** Renderizado con post-procesado fílmico ACES Fitted, niebla atmosférica linealizada y clasificación de materiales activa (`tonemapPassCount > 0`, `atmosphericPassCount > 0`, `materialDrawCallCount > 0`).

> **Nota de compatibilidad:** El motor incluye una migración automática desde la clave legada `gVisualEnhancements.MasterTonemapping` hacia la canónica `gEnhancements.Graphics.VisualProfile`.

### 🌫️ Niebla Atmosférica Dinámica F3DEX2
- Extracción directa en cada cuadro de los registros de niebla del RDP (`mRdp->fog_color`) y parámetros de profundidad RSP (`fog_mul`, `fog_offset`).
- Mezcla linealizada en espacio HDR antes del tonemapping ACES para evitar artefactos de banding.

### 🛡️ Aislamiento de Interfaz de Usuario
- Todos los elementos 2D (HUD, corazones, magia, rupias, botones y diálogos) se dibujan después del pase de postprocesado para garantizar máxima legibilidad y cero contaminación cromática.

### 🧱 Clasificación de Materiales e Inteligencia de Superficies
- **1.407 Reglas Piloto Compiladas (`MaterialRulesPilot.h`):** Búsqueda binaria indexada por hash de estado de tubería.
- **PBR-Lite:** Estado actual auditado: **`METADATA_ONLY`**. Las propiedades de rugosidad, metalicidad y emisividad están clasificadas en memoria en el `MaterialRegistry` para consumo futuro; no se declara sombreado PBR activo en pantalla en esta versión.

---

## 🌎 Localización Español Latino (ES-419)

- **2.116 Mensajes Traducidos & Verificados:** Diálogos, descripciones, objetos y cinemáticas en español latino.
- **Opcodes de Decisión Nativos:** Soporte para selección binaria nativa (`0x1B` TWO_CHOICE y `0x1C` THREE_CHOICE).
- **Tipografía Auténtica de Nintendo (11 Glifos I4):** Tildes, eñes y signos de interrogación/exclamación integrados con antialiasing nativo.
- **Alcance de la Traducción:**
  - Los textos y diálogos de la historia y del mundo dentro del juego están 100% en español latino.
  - La barra de menús de configuración de Ship of Harkinian (tecla F1 / ImGui) permanece en inglés por diseño upstream.
  - El mod `es.o2r` es retrocompatible con compilaciones estándar de Ship of Harkinian (proporcionando traducción de diálogos) y se integra nativamente en el runtime completo de Couch Edition V03.1.1.

---

## 🚀 Opciones de Uso e Instalación

### Opción A: Paquete Precompilado (`V03_1_1_RUNTIME`)
Si dispones del paquete precompilado de Couch Edition:
1. Copia tu archivo **`oot.o2r`** (generado con tu ROM legítima de N64) dentro de la carpeta del juego junto a `soh.exe`, `soh.o2r`, `es.o2r`, `shipofharkinian.json` y `gamecontrollerdb.txt`.
2. Ejecuta **`soh.exe`**.
3. Para alternar el perfil visual:
   - Presiona **F1** para abrir la barra de menús.
   - Dirígete a:
     ```
     Settings -> Graphics -> Visual Profile
     ```
   - Selecciona **Classic** o **Enhanced**.

### Opción B: Compilación desde Código Fuente Mediante Parches
Aplica los dos parches canónicos sobre los commits fijados:
1. **Shipwright** (`commit 4aaad850bd5540cd77c2d83f3ad348d3b38605b2`):
   ```bash
   git apply patches/shipwright_couch_edition_v03_1_1.patch
   ```
2. **libultraship** (`commit 17a0b7939bd05f5e617cef89457ca43774fc9a9f`):
   ```bash
   git apply patches/libultraship_couch_edition_v03_1_1.patch
   ```
3. Compilar usando Visual Studio 2022 con el entorno vcpkg fijado.

---

## 📦 Estructura del Repositorio

```
zel-da-OFT-esp/
├── es.o2r                                        # Archivo O2R de textos y glifos en español latino
├── data/
│   ├── spa_message_data_static                   # Tabla binaria canónica de mensajes
│   └── glyphs/                                   # Texturas binarias I4 de glifos
├── docs/
│   ├── RELEASE_NOTES_V03_1.md                    # Notas de lanzamiento
│   └── v03_1/                                    # Documentación técnica y auditorías
├── patches/
│   ├── shipwright_couch_edition_v03_1_1.patch    # Parche canónico V03.1.1 para Shipwright
│   ├── libultraship_couch_edition_v03_1_1.patch  # Parche canónico V03.1.1 para libultraship
│   └── legacy/                                   # Parches históricos de versiones previas
├── tools/
│   ├── test_v03_1_1_runtime.py                   # Suite de verificación runtime con diagnósticos
│   ├── export_v03_1_1_patches.py                 # Generador canónico de parches UTF-8 LF
│   └── package_v03_1_1.py                        # Empaquetador local del runtime
└── README.md
```

---

## 🧪 Validación en Tiempo de Ejecución (Evidencia Real)

Resultados certificados en ejecución real mediante `tools/test_v03_1_1_runtime.py`:

| Parámetro / Métrica | Modo Classic | Modo Enhanced | Estado |
|---|---|---|---|
| Perfil Visual Real | `0` | `1` | PASS |
| Tonemap Passes | `0` | `> 0` (301 medidos) | PASS |
| Atmospheric Passes | `0` | `> 0` (301 medidos) | PASS |
| Material Draw Calls | N/A | `> 0` (6.536 medidos) | PASS |
| Reglas de Materiales | 1.407 cargadas | 1.407 cargadas | PASS |
| Montaje de `es.o2r` | YES | YES | PASS |
| Carga de Tabla Español | YES | YES | PASS |
| Rendimiento Medido | NOT_MEASURED (en este entorno) | NOT_MEASURED (en este entorno) | INFORMATIVO |
| PBR-Lite | METADATA_ONLY | METADATA_ONLY | CERTIFICADO |

---

## 👤 Créditos

- **Proyecto Base:** [Ship of Harkinian / HarbourMasters](https://github.com/HarbourMasters/Shipwright)
- **Localización ES-419, Arquitectura Visual & Couch Edition:** Daniel Aguilar & Colaboradores
