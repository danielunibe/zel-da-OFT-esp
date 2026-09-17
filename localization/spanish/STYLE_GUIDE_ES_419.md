# Style Guide — Spanish LATAM (es-419) Localization

## Project
- **Target**: Ship of Harkinian 9.1.1 — Ocarina of Time (Couch Edition)
- **Locale**: es-419 (Español Latinoamericano Neutral)
- **Scope**: Written text only (no voice localization)

---

## 1. Tone & Register

### Voice
- **Informal (tú)** for all player-facing text
- **Neutral LATAM** — no country-specific slang (no "vos", "che", "wey", "cacho", "po")
- Friendly, approachable tone consistent with the game's whimsical fantasy world

### Examples
| Context | English | Correct (es-419) | Incorrect |
|---------|---------|-------------------|-----------|
| NPC greeting | "Hey! Listen!" | "¡Oye! ¡Escucha!" | "¡Che! ¡Escuchá!" (voseo) |
| Hint | "You should go north" | "Deberías ir al norte" | "Deberías ir pa' l norte" (slang) |
| System | "Press Start to begin" | "Presiona Iniciar para comenzar" | "Presione Iniciar para comenzar" (usted) |

### When to use USTED
- Never — all text uses tú form
- Exception: formal in-game documents (king's decrees, ancient texts) may use usted for flavor

---

## 2. Grammar Rules

### Capitalization
- **Titles/Proper Nouns**: Capitalize as in English — "Templo del Bosque", "Espada Maestra"
- **Menu Items**: Capitalize first letter only — "Guardar partida", "Opciones del juego"
- **In-Game Speech**: Follow natural sentence case — "No puedo abrir esa puerta"
- **Button Prompts**: "Presiona A" (not "Presiona a")

### Punctuation
- **¡** and **¿** REQUIRED for all exclamations and questions
- "¡Hola!" (not "Hola!")
- "¿Qué haces?" (not "Que haces?")
- **Ellipsis**: "..." (three dots, no spaces) — "No sé..."
- **Em dash**: Use " — " for interruptions — "Pero yo —"

### Accents (Tildes)
Mandatory on all Spanish words:
- á, é, í, ó, ú — "también", "aquí", "menú", "café"
- ñ — "año", "pañuelo", "señor"
- ü — "pingüino", "bilingüe" (only in güe/güi contexts)

### Gender & Number Agreement
- "El jugador" / "La jugadora" — use masculine as default for mixed groups
- "Los enemigos" — masculine plural as default
- Item names: match grammatical gender — "la espada", "el escudo", "la poción"

---

## 3. Terminology Decisions

### Character Names
- **Link**: Remains "Link" (never translated)
- **Zelda**: Remains "Zelda"
- **Ganondorf/Ganon**: Remains unchanged
- **Saria**: Remains "Saria"
- **Navi**: Remains "Navi"
- **All other character names**: Remain unchanged

### Location Names
- **Keep original fantasy names** in their established form
- **Translate descriptors**: "Forest" → "Bosque", "Temple" → "Templo", "Mountain" → "Monte"
- Examples:
  - "Kokiri Forest" → "Bosque Kokiri"
  - "Death Mountain" → "Monte Muerte"
  - "Forest Temple" → "Templo del Bosque"
  - "Water Temple" → "Templo del Agua"
  - "Gerudo Valley" → "Valle Gerudo"

### Item Names
- Translate common nouns, keep proper names
- "Kokiri Sword" → "Espada Kokiri"
- "Master Sword" → "Espada Maestra"
- "Deku Shield" → "Escudo Deku"
- "Heart Container" → "Contenedor de Corazón"
- "Piece of Heart" → "Pieza de Corazón"
- "Rupee" → "Rupee" (currency name stays)
- "Magic Meter" → "Medidor de Magia"

### Enemy Names
- **Bosses**: Keep English names — "Gohma", "King Dodongo", "Volvagia"
- **Regular enemies**: Translate where natural — "Skulltula" stays, "Stalfos" stays
- **Descriptive names**: Translate — "Dead Hand" → "Mano Muerta"

### Dungeon Names
- Translate fully — "Templo del Bosque", "Templo del Fuego", "Templo del Agua"
- "Inside Ganon's Castle" → "Interior del Castillo de Ganon"

---

## 4. Control Codes & Special Markers

### DO NOT TRANSLATE
These must remain exactly as-is in translated text:
- `&` — line break
- `^` — page break
- `%w`, `%r`, `%g`, `%b`, `%c`, `%p`, `%y`, `%B` — color codes
- `##` — color placeholder
- `[[variable]]` — dynamic variable (e.g., `[[gsCount]]`, `[[rupee]]`)
- `$icon` — altar icon codes
- `\x01`–`\x1F` — native OoT control bytes
- Button glyphs: bytes 0x9F–0xAB

### Variable Handling
- Variables like `[[rupee]]`, `[[heartPieceCount]]` must remain in translated text
- Adjust surrounding grammar to accommodate variable position
- Example: English "You got [[rupee]] rupees!" → Spanish "¡Obtuviste [[rupee]] rupias!"

---

## 5. Text Length Constraints

### Message Box Limits
- Standard box: ~192 pixels wide (≈25–30 characters depending on font)
- Page limit: 4 pages per message (controlled by `^` breaks)
- Some messages are space-constrained — prioritize clarity over literalness

### Overflow Strategy
- If translated text exceeds box width:
  1. Simplify phrasing
  2. Use shorter synonyms
  3. Split across more pages
  4. NEVER truncate mid-word

### Approximate Character Limits
| Message Type | Max Characters | Max Pages |
|-------------|---------------|-----------|
| NPC dialogue | ~400 chars | 4 pages |
| Item description | ~200 chars | 2 pages |
| System message | ~150 chars | 2 pages |
| Choice text | ~80 chars | 1 page |
| Hint text | ~300 chars | 4 pages |

---

## 6. Common LATAM Translations

| English | es-419 | Notes |
|---------|--------|-------|
| "Press Start" | "Presiona Iniciar" | |
| "Game Over" | "Fin del juego" | or "Juego terminado" |
| "Save" | "Guardar" | |
| "Load" | "Cargar" | |
| "Continue" | "Continuar" | |
| "Options" | "Opciones" | |
| "Yes" | "Sí" | |
| "No" | "No" | |
| "OK" | "Aceptar" | |
| "Cancel" | "Cancelar" | |
| "Back" | "Volver" | |
| "Quit" | "Salir" | |
| "Pause" | "Pausa" | |
| "Resume" | "Reanudar" | |
| "Attack" | "Atacar" | |
| "Defend" | "Defender" | |
| "Run" | "Correr" | |
| "Open" | "Abrir" | |
| "Close" | "Cerrar" | |
| "Take" | "Tomar" | |
| "Drop" | "Soltar" | |
| "Use" | "Usar" | |
| "Equip" | "Equipar" | |
| "Check" | "Revisar" | |
| "Talk" | "Hablar" | |
| "Read" | "Leer" | |
| "Play" | "Tocar" | (instrument) |
| "Place" | "Colocar" | |
| "Grab" | "Agarrar" | |
| "Throw" | "Lanzar" | |
| "Pull" | "Tirar" | |
| "Push" | "Empujar" | |
| "Climb" | "Escalar" | |
| "Swim" | "Nadar" | |
| "Dive" | "Bucear" | |
| "Wait" | "Esperar" | |
| "Return" | "Regresar" | |
| "Enter" | "Entrar" | |
| "Exit" | "Salir" | |
| "Follow me" | "Sígueme" | |
| "Come on" | "Vamos" | |
| "Thank you" | "Gracias" | |
| "You're welcome" | "De nada" | |
| "I'm sorry" | "Lo siento" | |
| "Excuse me" | "Disculpa" | |
| "Help!" | "¡Ayuda!" | |
| "Danger!" | "¡Peligro!" | |
| "Warning" | "Advertencia" | |
| "Notice" | "Aviso" | |
| "Hint" | "Pista" | |
| "Secret" | "Secreto" | |
| "Mystery" | "Misterio" | |
| "Legendary" | "Legendario" | |
| "Ancient" | "Antiguo" | |
| "Sacred" | "Sagrado" | |
| "Magic" | "Magia" | |
| "Power" | "Poder" | |
| "Courage" | "Valentía" | |
| "Wisdom" | "Sabiduría" | |
| "Power" | "Poder" | (Triforce) |
| "Courage" | "Valentía" | (Triforce) |
| "Wisdom" | "Sabiduría" | (Triforce) |
| "Forest" | "Bosque" | |
| "Fire" | "Fuego" | |
| "Water" | "Agua" | |
| "Spirit" | "Espíritu" | |
| "Shadow" | "Sombra" | |
| "Light" | "Luz" | |
| "Time" | "Tiempo" | |
| "Temple" | "Templo" | |
| "Castle" | "Castillo" | |
| "Village" | "Aldea" | |
| "Town" | "Pueblo" | |
| "Mountain" | "Monte" | |
| "Lake" | "Lago" | |
| "River" | "Río" | |
| "Valley" | "Valle" | |
| "Field" | "Campo" | |
| "Forest" | "Bosque" | |
| "Cave" | "Cueva" | |
| "Shrine" | "Santuario" | |
| "Graveyard" | "Cementerio" | |
| "Market" | "Mercado" | |
| "Ranch" | "Granja" | |
| "Hideout" | "Escondite" | |
| "Fortress" | "Fortaleza" | |
| "Tower" | "Torre" | |
| "Bridge" | "Puente" | |
| "Gate" | "Puerta" | |
| "Door" | "Puerta" | |
| "Chest" | "Cofre" | |
| "Crate" | "Caja" | |
| "Barrel" | "Barril" | |
| "Sign" | "Letrero" | |
| "Tree" | "Árbol" | |
| "Rock" | "Roca" | |
| "Stone" | "Piedra" | |
| "Crystal" | "Cristal" | |
| "Skull" | "Calavera" | |
| "Token" | "Token" | (collectible) |
| "Medallion" | "Medallón" | |
| "Relic" | "Reliquia" | |
| "Sword" | "Espada" | |
| "Shield" | "Escudo" | |
| "Bow" | "Arco" | |
| "Arrow" | "Flecha" | |
| "Bomb" | "Bomba" | |
| "Boomerang" | "Bumerán" | |
| "Hookshot" | "Anzuelo" | |
| "Ocarina" | "Ocarina" | |
| "Potion" | "Poción" | |
| "Tunic" | "Túnica" | |
| "Boots" | "Botas" | |
| "Gauntlets" | "Guantes" | |
| "Bracelet" | "Pulsera" | |
| "Ring" | "Anillo" | |
| "Amulet" | "Amuleto" | |
| "Key" | "Llave" | |
| "Map" | "Mapa" | |
| "Compass" | "Brújula" | |
| "Bottle" | "Botella" | |
| "Fairy" | "Hada" | |
| "Heart" | "Corazón" | |
| "Magic" | "Magia" | |
| "Strength" | "Fuerza" | |
| "Defense" | "Defensa" | |
| "Speed" | "Velocidad" | |
| "Range" | "Alcance" | |

---

## 7. Prohibited Terms

These translations are **NOT** acceptable:
- ❌ "Link" → never translate the name
- ❌ "Trifuerza" — use "Triforce"
- ❌ "Zeldar" — use "Zelda"
- ❌ "Hyruliano" — use "Hyliano" or "de Hyrule"
- ❌ "Deku" → never translate race names
- ❌ "Goron" → never translate race names
- ❌ "Zora" → never translate race names
- ❌ "Gerudo" → never translate race names
- ❌ "Kokiri" → never translate race names

---

## 8. QA Checklist

Before finalizing any translation:
- [ ] No control codes modified (%, &, ^, ##, [[var]])
- [ ] Button glyphs preserved (bytes 0x9F–0xAB)
- [ ] ¡ and ¿ present on all exclamations/questions
- [ ] Accents on all Spanish words (á, é, í, ó, ú, ñ)
- [ ] Gender/number agreement correct
- [ ] Character names unchanged
- [ ] Location names follow convention
- [ ] No country-specific slang
- [ ] Text fits within box width (estimate: ≤30 chars/line)
- [ ] Dynamic variables [[var]] preserved
- [ ] Text does not exceed 4 pages
