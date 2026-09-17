# Zelda: Ocarina of Time (PC - Ship of Harkinian) — Localización Español Latino (ES-419)

Traducción completa de diálogos al **Español Latino (ES-419)** para **Ship of Harkinian (Ocarina of Time PC) — Couch Edition (Release Candidate 1)**, con tipografía auténtica de Nintendo, corrección de kerning proporcional, controles NTSC verificados y estabilidad de memoria certificada.

---

## 🌟 Características Principales (RC1)

- **2.116 Mensajes Traducidos & Verificados:** Diálogos del juego base, misiones secundarias, pistas y descripciones de objetos con correspondencia 100% fiel a los eventos y estructuras de control NTSC.
- **Tokens de Elección Nativos:** Erradicación total de placeholders literales, implementando los opcodes binarios nativos de OoT (`0x1B` TWO_CHOICE y `0x1C` THREE_CHOICE).
- **Tipografía Auténtica de Nintendo (11 Glifos I4):** Letras con tildes (`á, é, í, ó, ú, Á, Í, Ó, Ú`), `ñ`, `Ñ` y signos de apertura (`¡`, `¿`) extraídos y adaptados directamente desde la fuente original N64 de *Ocarina of Time*, respetando la paleta de antialiasing, dimensiones 16x16 I4 (128 bytes) y línea base nativa.
- **Espaciado y Anchos de Fuente Proporcionales:** Tabla de anchos (`sCharWidths`) ajustada en el motor para eliminar espacios indeseados entre caracteres latinos.
- **Hardening y Estabilidad Integral:**
  - **Fast3D Safe Lookup:** Verificación segura mediante iteradores (`std::unordered_map::find`) en texturas enmascaradas (`mMaskedTextures`), eliminando desreferencias de `end()`.
  - **G_SETTIMG / OtrSignatureCheck:** Barrera de protección de memoria con `VirtualQuery` y Structured Exception Handling (`__try ... __except`) para prevenir violaciones de acceso `0xC0000005`.
  - **Decodificador Seguro:** Control estricto de capacidad de buffers en `z_message_PAL.c` (`CAN_SRC_READ`, `CAN_DST_WRITE`) sin números mágicos fijos.
  - **Índices de Idioma Seguros:** Normalización `LANGUAGE_ARRAY_INDEX(lang)` para prevenir lecturas fuera de límites con `LANGUAGE_ESP = 4`.
  - **Fallback Seguro:** Si `es.o2r` no está presente, el juego arranca y opera en inglés sin crasheos.

---

## 📦 Estructura del Repositorio

```
zel-da-OFT-esp/
├── es.o2r                                  # Archivo de mod listo para jugar (Couch Edition RC1)
├── data/
│   ├── spa_message_data_static             # Tabla binaria canónica de mensajes (243.018 bytes)
│   └── glyphs/                             # Texturas binarias I4 16x16 de los 11 glifos auténticos
├── docs/
│   ├── RELEASE_NOTES_RC1.md                # Notas completas de la versión Release Candidate 1
│   └── RC1_MANIFEST.json                   # Manifiesto técnico con hashes SHA-256 e invariantes
├── patches/
│   ├── shipwright_es419.patch              # Parche completo para Ship of Harkinian (ES-419 + decodificador)
│   ├── libultraship_crash_guard.patch      # Parche quirúrgico de estabilidad (Fast3D + ResourceManager)
│   └── libultraship_couch_edition.patch    # Parche completo libultraship (Estabilidad + V02 Visual Foundation)
├── tools/
│   ├── generate_authentic_spanish_glyphs.py  # Extractor/generador de glifos a partir de oot.o2r
│   └── rebuild_spanish_archive.py            # Empaquetador reproducible de es.o2r
└── README.md
```

---

## 🚀 Instalación Rápida

1. Descarga el archivo **`es.o2r`** (SHA256: `d9037a8a4086d0073f90cc297fcbfe558a78ac3758f27b11a250537883fd990d`) de este repositorio o de la sección de Releases.
2. Cópialo directamente en la carpeta raíz de tu instalación de **Ship of Harkinian** (junto a `oot.o2r` y `soh.exe`) o dentro de la subcarpeta `mods/`.
3. Inicia el juego.
4. En el menú de configuración de Ship of Harkinian, ve a **Settings > Audio / General** (o `SohMenu`) y selecciona el idioma **Español**.
5. ¡Disfruta de Ocarina of Time en español latinoamericano con estabilidad certificada!

---

## 🛠️ Compilación desde el Código Fuente

Si deseas compilar la integración completa desde el código de **Shipwright** (Ship of Harkinian 9.1.1):

1. Clona el repositorio oficial de Shipwright:
   ```bash
   git clone --recursive https://github.com/HarbourMasters/Shipwright.git
   ```
2. Aplica los parches contenidos en la carpeta `patches/`:
   ```bash
   cd Shipwright
   git apply ../patches/shipwright_es419.patch
   cd libultraship
   git apply ../../patches/libultraship_crash_guard.patch
   ```
3. Compila el proyecto en Release mediante CMake y Visual Studio:
   ```bash
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```
4. Empaqueta el archivo `es.o2r` de forma reproducible:
   ```bash
   python tools/rebuild_spanish_archive.py
   ```

---

## 👤 Créditos

- **Proyecto Base:** [Ship of Harkinian / HarbourMasters](https://github.com/HarbourMasters/Shipwright)
- **Localización ES-419 & Arquitectura de Estabilidad:** Daniel Aguilar & Equipo Couch Edition
