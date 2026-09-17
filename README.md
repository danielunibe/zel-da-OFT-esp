# Zelda: Ocarina of Time (PC - Ship of Harkinian) — Localización Español Latino (ES-419)

Traducción completa de diálogos al **Español Latino (ES-419)** para **Ship of Harkinian (Ocarina of Time PC)**, con tipografía auténtica de Nintendo, corrección de kerning proporcional y estabilidad de memoria certificada.

---

## 🌟 Características

- **2.116 Mensajes Traducidos:** Cobertura de diálogos del juego base, misiones secundarias, pistas y descripciones de objetos.
- **Tipografía Auténtica de Nintendo (11 Glifos I4):** Letras con tildes (`á, é, í, ó, ú, Á, Í, Ó, Ú`), `ñ`, `Ñ` y signos de apertura (`¡`, `¿`) generados directamente desde la fuente original N64 de *Ocarina of Time*, respetando la paleta de antialiasing y línea base nativa.
- **Espaciado y Kerning Dinámico:** Ajuste en el motor del juego para eliminar espacios indeseados entre letras especiales.
- **Estabilidad Certificada (Hardening Fast3D / LibUltraShip):** Protección contra accesos a memoria no mapeada en comandos `G_SETTIMG` durante transiciones de escena y grottos (`En_Holl` / `Door_Ana`).

---

## 📦 Estructura del Repositorio

```
zel-da-OFT-esp/
├── es.o2r                   # Archivo listo para jugar en Ship of Harkinian
├── data/
│   ├── spa_message_data_static  # Tabla binaria de mensajes con colas de control NTSC
│   └── glyphs/                  # Texturas binarias I4 de 16x16 de los 11 caracteres especiales
├── patches/
│   ├── shipwright_es419.patch          # Parche para Ship of Harkinian (menús, idioma, anchos)
│   └── libultraship_hardening.patch    # Parche de seguridad para Fast3D / ResourceManager
├── tools/
│   ├── generate_authentic_spanish_glyphs.py  # Generador de glifos a partir de oot.o2r
│   └── rebuild_spanish_archive.py            # Generador del archivo es.o2r
└── README.md
```

---

## 🚀 Instalación Rápida

1. Descarga el archivo **`es.o2r`** de este repositorio o de la sección de Releases.
2. Cópialo directamente en la carpeta raíz de tu instalación de **Ship of Harkinian** (junto a `oot.o2r` y `soh.exe`) o dentro de la subcarpeta `mods/`.
3. Inicia el juego.
4. En el menú de configuración de Ship of Harkinian, ve a **Settings > Audio / General** (o `SohMenu`) y selecciona el idioma **Español**.
5. ¡Disfruta de Ocarina of Time en español latino!

---

## 🛠️ Compilación desde el Código Fuente

Si deseas compilar la integración completa desde el código de **Shipwright** (Ship of Harkinian):

1. Clona el repositorio oficial de Shipwright (v9.1.1):
   ```bash
   git clone --recursive https://github.com/HarbourMasters/Shipwright.git
   ```
2. Aplica los parches contenidos en la carpeta `patches/`:
   ```bash
   cd Shipwright
   git apply ../patches/shipwright_es419.patch
   cd libultraship
   git apply ../../patches/libultraship_hardening.patch
   ```
3. Compila el proyecto en Release mediante CMake y Visual Studio.
4. Genera el archivo `es.o2r` con la herramienta provista:
   ```bash
   python tools/generate_authentic_spanish_glyphs.py
   python tools/rebuild_spanish_archive.py
   ```

---

## 👤 Créditos

- **Proyecto Base:** [Ship of Harkinian / HarbourMasters](https://github.com/HarbourMasters/Shipwright)
- **Localización ES-419 & Integración:** Daniel Aguilar & Equipo Couch Edition
