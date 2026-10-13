#include "UIWidgets.hpp"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <sstream>
#include <libultraship/libultraship.h>
#include <string>
#include <random>
#include <math.h>
#include <unordered_map>
#include <fstream>
#include <nlohmann/json.hpp>
#include <libultraship/libultra/types.h>
#include <spdlog/fmt/fmt.h>
#include "include/global.h"

namespace UIWidgets {

const char* Translate(const char* text) {
    // Spanish is intentionally limited to in-game message/narrative tables.
    // Keep the Ship of Harkinian application UI exactly as authored.
    return text;

#if 0
    if (text == nullptr || gSaveContext.language != LANGUAGE_ESP) {
        return text;
    }

    static const std::unordered_map<std::string, std::string> catalog = [] {
        std::unordered_map<std::string, std::string> result;
        const char* candidates[] = {
            "soh_ui_catalog_es_419.json",
            "localization/soh_ui_catalog_es_419.json",
            "assets/localization/soh_ui_catalog_es_419.json",
        };
        for (const char* path : candidates) {
            std::ifstream input(path);
            if (!input.is_open()) {
                continue;
            }
            try {
                const auto json = nlohmann::json::parse(input);
                for (const auto& [key, value] : json.items()) {
                    if (value.contains("english") && value.contains("spanish") && value["english"].is_string() &&
                        value["spanish"].is_string()) {
                        result.emplace(value["english"].get<std::string>(), value["spanish"].get<std::string>());
                    }
                }
            } catch (...) {
                result.clear();
            }
            if (!result.empty()) {
                break;
            }
        }
        // These resolution-editor strings are format-bearing literals and are
        // kept here until they can be added to the generated catalog pipeline.
        result.emplace("Pixel Count Presets", "Preajustes de cantidad de píxeles");
        result.emplace("Integer Scaling Settings", "Configuración de escalado entero");
        result.emplace("Additional Settings", "Configuración adicional");
        result.emplace("Click to reenable aspect correction.",
                       "Haz clic para volver a activar la corrección de aspecto.");
        result.emplace("Aspect ratio: %.2f:1", "Relación de aspecto: %.2f:1");
        result.emplace(ICON_FA_QUESTION_CIRCLE " \"Force aspect ratio\" required.",
                       ICON_FA_QUESTION_CIRCLE " Se requiere activar \"Forzar relación de aspecto\".");
        result.emplace(ICON_FA_QUESTION_CIRCLE " If the image is stretched and you don't know why, click this.",
                       ICON_FA_QUESTION_CIRCLE " Si la imagen aparece estirada y no sabes por qué, haz clic aquí.");
        result.emplace(ICON_FA_EXCLAMATION_TRIANGLE " Window exceeded.",
                       ICON_FA_EXCLAMATION_TRIANGLE " Se excedió el tamaño de la ventana.");
        result.emplace("Audio API", "API de audio");
        result.emplace("Audio API (Needs reload)", "API de audio (requiere reinicio)");
        result.emplace("Renderer API (Needs reload)", "API de renderizado (requiere reinicio)");
        result.emplace("Vertical Pixel Count", "Cantidad vertical de píxeles");
        result.emplace("Horiz. Pixel Count", "Cantidad horizontal de píxeles");
        result.emplace("Advanced Graphics Options", "Opciones gráficas avanzadas");
        result.emplace("Allow multi-windows", "Permitir varias ventanas");
        result.emplace("Anti-aliasing (MSAA)", "Antialiasing (MSAA)");
        result.emplace("Background Opacity", "Opacidad del fondo");
        result.emplace("Controller Bindings", "Asignación del mando");
        result.emplace("Configure Controller", "Configurar mando");
        result.emplace("Clear Devices", "Limpiar dispositivos");
        result.emplace("Current FPS", "FPS actuales");
        result.emplace("Cursor Always Visible", "Cursor siempre visible");
        result.emplace("Disable Idle Camera Re-Centering", "Desactivar recentrado de cámara inactiva");
        result.emplace("Disable Title Card", "Desactivar tarjeta de título");
        result.emplace("Draw Distance", "Distancia de dibujado");
        result.emplace("Always Show Dungeon Entrances", "Mostrar siempre las entradas de mazmorra");
        result.emplace("Animated Link in Pause Menu", "Link animado en el menú de pausa");
        result.emplace("Better Debug Warp Screen", "Mejor pantalla de teletransporte de depuración");
        result.emplace("Debug Mode", "Modo de depuración");
        result.emplace("Debug Save File Mode", "Modo de depuración de partidas");
        result.emplace("Boot To Debug Warp Screen", "Iniciar en la pantalla de teletransporte de depuración");
        result.emplace("Frame Advance", "Avance de fotogramas");
        result.emplace("Log Level", "Nivel de registro");
        result.emplace("Connect to Crowd Control", "Conectar a Crowd Control");
        result.emplace("Connecting...", "Conectando...");
        result.emplace("About Crowd Control", "Acerca de Crowd Control");
        result.emplace("Network", "Red");
        result.emplace("Sail", "Sail");
        result.emplace("Host & Port", "Servidor y puerto");
        result.emplace("Enable##Sail", "Activar##Sail");
        result.emplace("Disable##Sail", "Desactivar##Sail");
        result.emplace("Connected##Sail", "Conectado##Sail");
        result.emplace("Connecting...##Sail", "Conectando...##Sail");
        result.emplace("Copied to clipboard", "Copiado al portapapeles");
        result.emplace("Crowd Control", "Crowd Control");
        result.emplace("Enable##CrowdControl", "Activar##CrowdControl");
        result.emplace("Disable##CrowdControl", "Desactivar##CrowdControl");
        result.emplace("Connected", "Conectado");
        result.emplace("Connect to Crowd Control", "Conectar a Crowd Control");
        result.emplace("Enemy Name Tags", "Nombres sobre los enemigos");
        result.emplace("Spawned Enemies Ignored Ingame", "Ignorar enemigos creados durante la partida");
        result.emplace("Menu Settings", "Configuración del menú");
        result.emplace("Menu Theme", "Tema del menú");
        result.emplace("Menu Controller Navigation", "Navegación del menú con mando");
        result.emplace("Menu Background Opacity", "Opacidad del fondo del menú");
        result.emplace("General Settings", "Configuración general");
        result.emplace("Search In Sidebar", "Buscar en la barra lateral");
        result.emplace("Search Input Autofocus", "Enfoque automático del buscador");
        result.emplace("Open App Files Folder", "Abrir carpeta de archivos de la aplicación");
        result.emplace("Boot Sequence", "Secuencia de inicio");
        result.emplace("Languages", "Idiomas");
        result.emplace("Translate Title Screen", "Traducir pantalla de título");
        result.emplace("Language", "Idioma");
        result.emplace("Accessibility", "Accesibilidad");
        result.emplace("Text to Speech", "Texto a voz");
        result.emplace("EXPERIMENTAL", "EXPERIMENTAL");
        result.emplace("ImGui Menu Scaling", "Escalado del menú ImGui");
        result.emplace("Graphics Options", "Opciones gráficas");
        result.emplace("Toggle Fullscreen", "Alternar pantalla completa");
        result.emplace("Internal Resolution", "Resolución interna");
        result.emplace("Enable Vsync", "Activar VSync");
        result.emplace("Windowed Fullscreen", "Pantalla completa en ventana");
        result.emplace("Texture Filter (Needs reload)", "Filtro de texturas (requiere reinicio)");
        result.emplace("Visual Profile", "Perfil visual");
        result.emplace("Popout Bindings Window", "Abrir ventana de asignaciones");
        result.emplace("Toggle Input Viewer", "Alternar visor de entrada");
        result.emplace("Input Viewer Settings", "Configuración del visor de entrada");
        result.emplace("Popout Input Viewer Settings", "Abrir configuración del visor de entrada");
        result.emplace("Test Notification", "Probar notificación");
        result.emplace("Popout Menu", "Abrir menú en ventana");
        result.emplace("OoT Registry Editor", "Editor del registro de OoT");
        result.emplace("OoT Skulltula Debug", "Depuración de Skulltulas de OoT");
        result.emplace("Debug Warp Screen Translation", "Traducción de pantalla de teletransporte");
        result.emplace("Resource logging", "Registro de recursos");
        result.emplace("Advance 1", "Avanzar 1");
        result.emplace("Advance (Hold)", "Avanzar (mantener)");
        result.emplace("Popout Stats Window", "Abrir estadísticas en ventana");
        result.emplace("Popout Console", "Abrir consola en ventana");
        result.emplace("Popout Save Editor", "Abrir editor de partidas en ventana");
        result.emplace("Popout Hook Debugger", "Abrir depurador de hooks en ventana");
        result.emplace("Popout Collision Viewer", "Abrir visor de colisiones en ventana");
        result.emplace("Popout Actor Viewer", "Abrir visor de actores en ventana");
        result.emplace("Popout Display List Viewer", "Abrir visor de listas de dibujo en ventana");
        result.emplace("Popout Value Viewer", "Abrir visor de valores en ventana");
        result.emplace("Popout Message Viewer", "Abrir visor de mensajes en ventana");
        result.emplace("Popout Gfx Debugger", "Abrir depurador gráfico en ventana");
        result.emplace("Saving", "Guardado");
        result.emplace("Autosave", "Guardado automático");
        result.emplace("Remember Save Location", "Recordar ubicación de guardado");
        result.emplace("Containers Match Contents", "Los contenedores coinciden con su contenido");
        result.emplace("Chest Size & Texture Matches Contents", "Tamaño y textura del cofre según su contenido");
        result.emplace("Chests of Agony", "Cofres de agonía");
        result.emplace("Time of Day", "Hora del día");
        result.emplace("Nighttime GS Always Spawn", "Las Skulltulas doradas nocturnas siempre aparecen");
        result.emplace("Pull Grave During the Day", "Abrir la tumba durante el día");
        result.emplace("Dampe Appears All Night", "Dampé aparece toda la noche");
        result.emplace("Exit Market at Night", "Salir del mercado por la noche");
        result.emplace("Shops and Games Always Open", "Tiendas y juegos siempre abiertos");
        result.emplace("Pause Menu", "Menú de pausa");
        result.emplace("Allow the Cursor to be on Any Slot", "Permitir el cursor en cualquier casilla");
        result.emplace("Pause Warp", "Teletransporte desde pausa");
        result.emplace("Controls", "Controles");
        result.emplace("Answer Navi Prompt with L Button", "Responder a Navi con el botón L");
        result.emplace("Don't Require Input for Credits Sequence", "No requerir entrada durante los créditos");
        result.emplace("Item Count Messages", "Mensajes de cantidad de objetos");
        result.emplace("Gold Skulltula Tokens", "Fichas de Skulltula dorada");
        result.emplace("Pieces of Heart", "Fragmentos de corazón");
        result.emplace("Heart Containers", "Contenedores de corazón");
        result.emplace("Convenience", "Comodidad");
        result.emplace("Quit Fishing at Door", "Salir de pesca en la puerta");
        result.emplace("Instant Putaway", "Guardar al instante");
        result.emplace("Navi Timer Resets on Scene Change", "Reiniciar temporizador de Navi al cambiar de escena");
        result.emplace("Link's Cow in Both Time Periods", "La vaca de Link en ambos periodos");
        result.emplace("Skip Feeding Jabu-Jabu", "Omitir alimentar a Jabu-Jabu");
        result.emplace("Cutscenes", "Escenas cinemáticas");
        result.emplace("All##Skips", "Todas##Skips");
        result.emplace("None##Skips", "Ninguna##Skips");
        result.emplace("Skip Intro", "Omitir introducción");
        result.emplace("Skip Entrance Cutscenes", "Omitir escenas de entrada");
        result.emplace("Skip Story Cutscenes", "Omitir escenas de historia");
        result.emplace("Skip Song Cutscenes", "Omitir escenas de canciones");
        result.emplace("Skip Boss Introductions", "Omitir presentaciones de jefes");
        result.emplace("Quick Boss Deaths", "Muertes rápidas de jefes");
        result.emplace("Only change the size/texture of chests if you have the Stone of Agony.",
                       "Cambiar el tamaño o la textura de los cofres solo si tienes la Piedra de la Agonía.");
        result.emplace("Nighttime Skulltulas will spawn during both day and night.",
                       "Las Skulltulas nocturnas aparecerán tanto de día como de noche.");
        result.emplace("Allows graves to be pulled when child during the day.",
                       "Permite abrir tumbas durante el día cuando Link es niño.");
        result.emplace("Makes Dampe appear anytime during the night, not just his usual working hours.",
                       "Hace que Dampé aparezca durante toda la noche, no solo en su horario habitual.");
        result.emplace("Allows exiting Hyrule Castle Market Town to Hyrule Field at night by speaking to the guard next to the gate.",
                       "Permite salir del mercado del castillo hacia la llanura de Hyrule por la noche hablando con el guardia de la puerta.");
        result.emplace("Shops and Minigames are open both day and night. Requires a scene reload to take effect.",
                       "Las tiendas y minijuegos están abiertos de día y de noche. Requiere recargar la escena.");
        result.emplace("This is not compatible with the Locked Overworld Doors Randomizer option.",
                       "No es compatible con la opción de puertas del mundo bloqueadas del randomizador.");
        result.emplace("Allows the cursor on the pause menu to be over any slot. Sometimes required in Randomizer to select certain items.",
                       "Permite colocar el cursor del menú de pausa sobre cualquier casilla. A veces es necesario en el randomizador para seleccionar ciertos objetos.");
        result.emplace("Selection of warp song in pause menu initiates a warp. Disables song playback.",
                       "Seleccionar una canción de teletransporte en el menú de pausa inicia el teletransporte. Desactiva la reproducción de la canción.");
        result.emplace("Speak to Navi with L but enter First-Person Camera with C-Up.",
                       "Habla con Navi usando L, pero entra en primera persona con C-arriba.");
        result.emplace("Buffers your inputs to be executed a specified amount of frames later.",
                       "Almacena tus entradas para ejecutarlas después de un número determinado de fotogramas.");
        result.emplace("Changes the speed of sections of text that normally are paced slower than the text surrounding it.",
                       "Cambia la velocidad de las partes del texto que normalmente avanzan más lento que el texto cercano.");
        result.emplace("Speeds up lifting Silver Rocks and Obelisks.", "Acelera levantar rocas plateadas y obeliscos.");
        result.emplace("Speeds up ship in Shadow Temple.", "Acelera el barco del Templo de las Sombras.");
        result.emplace("Speeds up emptying animation when dumping out the contents of a bottle.",
                       "Acelera la animación de vaciado al tirar el contenido de una botella.");
        result.emplace("Don't increase crawl speed when exiting glitch-useful crawlspaces.Currently it is only the BOTW crawlspace to locked door",
                       "No aumentar la velocidad de gateo al salir de espacios útiles para glitches. Actualmente solo afecta al espacio de BOTW hacia la puerta cerrada.");
        result.emplace("Speeds up animation of the pause menu, similar to Majora's Mask",
                       "Acelera la animación del menú de pausa, como en Majora's Mask.");
        result.emplace("The crawlspace into Hyrule Castle goes straight to Zelda, skipping the guards.",
                       "El espacio de gateo hacia el castillo de Hyrule lleva directamente hasta Zelda y omite a los guardias.");
        result.emplace("Skip the tower escape sequence between Ganondorf and Ganon.",
                       "Omitir la secuencia de escape de la torre entre Ganondorf y Ganon.");
        result.emplace("Pierre appears when an Ocarina is pulled out. Requires learning the Scarecrow's Song first.",
                       "Pierre aparece al sacar la ocarina. Primero debes aprender la canción del espantapájaros.");
        result.emplace("Causes your Wallet to fill and empty faster when you gain or lose money.",
                       "Hace que la cartera se llene y vacíe más rápido al ganar o perder dinero.");
        result.emplace("Stops the game from freezing the player when picking up Gold Skulltula Tokens. Does not ",
                       "Evita que el juego congele al jugador al recoger fichas de Skulltula dorada. No ");
        result.emplace("Skip the \"Game Saved\" confirmation screen.",
                       "Omitir la pantalla de confirmación de \"Partida guardada\".");
        result.emplace("Allows you to have \"Link\" as a premade file name.",
                       "Permite usar \"Link\" como nombre de archivo predeterminado.");
        result.emplace("Renders a health bar for Enemies when Z-Targeted.",
                       "Muestra una barra de vida para los enemigos al fijarlos con Z.");
        result.emplace("Makes all equipment visible, regardless of age.",
                       "Hace visible todo el equipamiento, independientemente de la edad.");
        result.emplace("Renders Gauntlets when using the Bow and Hookshot like in OoT3D.",
                       "Muestra los guanteletes al usar el arco y el gancho, como en OoT3D.");
        result.emplace("Always shows dungeon entrance icons on the Minimap.",
                       "Muestra siempre los iconos de entradas de mazmorra en el minimapa.");
        result.emplace("Hides most of the UI when not needed.\nNOTE: Doesn't activate until scene transition.",
                       "Oculta la mayor parte de la interfaz cuando no es necesaria.\nNOTA: Se activa después de cambiar de escena.");
        result.emplace("Disables the Beating Animation of the Hearts on the HUD.",
                       "Desactiva la animación de latido de los corazones del HUD.");
        result.emplace("Changes the Rupee in the Wallet icon to match the wallet size you currently have.",
                       "Cambia la rupia del icono de la cartera para coincidir con el tamaño actual de la cartera.");
        result.emplace("Shows what items you have collected in the File Select screen, like in N64 Randomizer.",
                       "Muestra los objetos recogidos en la pantalla de selección de partida, como en el Randomizer de N64.");
        result.emplace("Ammo counts in the pause menu will work correctly regardless of the position of items in the Inventory.",
                       "Las cantidades de munición del menú de pausa funcionarán correctamente sin importar la posición de los objetos en el inventario.");
        result.emplace("Sets the aspect ratio to 4:3 and lowers resolution to 240p, the N64's native resolution.",
                       "Establece la relación de aspecto en 4:3 y baja la resolución a 240p, la resolución nativa de N64.");
        result.emplace("Remove the Darkness that appears when charging a Spin Attack.",
                       "Eliminar la oscuridad que aparece al cargar un ataque giratorio.");
        result.emplace("Increases the range in which Actors/Objects are drawn.",
                       "Aumenta el rango en el que se dibujan actores y objetos.");
        result.emplace("Adjusts the Horizontal Culling Plane to account for Widescreen Resolutions.",
                       "Ajusta el plano de poda horizontal para tener en cuenta las resoluciones panorámicas.");
        result.emplace("Requires Actor Draw Distance to be increased or Widscreen Actor Culling to be enabled.",
                       "Requiere aumentar la distancia de dibujado de actores o activar la poda panorámica.");
        result.emplace("Equip items and equipment on the D-pad. If used with \"D-pad on Pause Screen\", you must hold C-Up to equip instead of navigate.",
                       "Equipa objetos y equipamiento con la cruceta. Si usas \"Cruceta en pantalla de pausa\", mantén C-arriba para equipar en lugar de navegar.");
        result.emplace("Allows equipping the Tunics and Boots to C-Buttons/D-pad.",
                       "Permite equipar túnicas y botas en botones C o en la cruceta.");
        result.emplace("Adds a prompt to equip newly-obtained Swords, Shields, and Tunics.",
                       "Añade un aviso para equipar las espadas, escudos y túnicas recién obtenidos.");
        result.emplace("Prevent dropping inputs when playing the Ocarina too quickly.",
                       "Evita perder entradas al tocar la ocarina demasiado rápido.");
        result.emplace("Allows masks to be equipped normally from the pause menu as adult.",
                       "Permite equipar máscaras normalmente desde el menú de pausa como adulto.");
        result.emplace("Skip the part where the Ocarina Playback is called when you play a song.",
                       "Omitir la parte en la que se reproduce la ocarina al tocar una canción.");
        result.emplace("Turns Bunny Hood Invisible while still maintaining its effects.",
                       "Vuelve invisible la capucha de conejo, manteniendo sus efectos.");
        result.emplace("Removes the cap of 3 active explosives being deployed at once.",
                       "Elimina el límite de 3 explosivos activos al mismo tiempo.");
        result.emplace("Deku Nuts Explode Bombs", "Las nueces Deku hacen explotar bombas");
        result.emplace("Prevent Bombchus Forcing First-Person", "Evitar que los Bombchus fuercen la primera persona");
        result.emplace("Better Bombchu Shopping", "Mejoras al comprar Bombchus");
        result.emplace("Equip Multiple Arrows at Once", "Equipar varias flechas a la vez");
        result.emplace("Skip Magic Arrow Equip Animation", "Omitir animación de equipar flechas mágicas");
        result.emplace("Blue Fire Arrows", "Flechas de fuego azul");
        result.emplace("Sunlight Arrows", "Flechas de luz solar");
        result.emplace("Hookshot", "Gancho");
        result.emplace("Targetable Hookshot Reticle", "Retícula del gancho fijable");
        result.emplace("Bottles", "Botellas");
        result.emplace("Fix Deku Nut Upgrade", "Corregir mejora de nueces Deku");
        result.emplace("Bombchus Out of Bounds", "Bombchus fuera de límites");
        result.emplace("Enable Bombchu Drops", "Activar objetos Bombchu");
        result.emplace("Bombchu Bowling", "Bolera de Bombchus");
        result.emplace("Bomb Traps", "Trampas bomba");
        result.emplace("Fireproof Deku Shield", "Escudo Deku ignífugo");
        result.emplace("Hookshot Everything", "Usar el gancho con todo");
        result.emplace("Explosives", "Explosivos");
        result.emplace("Remove Explosive Limit", "Eliminar límite de explosivos");
        result.emplace("Static Explosion Radius", "Radio de explosión estático");
        result.emplace("Bow / Slingshot", "Arco / tirachinas");
        result.emplace("Bow as Child/Slingshot as Adult", "Arco como niño / tirachinas como adulto");
        result.emplace("Aiming Reticle for the Bow/Slingshot", "Retícula de apuntado para arco/tirachinas");
        result.emplace("Boomerang", "Bumerán");
        result.emplace("Instant Boomerang Recall", "Retorno instantáneo del bumerán");
        result.emplace("Aim Boomerang in First-Person Mode", "Apuntar el bumerán en primera persona");
        result.emplace("Aiming Reticle for Boomerang", "Retícula de apuntado para bumerán");
        result.emplace("Magic Spells", "Hechizos mágicos");
        result.emplace("Better Farore's Wind", "Mejoras del Viento de Farore");
        result.emplace("Faster Farore's Wind", "Viento de Farore más rápido");
        result.emplace("Rebottle Blue Fire", "Volver a embotellar fuego azul");
        result.emplace("Gameplay Fixes", "Correcciones de jugabilidad");
        result.emplace("Item-related Fixes", "Correcciones relacionadas con objetos");
        result.emplace("Camera Fixes", "Correcciones de cámara");
        result.emplace("Graphical Fixes", "Correcciones gráficas");
        result.emplace("Audio Fixes", "Correcciones de audio");
        result.emplace("Desync Fixes", "Correcciones de desincronización");
        result.emplace("Graphical Restorations", "Restauraciones gráficas");
        result.emplace("Glitch Restorations", "Restauraciones de glitches");
        result.emplace("Misc Restorations", "Restauraciones varias");
        result.emplace("Health", "Salud");
        result.emplace("Drops", "Objetos soltados");
        result.emplace("Miscellaneous", "Varios");
        result.emplace("Enemies", "Enemigos");
        result.emplace("No Heart Drops", "Sin corazones soltados");
        result.emplace("No Random Drops", "Sin objetos aleatorios soltados");
        result.emplace("Trees Drop Sticks", "Los árboles sueltan palos");
        result.emplace("Always Win Goron Pot", "Ganar siempre el juego de vasijas Goron");
        result.emplace("Always Win Dampe Digging Game", "Ganar siempre el juego de excavación de Dampé");
        result.emplace("Hyper Bosses", "Jefes potenciados");
        result.emplace("Hyper Enemies", "Enemigos potenciados");
        result.emplace("Enable Visual Guard Vision", "Activar visión visual de los guardias");
        result.emplace("Permanent Heart Loss", "Pérdida permanente de corazones");
        result.emplace("Fall Damage Multiplier", "Multiplicador de daño por caída");
        result.emplace("Void Damage Multiplier", "Multiplicador de daño del vacío");
        result.emplace("Bonk Damage Multiplier", "Multiplicador de daño por golpe");
        result.emplace("Spawn with Full Health", "Aparecer con salud completa");
        result.emplace("Switch Timer Multiplier", "Multiplicador del temporizador de interruptores");
        result.emplace("Dampe Drop Rate", "Probabilidad de objetos de Dampé");
        result.emplace("Delete File on Death", "Eliminar partida al morir");
        result.emplace("All Dogs are Richard", "Todos los perros son Richard");
        result.emplace("Cuccos Stay Put Multiplier: %dx", "Multiplicador de permanencia de cuccos: %dx");
        result.emplace("Cuccos Needed By Anju: %d", "Cuccos necesarios para Anju: %d");
        result.emplace("Leever Spawn Rate: %d seconds", "Frecuencia de aparición de Leevers: %d segundos");
        result.emplace("Quick Putaway", "Guardar rápidamente");
        result.emplace("Quick Bongo Kill", "Derrotar rápidamente a Bongo Bongo");
        result.emplace("Early Eyeball Frog", "Rana ocular temprana");
        result.emplace("Wide Door Ranges", "Rangos amplios de puertas");
        result.emplace("Grave Hole Jumps", "Saltos sobre agujeros de tumbas");
        result.emplace("Permanent Heart Loss", "Pérdida permanente de corazones");
        result.emplace("Master Volume: %d %%", "Volumen maestro: %d %%");
        result.emplace("Main Music Volume: %d %%", "Volumen de música principal: %d %%");
        result.emplace("Sub Music Volume: %d %%", "Volumen de música secundaria: %d %%");
        result.emplace("Fanfare Volume: %d %%", "Volumen de fanfarrias: %d %%");
        result.emplace("Sound Effects Volume: %d %%", "Volumen de efectos de sonido: %d %%");
        result.emplace("Graphics Options", "Opciones gráficas");
        result.emplace("Toggle Fullscreen", "Alternar pantalla completa");
        result.emplace("Internal Resolution", "Resolución interna");
        result.emplace("Match Refresh Rate", "Coincidir con la frecuencia de actualización");
        result.emplace("Enable Vsync", "Activar VSync");
        result.emplace("Windowed Fullscreen", "Pantalla completa en ventana");
        result.emplace("Position", "Posición");
        result.emplace("Duration (seconds):", "Duración (segundos):");
        result.emplace("Size:", "Tamaño:");
        result.emplace("Test Notification", "Probar notificación");
        result.emplace("Open App Files Folder", "Abrir carpeta de archivos de la aplicación");
        result.emplace("Boot Sequence", "Secuencia de inicio");
        result.emplace("Translate Title Screen", "Traducir pantalla de título");
        result.emplace("Text to Speech", "Texto a voz");
        result.emplace("Text", "Texto");
        result.emplace("Skip Bottle Pickup Messages", "Omitir mensajes al recoger botellas");
        result.emplace("Skip Consumable Item Pickup Messages", "Omitir mensajes al recoger consumibles");
        result.emplace("Skip Forced Dialog", "Omitir diálogos forzados");
        result.emplace("Skip Text", "Omitir texto");
        result.emplace("Text Speed: %dx", "Velocidad del texto: %dx");
        result.emplace("Slow Text Speed: %dx", "Velocidad lenta del texto: %dx");
        result.emplace("Animations", "Animaciones");
        result.emplace("Faster Heavy Block Lift", "Levantar bloques pesados más rápido");
        result.emplace("Faster Shadow Ship", "Barco sombra más rápido");
        result.emplace("Fast Chests", "Cofres rápidos");
        result.emplace("Skip Water Take Breath Animation", "Omitir animación de tomar aire bajo el agua");
        result.emplace("Empty Bottles Faster", "Vaciar botellas más rápido");
        result.emplace("Crawl Speed %dx", "Velocidad de gateo %dx");
        result.emplace("Faster Pause Menu", "Menú de pausa más rápido");
        result.emplace("Skip Child Stealth", "Omitir sigilo infantil");
        result.emplace("Skip Tower Escape", "Omitir escape de la torre");
        result.emplace("Skip Scarecrow's Song", "Omitir canción del espantapájaros");
        result.emplace("Faster Rupee Accumulator", "Acumulador de rupias más rápido");
        result.emplace("No Skulltula Freeze", "Sin congelación de Skulltulas");
        result.emplace("Skip Save Confirmation", "Omitir confirmación de guardado");
        result.emplace("Link as Default File Name", "Link como nombre de archivo predeterminado");
        result.emplace("Spawn Bean Skulltula Faster", "Hacer aparecer la Skulltula de judías más rápido");
        result.emplace("Mods", "Mods");
        result.emplace("Models & Textures", "Modelos y texturas");
        result.emplace("Disable LOD", "Desactivar LOD");
        result.emplace("Enemy Health Bars", "Barras de vida de enemigos");
        result.emplace("Enable 3D Dropped Items/Projectiles", "Activar objetos/proyectiles 3D soltados");
        result.emplace("Minimal UI", "Interfaz mínima");
        result.emplace("Disable HUD Heart Animations", "Desactivar animaciones de corazones del HUD");
        result.emplace("Dynamic Wallet Icon", "Icono de cartera dinámico");
        result.emplace("N64 Mode", "Modo N64");
        result.emplace("Equipment", "Equipamiento");
        result.emplace("Equip Items on Dpad", "Equipar objetos con la cruceta");
        result.emplace("Assignable Tunics and Boots", "Túnicas y botas asignables");
        result.emplace("Equipment Toggle", "Alternar equipamiento");
        result.emplace("Sword Toggle Options", "Opciones de alternancia de espada");
        result.emplace("Ask to Equip New Items", "Preguntar al equipar objetos nuevos");
        result.emplace("Ocarina", "Ocarina");
        result.emplace("Prevent Dropped Ocarina Inputs", "Evitar entradas de ocarina al soltarla");
        result.emplace("Fast Ocarina Playback", "Reproducción rápida de ocarina");
        result.emplace("Masks", "Máscaras");
        result.emplace("Masks Equippable as Adult", "Máscaras equipables como adulto");
        result.emplace("Persistent Masks", "Máscaras persistentes");
        result.emplace("Invisible Bunny Hood", "Capucha de conejo invisible");
        result.emplace("Mask Select in Inventory", "Seleccionar máscara en el inventario");
        result.emplace("UI", "Interfaz");
        result.emplace("Disable Hot/Underwater Warning Text", "Desactivar avisos de calor/agua");
        result.emplace("Remember Minimap State Between Areas", "Recordar estado del minimapa entre zonas");
        result.emplace("Visual Stone of Agony", "Piedra de la Agonía visual");
        result.emplace("Glitch Line-up Tick", "Marca de alineación para glitches");
        result.emplace("Disable Black Bar Letterboxes", "Desactivar barras negras cinematográficas");
        result.emplace("More Info in File Select", "Más información al seleccionar partida");
        result.emplace("Better Ammo Rendering in Pause Menu", "Mejor representación de munición en el menú de pausa");
        result.emplace("Enable Passage of Time on File Select", "Activar paso del tiempo al seleccionar partida");
        result.emplace("Misc.", "Varios");
        result.emplace("Remove Spin Attack Darkness", "Eliminar oscuridad del ataque giratorio");
        result.emplace("Draw Distance", "Distancia de dibujado");
        result.emplace("Increase Actor Draw Distance: %dx", "Aumentar distancia de dibujado de actores: %dx");
        result.emplace("Kokiri Draw Distance", "Distancia de dibujado de Kokiri");
        result.emplace("Widescreen Actor Culling", "Poda de actores en pantalla panorámica");
        result.emplace("Cull Glitch Useful Actors", "Podar actores útiles para glitches");
        result.emplace("Disable Bomb Billboarding", "Desactivar orientación 2D de bombas");
        result.emplace("Disable Grotto Fixed Rotation", "Desactivar rotación fija de grutas");
        result.emplace("Disable 2D Pre-Rendered Scenes", "Desactivar escenas 2D prerenderizadas");
        result.emplace("Ingame Text Spacing: %d", "Espaciado del texto en juego: %d");
        result.emplace("Show Age-Dependent Equipment", "Mostrar equipamiento dependiente de la edad");
        result.emplace("Scale Adult Equipment as Child", "Escalar equipamiento adulto como infantil");
        result.emplace("Show Gauntlets in First-Person", "Mostrar guanteletes en primera persona");
        result.emplace("Show Chains on Both Sides of Locked Doors", "Mostrar cadenas a ambos lados de puertas cerradas");
        result.emplace("Color Temple of Time's Medallions", "Colorear medallones del Templo del Tiempo");
        result.emplace("Disable Crit Wiggle", "Desactivar sacudida crítica");
        result.emplace("Better Owl", "Mejoras del búho");
        result.emplace("Faster Heavy Block Lift", "Levantar bloques pesados más rápido");
        result.emplace("Faster Pause Menu", "Menú de pausa más rápido");
        result.emplace("No Skulltula Freeze", "Sin congelación de Skulltulas");
        result.emplace("Biggoron Forge Time: %d days", "Tiempo de forja de Biggoron: %d días");
        result.emplace("Enemy Health Bars", "Barras de vida de enemigos");
        result.emplace("Disable HUD Heart Animations", "Desactivar animaciones de corazones del HUD");
        result.emplace("Always Show Dungeon Entrances", "Mostrar siempre las entradas de mazmorra");
        result.emplace("Better Ammo Rendering in Pause Menu", "Mejor representación de munición en el menú de pausa");
        result.emplace("Equip Items on Dpad", "Equipar objetos con la cruceta");
        result.emplace("Allow Strength Equipment to be Toggled", "Permitir alternar el equipamiento de fuerza");
        result.emplace("Prevent Dropped Ocarina Inputs", "Evitar entradas de ocarina al soltarla");
        result.emplace("Time Travel with Song of Time", "Viajar en el tiempo con la Canción del Tiempo");
        result.emplace("Bunny Hood Effect", "Efecto de la capucha de conejo");
        result.emplace("Mask Select in Inventory", "Seleccionar máscara en el inventario");
        result.emplace("Disable Title Card", "Desactivar tarjeta de título");
        result.emplace("Exclude Glitch-Aiding Cutscenes", "Excluir escenas que ayudan a glitches");
        result.emplace("Quick Boss Deaths", "Muertes rápidas de jefes");
        result.emplace("Randomizer Enhancements", "Mejoras del randomizador");
        result.emplace("Rando-Relevant Navi Hints", "Pistas de Navi relevantes para el randomizador");
        result.emplace("Random Rupee Names", "Nombres aleatorios para rupias");
        result.emplace("Use Custom Key Models", "Usar modelos de llave personalizados");
        result.emplace("Map & Compass Colors Match Dungeon", "Los colores del mapa y brújula coinciden con la mazmorra");
        result.emplace("Quest Item Fanfares", "Fanfarrias de objetos de misión");
        result.emplace("Mysterious Shuffled Items", "Objetos barajados misteriosos");
        result.emplace("Simpler Boss Soul Models", "Modelos simplificados de almas de jefes");
        result.emplace("Skip Get Item Animations", "Omitir animaciones de obtención de objetos");
        result.emplace("Item Scale: %.2f", "Escala de objetos: %.2f");
        result.emplace("Popout Randomizer Settings Window", "Abrir configuración del randomizador en ventana");
        result.emplace("Item Tracker", "Rastreador de objetos");
        result.emplace("Toggle Item Tracker", "Alternar rastreador de objetos");
        result.emplace("Item Tracker Settings", "Configuración del rastreador de objetos");
        result.emplace("Popout Item Tracker Settings", "Abrir configuración del rastreador de objetos");
        result.emplace("Entrance Tracker", "Rastreador de entradas");
        result.emplace("Toggle Entrance Tracker", "Alternar rastreador de entradas");
        result.emplace("Entrance Tracker Settings", "Configuración del rastreador de entradas");
        result.emplace("Popout Entrance Tracker Settings", "Abrir configuración del rastreador de entradas");
        result.emplace("Check Tracker", "Rastreador de comprobaciones");
        result.emplace("Toggle Check Tracker", "Alternar rastreador de comprobaciones");
        result.emplace("Check Tracker Settings", "Configuración del rastreador de comprobaciones");
        result.emplace("Popout Check Tracker Settings", "Abrir configuración del rastreador de comprobaciones");
        result.emplace("Customize Behavior", "Personalizar comportamiento");
        result.emplace("Shooting Gallery", "Galería de tiro");
        result.emplace("Customize Behavior##Shooting", "Personalizar comportamiento##Tiro");
        result.emplace("Instant Win", "Victoria instantánea");
        result.emplace("No Rupee Randomization", "Sin aleatorización de rupias");
        result.emplace("Child Starting Ammunition: %d seeds", "Munición inicial de niño: %d semillas");
        result.emplace("Adult Starting Ammunition: %d arrows", "Munición inicial de adulto: %d flechas");
        result.emplace("Customize Behavior##Bowling", "Personalizar comportamiento##Bolera");
        result.emplace("Remove Small Cucco", "Eliminar cucco pequeño");
        result.emplace("Remove Big Cucco", "Eliminar cucco grande");
        result.emplace("Bombchu Count: %d bombchus", "Cantidad de Bombchus: %d Bombchus");
        result.emplace("Frogs’ Ocarina Game", "Juego de la ocarina de las ranas");
        result.emplace("Customize Behavior##Frogs", "Personalizar comportamiento##Ranas");
        result.emplace("Instant Win##Frogs", "Victoria instantánea##Ranas");
        result.emplace("Unlimited Playback Time##Frogs", "Tiempo de reproducción ilimitado##Ranas");
        result.emplace("Modify Note Timer: %dx", "Modificar temporizador de notas: %dx");
        result.emplace("Lost Woods Ocarina Game", "Juego de la ocarina del Bosque Perdido");
        result.emplace("Customize Behavior##LostWoods", "Personalizar comportamiento##Bosque Perdido");
        result.emplace("Instant Win##LostWoods", "Victoria instantánea##Bosque Perdido");
        result.emplace("Note Play Speed: %dx", "Velocidad de reproducción de notas: %dx");
        result.emplace("Unlimited Playback Time##LostWoods", "Tiempo de reproducción ilimitado##Bosque Perdido");
        result.emplace("Number of Starting Notes: %d notes", "Cantidad de notas iniciales: %d notas");
        result.emplace("Round One Notes: %d notes", "Notas de la ronda uno: %d notas");
        result.emplace("Round Two Notes: %d notes", "Notas de la ronda dos: %d notas");
        result.emplace("Round Three Notes: %d notes", "Notas de la ronda tres: %d notas");
        result.emplace("Forest Temple", "Templo del Bosque");
        result.emplace("Solve Amy’s Puzzle", "Resolver el rompecabezas de Amy");
        result.emplace("Fishing", "Pesca");
        result.emplace("Customize Behavior##Fishing", "Personalizar comportamiento##Pesca");
        result.emplace("Instant Fishing", "Pesca instantánea");
        result.emplace("Guarantee Bite", "Garantizar picada");
        result.emplace("Fish never Escape", "Los peces nunca escapan");
        result.emplace("Loaches always Appear", "Las lochas siempre aparecen");
        result.emplace("Skip Keep Confirmation", "Omitir confirmación para conservar");
        result.emplace("All Fish are Hyrule Loaches", "Todos los peces son lochas de Hyrule");
        result.emplace("Timeless Equipment", "Equipo atemporal");
        result.emplace("Unrestricted Items", "Objetos sin restricciones");
        result.emplace("Super Tunic", "Súper túnica");
        result.emplace("Shield with Two-Handed Weapons", "Escudo con armas a dos manos");
        result.emplace("Deku Sticks:", "Palos Deku:");
        result.emplace("Bomb Timer Multiplier: %.2fx", "Multiplicador del temporizador de bombas: %.2fx");
        result.emplace("Hookshot Reach Multiplier: %.2fx", "Multiplicador del alcance del gancho: %.2fx");
        result.emplace("Misc", "Varios");
        result.emplace("No Clip", "Sin colisiones");
        result.emplace("Moon Jump on L", "Salto lunar con L");
        result.emplace("No ReDead/Gibdo Freeze", "Sin congelación de Redead/Gibdo");
        result.emplace("Keese/Guay don't Target You", "Los Keese/Guay no te atacan");
        result.emplace("Disable Haunted Wasteland Sandstorm", "Desactivar tormenta de arena del Desierto Encantado");
        result.emplace("Glitch Aids", "Ayudas para glitches");
        result.emplace("Easy Frame Advancing with Pause", "Avance fácil de fotogramas con pausa");
        result.emplace("Easy ISG", "ISG fácil");
        result.emplace("Easy QPA", "QPA fácil");
        result.emplace("Clear Cutscene Pointer", "Limpiar puntero de cinemática");
        result.emplace("Fish don't Despawn", "Los peces no desaparecen");
        result.emplace("Freeze Time", "Congelar el tiempo");
        result.emplace("Time Sync", "Sincronización temporal");
        result.emplace("Instant Age Change", "Cambio instantáneo de edad");
        result.emplace("Save States", "Estados de guardado");
        result.emplace("I promise I have read the warning", "Prometo haber leído la advertencia");
        result.emplace("I understand, enable save states", "Entiendo; activar estados de guardado");
        result.emplace("Beta Quest", "Beta Quest");
        result.emplace("Enable Beta Quest", "Activar Beta Quest");
        result.emplace("Beta Quest World: %d", "Mundo de Beta Quest: %d");
        result.emplace("Font Scale: %.2fx", "Escala de fuente: %.2fx");
        result.emplace("Hide Background", "Ocultar fondo");
        result.emplace("Additional Traps", "Trampas adicionales");
        result.emplace("Ammo", "Munición");
        result.emplace("Ammo Traps", "Trampas de munición");
        result.emplace("Block Pushing Speed +%d", "Velocidad al empujar bloques +%d");
        result.emplace("Burn Traps", "Trampas de quemadura");
        result.emplace("Death Traps", "Trampas mortales");
        result.emplace("Enemy List", "Lista de enemigos");
        result.emplace("Enemy Randomizer", "Aleatorizador de enemigos");
        result.emplace("Exclude Glitch-Aiding Crawlspaces", "Excluir pasadizos que ayudan a glitches");
        result.emplace("Fish while Hovering", "Pescar mientras levitas");
        result.emplace("Fix Anubis Fireballs", "Corregir bolas de fuego de Anubis");
        result.emplace("Fix Broken Giant's Knife Bug", "Corregir error del Cuchillo Gigante roto");
        result.emplace("Fix Bush Item Drops", "Corregir objetos soltados por arbustos");
        result.emplace("Fix Camera Drift", "Corregir deriva de cámara");
        result.emplace("Fix Camera Swing", "Corregir balanceo de cámara");
        result.emplace("Fix Credits Timing (PAL)", "Corregir sincronización de créditos (PAL)");
        result.emplace("Fix Dampé Going Backwards", "Corregir a Dampé caminando hacia atrás");
        result.emplace("Fix Darunia Dancing too Fast", "Corregir baile demasiado rápido de Darunia");
        result.emplace("Fix Dungeon Entrances", "Corregir entradas de mazmorras");
        result.emplace("Fix Enemies not Spawning Near Water", "Corregir enemigos que no aparecen cerca del agua");
        result.emplace("Fix Falling from Vine Edges", "Corregir caídas desde los bordes de enredaderas");
        result.emplace("Fix Gerudo Warrior's Clothing Colors", "Corregir colores de ropa de guerreros Gerudo");
        result.emplace("Fix Hand Holding Hammer", "Corregir mano que sostiene el martillo");
        result.emplace("Fix Hanging Ledge Swing Rate", "Corregir velocidad al balancearse de un saliente");
        result.emplace("Fix L&R Pause Menu", "Corregir menú de pausa con L y R");
        result.emplace("Fix L&Z Page Switch in Pause Menu", "Corregir cambio de página con L y Z en el menú de pausa");
        result.emplace("Fix Link's Eyes Open while Sleeping", "Corregir ojos abiertos de Link mientras duerme");
        result.emplace("Fix Megaton Hammer Crouch Stab", "Corregir estocada agachado con Martillo Megatón");
        result.emplace("Fix Missing Jingle after 5 Silver Rupees", "Corregir melodía faltante tras 5 rupias plateadas");
        result.emplace("Fix Navi Text HUD Position", "Corregir posición del texto de Navi en el HUD");
        result.emplace("Fix Out of Bounds Textures", "Corregir texturas fuera de límites");
        result.emplace("Fix Poacher's Saw Softlock", "Corregir bloqueo del aserradero del cazador furtivo");
        result.emplace("Fix Raised Floor Switches", "Corregir interruptores de suelo elevados");
        result.emplace("Fix the Gravedigging Tour Glitch", "Corregir glitch del recorrido de excavación");
        result.emplace("Fix Two-Handed Idle Animations", "Corregir animaciones inactivas a dos manos");
        result.emplace("Fix Vanishing Paths", "Corregir caminos que desaparecen");
        result.emplace("Fix Zora Hint Dialogue", "Corregir diálogo de pistas de Zora");
        result.emplace("Freeze Traps", "Trampas de congelación");
        result.emplace("Frogs' Ocarina Game", "Juego de la ocarina de las ranas");
        result.emplace("Hurt Container Mode", "Modo de recipientes dañinos");
        result.emplace("Infinite...", "Infinito...");
        result.emplace("King Zora Speed: %.2fx", "Velocidad del Rey Zora: %.2fx");
        result.emplace("Knockback Traps", "Trampas de retroceso");
        result.emplace("Magic", "Magia");
        result.emplace("Mirrored World", "Mundo reflejado");
        result.emplace("Money", "Dinero");
        result.emplace("N64 Weird Frames", "Fotogramas extraños de N64");
        result.emplace("Nayru's Love", "Amor de Nayru");
        result.emplace("Pause Buffer Input Window: %d frames", "Ventana de entrada almacenada durante pausa: %d fotogramas");
        result.emplace("Play Zelda's Lullaby to Open Sleeping Waterfall", "Tocar la Nana de Zelda para abrir la cascada dormida");
        result.emplace("Pulsate Boss Icon", "Hacer pulsar el icono del jefe");
        result.emplace("Randomized Enemy Sizes", "Tamaños de enemigos aleatorizados");
        result.emplace("Red Ganon Blood", "Sangre roja de Ganon");
        result.emplace("Remove Power Crouch Stab", "Eliminar estocada agachado potenciada");
        result.emplace("Restore Old Gold Skulltula Cutscene", "Restaurar cinemática antigua de Skulltula dorada");
        result.emplace("Rupee Dash Interval %d seconds", "Intervalo de carrera de rupias: %d segundos");
        result.emplace("Rupee Dash Mode", "Modo carrera de rupias");
        result.emplace("Scale Health with Size", "Escalar salud con el tamaño");
        result.emplace("Select all Enemies", "Seleccionar todos los enemigos");
        result.emplace("Shadow Tag Mode", "Modo etiqueta de sombras");
        result.emplace("Shock Traps", "Trampas eléctricas");
        result.emplace("Simulated Input Lag: %d frames", "Latencia de entrada simulada: %d fotogramas");
        result.emplace("Skip Misc Interactions", "Omitir interacciones varias");
        result.emplace("Skip One Point Cutscenes (Chests, Door Unlocks, etc.)", "Omitir cinemáticas breves (cofres, abrir puertas, etc.)");
        result.emplace("Skip Owl Interactions", "Omitir interacciones con búhos");
        result.emplace("Solve Amy's Puzzle", "Resolver el rompecabezas de Amy");
        result.emplace("Speed Traps", "Trampas de velocidad");
        result.emplace("Teleport Traps", "Trampas de teletransporte");
        result.emplace("Tier 1 Traps:", "Trampas de nivel 1:");
        result.emplace("Tier 2 Traps:", "Trampas de nivel 2:");
        result.emplace("Tier 3 Traps:", "Trampas de nivel 3:");
        result.emplace("Toggle Timers Window", "Alternar ventana de temporizadores");
        result.emplace("Trap Options", "Opciones de trampas");
        result.emplace("Vine/Ladder Climb Speed +%d", "Velocidad al subir enredaderas/escaleras +%d");
        result.emplace("Void Traps", "Trampas de vacío");
        result.emplace("Epona Boost", "Impulso de Epona");
        result.emplace("Include Held Inputs at the Start of Pause Buffer Input Window", "Incluir entradas mantenidas al inicio de la ventana de pausa almacenada");
        result.emplace("Items", "Objetos");
        result.emplace("Ivan the Fairy (Coop Mode)", "Iván el hada (modo cooperativo)");
        result.emplace("Popout Audio Editor Window", "Abrir ventana del editor de audio");
        result.emplace("Popout Cosmetics Editor Window", "Abrir ventana del editor de cosméticos");
        result.emplace("Popout Gameplay Stats Window", "Abrir ventana de estadísticas de juego");
        result.emplace("Popout Time Splits Window", "Abrir ventana de tiempos parciales");
        result.emplace("About", "Acerca de");
        result.emplace("Boot", "Inicio");
        result.emplace("Input Viewer", "Visor de entradas");
        result.emplace("Popout Mod Menu Window", "Abrir ventana del menú de mods");
        result.emplace("Popout Plandomizer Window", "Abrir ventana del plandomizador");
        result.emplace("Ship Of Harkinian", "Ship of Harkinian");
        result.emplace("Toggle Input Viewer", "Alternar visor de entradas");
        result.emplace("Input Viewer Settings", "Configuración del visor de entradas");
        result.emplace("Popout Input Viewer Settings", "Abrir configuración del visor de entradas");
        result.emplace("Advance 1 frame.", "Avanzar 1 fotograma.");
        result.emplace("Advance frames while the button is held.", "Avanzar fotogramas mientras se mantenga pulsado el botón.");
        result.emplace("All fish will be caught instantly.", "Todos los peces se capturarán al instante.");
        result.emplace("Blue Fire dropped from bottle can be bottled.", "El fuego azul soltado de una botella se puede volver a embotellar.");
        result.emplace("Changes the menu display from overlay to windowed.", "Cambia la presentación del menú de superposición a ventana.");
        result.emplace("Changes the scaling of the ImGui menu elements.", "Cambia la escala de los elementos del menú ImGui.");
        result.emplace("Displays a test notification.", "Muestra una notificación de prueba.");
        result.emplace("Enables text to speech for in game dialog", "Activa la lectura en voz alta para los diálogos del juego");
        result.emplace("Enables Windowed Fullscreen Mode.", "Activa el modo de pantalla completa en ventana.");
        result.emplace("Freezes the time of day.", "Congela la hora del día.");
        result.emplace("Holding down B skips text.", "Mantener pulsado B omite el texto.");
        result.emplace("Holding L makes you float into the air.", "Mantener pulsado L te hace flotar en el aire.");
        result.emplace("Interval between Rupee reduction in Rupee Dash Mode.", "Intervalo entre reducciones de rupias en el modo carrera de rupias.");
        result.emplace("Loaches will always appear in the fishing pond instead of every four visits.", "Las lochas siempre aparecerán en el estanque de pesca en lugar de cada cuatro visitas.");
        result.emplace("Makes every surface in the game climbable.", "Hace escalable cualquier superficie del juego.");
        result.emplace("Makes every surface in the game hookshotable.", "Permite usar el gancho en cualquier superficie del juego.");
        result.emplace("Makes every tunic have the effects of every other tunic.", "Hace que cada túnica tenga los efectos de todas las demás.");
        result.emplace("How large notifications are.", "Qué tamaño tienen las notificaciones.");
        result.emplace("How long notifications are displayed for.", "Cuánto tiempo se muestran las notificaciones.");
        result.emplace("How opaque the background of notifications is.", "Qué tan opaco es el fondo de las notificaciones.");
        result.emplace("Toggles the Item Tracker.", "Alterna el rastreador de objetos.");
        result.emplace("Toggles the Input Viewer.", "Alterna el visor de entradas.");
        result.emplace("Toggles Fullscreen On/Off.", "Activa o desactiva la pantalla completa.");
        result.emplace("Toggles the Check Tracker.", "Alterna el rastreador de comprobaciones.");
        result.emplace("Enables the registry editor.", "Activa el editor del registro.");
        result.emplace("Toggles the Entrance Tracker.", "Alterna el rastreador de entradas.");
        result.emplace("Skips the Frogs' Ocarina Game.", "Omite el juego de la ocarina de las ranas.");
        result.emplace("Enables additional Trap variants.", "Activa variantes adicionales de trampas.");
        result.emplace("Don't scale image to fill window.", "No escalar la imagen para llenar la ventana.");
        result.emplace("Enables the separate Stats Window.", "Activa la ventana independiente de estadísticas.");
        result.emplace("Sets the applied Texture Filtering.", "Establece el filtrado de texturas aplicado.");
        result.emplace("Enables the separate Console Window.", "Activa la ventana independiente de consola.");
        result.emplace("Skips the Shooting Gallery minigame.", "Omite el minijuego de la galería de tiro.");
        result.emplace("Modifies Damage taken after Bonking.", "Modifica el daño recibido al golpearse.");
        result.emplace("Enables the separate Mod Menu Window.", "Activa la ventana independiente del menú de mods.");
        result.emplace("Enables the separate Bindings Window.", "Activa la ventana independiente de asignaciones.");
        result.emplace("Changes the Theme of the Menu Widgets.", "Cambia el tema de los widgets del menú.");
        result.emplace("Skip Pickup Messages for Bottle Swipes.", "Omitir mensajes de recogida al usar botellas.");
        result.emplace("Removes the timer to play back the song.", "Elimina el temporizador para reproducir la canción.");
        result.emplace("Enables the separate Time Splits Window.", "Activa la ventana independiente de tiempos parciales.");
        result.emplace("Enables the separate Save Editor Window.", "Activa la ventana independiente del editor de partidas.");
        result.emplace("Enables the separate Audio Editor Window.", "Activa la ventana independiente del editor de audio.");
        result.emplace("Skips the Lost Woods Ocarina Memory Game.", "Omite el juego de memoria de la ocarina del Bosque Perdido.");
        result.emplace("Enables the separate Value Viewer Window.", "Activa la ventana independiente del visor de valores.");
        result.emplace("Enables the separate Actor Viewer Window.", "Activa la ventana independiente del visor de actores.");
        result.emplace("Enables the separate Gfx Debugger Window.", "Activa la ventana independiente del depurador gráfico.");
        result.emplace("Switches Link's age and reloads the area.", "Cambia la edad de Link y recarga el área.");
        result.emplace("Enables the separate Hook Debugger Window.", "Activa la ventana independiente del depurador de ganchos.");
        result.emplace("Enables the separate Message Viewer Window.", "Activa la ventana independiente del visor de mensajes.");
        result.emplace("Enables the separate Gameplay Stats Window.", "Activa la ventana independiente de estadísticas de juego.");
        result.emplace("Amy's block pushing puzzle instantly solved.", "El rompecabezas de bloques de Amy se resuelve al instante.");
        result.emplace("The time between groups of Leevers spawning.", "El tiempo entre grupos de Leevers que aparecen.");
        result.emplace("Turn on/off changes to the Fishing behavior.", "Activa o desactiva cambios en el comportamiento de pesca.");
        result.emplace("Enables the separate Cosmetics Editor Window.", "Activa la ventana independiente del editor de cosméticos.");
        result.emplace("Enables the separate Collision Viewer Window.", "Activa la ventana independiente del visor de colisiones.");
        result.emplace("Respawn with Full Health instead of 3 hearts.", "Reaparecer con toda la salud en lugar de 3 corazones.");
        result.emplace("Enables the separate Additional Timers Window.", "Activa la ventana independiente de temporizadores adicionales.");
        result.emplace("Skip Pickup Messages for new Consumable Items.", "Omitir mensajes de recogida para consumibles nuevos.");
        result.emplace("Syncs the in-game time with the real world time.", "Sincroniza la hora del juego con la hora real.");
        result.emplace("Enables the separate Display List Viewer Window.", "Activa la ventana independiente del visor de listas de dibujo.");
        result.emplace("Enables the separate Randomizer Settings Window.", "Activa la ventana independiente de configuración del randomizador.");
        result.emplace("Enables the separate Item Tracker Settings Window.", "Activa la ventana independiente de configuración del rastreador de objetos.");
        result.emplace("Enables the separate Input Viewer Settings Window.", "Activa la ventana independiente de configuración del visor de entradas.");
        result.emplace("Allows you to use any item at any location", "Permite usar cualquier objeto en cualquier lugar");
        result.emplace("Enemies and Bosses spawn with random sizes.", "Los enemigos y jefes aparecen con tamaños aleatorios.");
        result.emplace("Bombchus will sometimes drop in place of Bombs.", "A veces aparecerán Bombchus en lugar de bombas.");
        result.emplace("Which corner of the screen notifications appear in.", "En qué esquina de la pantalla aparecen las notificaciones.");
        result.emplace("Enables the separate Check Tracker Settings Window.", "Activa la ventana independiente de configuración del rastreador de comprobaciones.");
        result.emplace("All dogs can be traded in and will count as Richard.", "Todos los perros se pueden intercambiar y cuentan como Richard.");
        result.emplace("Turn on/off changes to the Bombchu Bowling behavior.", "Activa o desactiva cambios en el comportamiento de la bolera de Bombchus.");
        result.emplace("Turn on/off changes to the shooting gallery behavior.", "Activa o desactiva cambios en el comportamiento de la galería de tiro.");
        result.emplace("Makes the cursor always visible, even in full screen.", "Hace que el cursor siempre sea visible, incluso en pantalla completa.");
        result.emplace("Enables the separate Entrance Tracker Settings Window.", "Activa la ventana independiente de configuración del rastreador de entradas.");
        result.emplace("A Wallmaster follows Link everywhere, don't get caught!", "Un Wallmaster sigue a Link a todas partes; ¡no te dejes atrapar!");
        result.emplace("Turn on/off changes to the Frogs' Ocarina Game behavior.", "Activa o desactiva cambios en el comportamiento del juego de la ocarina de las ranas.");
        result.emplace("Prevent forced conversations with Navi and/or other NPCs.", "Evita conversaciones forzadas con Navi u otros personajes.");
        result.emplace("Makes the L and R buttons in the pause menu the same color.", "Hace que los botones L y R del menú de pausa tengan el mismo color.");
        result.emplace("Translate the Debug Warp Screen based on the game language.", "Traduce la pantalla de teletransporte de depuración según el idioma del juego.");
        result.emplace("Prevents the Deku Shield from burning on contact with fire.", "Evita que el escudo Deku se queme al entrar en contacto con el fuego.");
        result.emplace("Clears the cutscene pointer to a value safe for wrong warps.", "Limpia el puntero de cinemática con un valor seguro para teletransportes incorrectos.");
        result.emplace("Turn on/off changes to the Lost Woods Ocarina Game behavior.", "Activa o desactiva cambios en el comportamiento del juego de la ocarina del Bosque Perdido.");
        result.emplace("Opens the folder that contains the save and mods folders, etc.", "Abre la carpeta que contiene las carpetas de partidas, mods y demás.");
        result.emplace("Matches interpolation value to the refresh rate of your display.", "Ajusta el valor de interpolación a la frecuencia de actualización de tu pantalla.");
        result.emplace("Logs some resources as XML when they're loaded in binary format.", "Registra algunos recursos como XML cuando se cargan en formato binario.");
        result.emplace("Correctly centers the Navi text prompt on the HUD's C-Up button.", "Centra correctamente el aviso de Navi en el botón C arriba del HUD.");
        result.emplace("Every fish in the Fishing Pond will always be a Hyrule Loach.\n\n", "Todos los peces del estanque serán siempre lochas de Hyrule.\n\n");
        result.emplace("Don't increase crawl speed when exiting glitch-useful crawlspaces.", "No aumenta la velocidad de arrastre al salir de pasadizos útiles para glitches.");
        result.emplace("Make crouch stabbing always do the same damage as a regular slash.", "Hace que la estocada agachado siempre cause el mismo daño que un tajo normal.");
        result.emplace("When a line is stable, guarantee bite. Otherwise use Default logic.", "Cuando la línea esté estable, garantiza la picada; de lo contrario usa la lógica predeterminada.");
        result.emplace("Always get the Heart Piece/Purple Rupee from the Spinning Goron Pot.", "Obtén siempre el fragmento de corazón o la rupia morada de la vasija Goron giratoria.");
        result.emplace("Disables Random Drops, except from the Goron Pot, Dampe, and Bosses.", "Desactiva los objetos aleatorios, excepto los de la vasija Goron, Dampé y los jefes.");
        result.emplace("Integer scales the image. Only available in Pixel Perfect Mode.", "Escala la imagen en múltiplos enteros. Solo está disponible en modo de píxel perfecto.");
        result.emplace("Restores the wider range of certain shutter doors from NTSC 1.0.\n", "Restaura el rango más amplio de ciertas puertas corredizas de NTSC 1.0.\n");
        result.emplace("Rupees reduce over time, Link suffers damage when the count hits 0.", "Las rupias disminuyen con el tiempo y Link recibe daño cuando llegan a 0.");
        result.emplace("The log level determines which messages are printed to the console.", "El nivel de registro determina qué mensajes se imprimen en la consola.");
        result.emplace("Matches the color of maps & compasses to the dungeon they belong to. ", "Hace coincidir el color de mapas y brújulas con la mazmorra a la que pertenecen. ");
        result.emplace("The ammunition at the start of the Shooting Gallery minigame as Child.", "La munición inicial del minijuego de la galería de tiro como niño.");
        result.emplace("The ammunition at the start of the Shooting Gallery minigame as Adult.", "La munición inicial del minijuego de la galería de tiro como adulto.");
        result.emplace("Prevents the big Cucco from appearing in the Bombchu Bowling minigame.", "Evita que aparezca el cucco grande en el minijuego de Bombchu Bowling.");
        result.emplace("Once a hook as been set, Fish will never let go while being reeled in.", "Una vez que el anzuelo se engancha, el pez nunca se soltará al recogerlo.");
        result.emplace("Removes tearing, but clamps your max FPS to your displays refresh rate.", "Elimina el tearing, pero limita los FPS máximos a la frecuencia de actualización de la pantalla.");
        result.emplace("Prevents bugs from automatically despawning after a while when dropped.", "Evita que los insectos desaparezcan automáticamente después de un tiempo al soltarlos.");
        result.emplace("Prevents fish from automatically despawning after a while when dropped.", "Evita que los peces desaparezcan automáticamente después de un tiempo al soltarlos.");
        result.emplace("Prevents the small Cucco from appearing in the Bombchu Bowling minigame.", "Evita que aparezca el cucco pequeño en el minijuego de Bombchu Bowling.");
        result.emplace("Allows dogs to follow you anywhere you go, even if you leave the Market.", "Permite que los perros te sigan a cualquier lugar, incluso si sales del mercado.");
        result.emplace("Disables 2D pre-rendered backgrounds. Enable this when using a mod that ", "Desactiva los fondos 2D prer renderizados. Actívalo al usar un mod que ");
        result.emplace("Instantly return the Boomerang to Link by pressing its item button while ", "Devuelve instantáneamente el bumerán a Link al pulsar su botón de objeto mientras ");
        result.emplace("When obtaining Rupees, randomize what the Rupee is called in the textbox.", "Al obtener rupias, aleatoriza el nombre de la rupia en el cuadro de texto.");
        result.emplace("Makes Link always kick the chest to open it, instead of doing the longer ", "Hace que Link siempre dé una patada al cofre para abrirlo, en vez de realizar la animación más larga ");
        result.emplace("Make Deku Nuts explode Bombs, similar to how they interact with Bombchus. ", "Hace que las nueces Deku detonen bombas, de forma similar a su interacción con Bombchus. ");
        result.emplace("The Pond Owner will not ask to confirm if you want to keep a smaller Fish.", "El dueño del estanque no pedirá confirmar si quieres conservar un pez más pequeño.");
        result.emplace("Restore the original red blood from NTSC 1.0/1.1. Disable for Green blood.", "Restaura la sangre roja original de NTSC 1.0/1.1. Desactívalo para usar sangre verde.");
        result.emplace("The skybox in the background of the File Select screen will go through the ", "El cielo del fondo de la pantalla de selección de partida recorrerá el ");
        result.emplace("Fixes the Broken Giant's Knife flag not being reset when Medigoron fixes it.", "Corrige que la bandera del Cuchillo Gigante roto no se restablezca cuando Medigoron lo arregla.");
        result.emplace("Adds back in a delay after unpausing before the game resumes playing again, ", "Vuelve a añadir un retraso después de quitar la pausa antes de reanudar el juego, ");
        result.emplace("F5 to save, F6 to change slots, F7 to load", "F5 para guardar, F6 para cambiar de ranura, F7 para cargar");
        result.emplace("Gives you the glitched damage value of the quick put away glitch.", "Te da el valor de daño glitcheado del glitch de guardado rápido.");
        result.emplace("Greatly decreases cast time of Farore's Wind magic spell.", "Reduce considerablemente el tiempo de lanzamiento del hechizo Viento de Farore.");
        result.emplace("Child Minimum Weight: %d lbs.", "Peso mínimo de niño: %d lb");
        result.emplace("Adult Minimum Weight: %d lbs.", "Peso mínimo de adulto: %d lb");
        result.emplace("Damage Multiplier", "Multiplicador de daño");
        result.emplace("Despawn Timers", "Temporizadores de desaparición");
        result.emplace("Delete File on Death", "Eliminar partida al morir");
        result.emplace("Change Age", "Cambiar edad");
        result.emplace("Camera Fixes", "Correcciones de cámara");
        result.emplace("Bunny Hood Effect", "Efecto de la capucha de conejo");
        result.emplace("Bomb Timer Multiplier", "Multiplicador del temporizador de bombas");
        result.emplace("Randomizer Settings", "Configuración del randomizador");
        result.emplace("Check Tracker", "Rastreador de comprobaciones");
        result.emplace("Entrance Tracker", "Rastreador de entradas");
        result.emplace("Item Tracker", "Rastreador de objetos");
        result.emplace("Adds a prompt to equip newly-obtained Swords, Shields, and Tunics.",
                       "Añade un aviso para equipar las espadas, escudos y túnicas recién obtenidos.");
        result.emplace("Allow Link to enter Jabu-Jabu without feeding him a fish.",
                       "Permitir que Link entre en Jabu-Jabu sin darle un pez.");
        result.emplace("Allow Link to put items away without having to wait around.",
                       "Permitir que Link guarde objetos sin tener que esperar.");
        result.emplace("Allow multi-windows", "Permitir varias ventanas");
        result.emplace("Always Show Dungeon Entrances", "Mostrar siempre las entradas de mazmorra");
        result.emplace("Always Win Dampe Digging Game", "Ganar siempre el juego de excavación de Dampé");
        result.emplace("Always Win Goron Pot", "Ganar siempre el juego de la vasija Goron");
        result.emplace("Animated Link in Pause Menu", "Link animado en el menú de pausa");
        result.emplace("Aiming Reticle for the Bow/Slingshot", "Mira para el arco y la resortera");
        result.emplace("Aiming Reticle for Boomerang", "Mira para el bumerán");
        result.emplace("All Dogs are Richard", "Todos los perros son Richard");
        result.emplace("All Fish are Hyrule Loaches", "Todos los peces son lochas de Hyrule");
        result.emplace("All Major Bosses move and act twice as fast.",
                       "Todos los jefes principales se mueven y actúan el doble de rápido.");
        result.emplace("All Regular Enemies and Mini-Bosses move and act twice as fast.",
                       "Todos los enemigos normales y minibosses se mueven y actúan el doble de rápido.");
        result.emplace("Better Bombchu Shopping", "Mejor compra de Bombchus");
        result.emplace("Better Farore's Wind", "Mejor Viento de Farore");
        result.emplace("Better Owl", "Mejor búho");
        result.emplace("Bugs don't Despawn", "Los insectos no desaparecen");
        result.emplace("Change Age", "Cambiar edad");
        result.emplace("Climb Everything", "Trepar por todo");
        result.emplace("Damage Multiplier", "Multiplicador de daño");
        result.emplace("Delete File on Death", "Eliminar partida al morir");
        result.emplace("Disable Black Bar Letterboxes", "Desactivar bandas negras cinematográficas");
        result.emplace("Disable HUD Heart Animations", "Desactivar animaciones de corazones del HUD");
        result.emplace("Disable Random Camera Wiggle at Low Health.",
                       "Desactivar el movimiento aleatorio de la cámara con poca salud.");
        result.emplace("Dogs Follow You Everywhere", "Los perros te siguen a todas partes");
        result.emplace("Drops Don't Despawn", "Los objetos caídos no desaparecen");
        result.emplace("Enable a delay after unpausing before the game resumes playing again.",
                       "Activar un retraso después de quitar la pausa antes de reanudar el juego.");
        result.emplace("Clear", "Limpiar");
        result.emplace("Clear Search", "Limpiar búsqueda");
        result.emplace("Quit SoH", "Salir de SoH");
        result.emplace("Are you sure you want to quit SoH?", "¿Seguro que quieres salir de SoH?");
        result.emplace("Cancel", "Cancelar");
        result.emplace("Reset", "Restablecer");
        result.emplace("Close Menu (Esc)", "Cerrar menú (Esc)");
        result.emplace("Advanced Resolution Enabled", "Resolución avanzada activada");
        result.emplace("Vertical Resolution Toggle Enabled", "Alternancia de resolución vertical activada");
        result.emplace("Vertical Resolution Toggle is Off", "La alternancia de resolución vertical está desactivada");
        result.emplace("Save Not Loaded", "No hay una partida cargada");
        result.emplace("Debug Mode is Disabled", "El modo de depuración está desactivado");
        result.emplace("Frame Advance is Disabled", "El avance de fotogramas está desactivado");
        result.emplace("Advanced Resolution is Disabled", "La resolución avanzada está desactivada");
        result.emplace("Disabling VSync not supported", "Desactivar VSync no es compatible");
        result.emplace("Windowed Fullscreen not supported", "La pantalla completa en ventana no es compatible");
        result.emplace("Available Only on DirectX", "Disponible únicamente en DirectX");
        result.emplace("Not Available on DirectX", "No disponible en DirectX");
        result.emplace("Match Refresh Rate is Enabled", "La coincidencia de frecuencia de actualización está activada");
        result.emplace("This setting is disabled because: \n", "Esta configuración está desactivada porque: \n");
        result.emplace("Race Lockout Active", "Bloqueo de carrera activo");
        result.emplace("Start typing to see results.", "Empieza a escribir para ver resultados.");
        result.emplace("Search Results", "Resultados de búsqueda");
        result.emplace("No results found", "No se encontraron resultados");
        result.emplace("Adjust the number of notes the Skull Kids play to start the first round.",
                       "Ajusta la cantidad de notas que tocan los Niños Calavera para iniciar la primera ronda.");
        result.emplace("Adjust the number of notes you need to play to end the first round.",
                       "Ajusta la cantidad de notas que debes tocar para terminar la primera ronda.");
        result.emplace("Adjust the number of notes you need to play to end the second round.",
                       "Ajusta la cantidad de notas que debes tocar para terminar la segunda ronda.");
        result.emplace("Adjust the number of notes you need to play to end the third round.",
                       "Ajusta la cantidad de notas que debes tocar para terminar la tercera ronda.");
        result.emplace("Adjusts rate Dampe drops flames during race.",
                       "Ajusta la frecuencia con la que Dampé deja llamas durante la carrera.");
        result.emplace("Adjusts the Horizontal Culling Plane to account for Widescreen Resolutions.",
                       "Ajusta el plano de recorte horizontal para las resoluciones panorámicas.");
        result.emplace("Allows Bombchus to explode out of bounds. Similar to GameCube and Wii VC.",
                       "Permite que los Bombchus exploten fuera de los límites, como en GameCube y Wii VC.");
        result.emplace("Allows Child Link to use a Bow with Arrows.",
                       "Permite que Link niño use un arco con flechas.");
        result.emplace("Allows equipping the Tunics and Boots to C-Buttons/D-pad.",
                       "Permite equipar túnicas y botas en los botones C y la cruceta.");
        result.emplace("Allows Link to freely change age by playing the Song of Time.",
                       "Permite que Link cambie de edad libremente al tocar la Canción del Tiempo.");
        result.emplace("Allows masks to be equipped normally from the pause menu as adult.",
                       "Permite equipar máscaras normalmente desde el menú de pausa siendo adulto.");
        result.emplace("Allows you to walk through walls.", "Permite atravesar paredes.");
        result.emplace("Always shows dungeon entrance icons on the Minimap.",
                       "Muestra siempre los iconos de entradas de mazmorra en el minimapa.");
        result.emplace("Better Ammo Rendering in Pause Menu", "Mejor representación de munición en el menú de pausa");
        result.emplace("Better Debug Warp Screen", "Mejor pantalla de teletransporte de depuración");
        result.emplace("Better Owl", "Mejor búho");
        result.emplace("Chest Size & Texture Matches Contents", "El tamaño y la textura del cofre coinciden con su contenido");
        result.emplace("Disable 2D Pre-Rendered Scenes", "Desactivar escenas 2D prerenderizadas");
        result.emplace("Disable Bomb Billboarding", "Desactivar billboarding de bombas");
        result.emplace("Disable Grotto Fixed Rotation", "Desactivar rotación fija de grutas");
        result.emplace("Disable LOD", "Desactivar nivel de detalle");
        result.emplace("Disable Random Camera Wiggle at Low Health.",
                       "Desactivar el movimiento aleatorio de cámara con poca salud.");
        result.emplace("Disables warning text when you don't have on the Goron/Zora Tunic",
                       "Desactiva el aviso cuando no llevas la túnica Goron o Zora");
        result.emplace("Disables the automatic re-centering of the camera when idle.",
                       "Desactiva el recentrado automático de la cámara cuando está inactiva.");
        result.emplace("Disables the Beating Animation of the Hearts on the HUD.",
                       "Desactiva la animación palpitante de los corazones del HUD.");
        result.emplace("Draw Distance", "Distancia de dibujado");
        result.emplace("Drops from enemies, grass, etc. don't disappear after a set amount of time.",
                       "Los objetos de enemigos, hierba, etc. no desaparecen después de cierto tiempo.");
        result.emplace("Dying will delete your file.\n\n", "Morir eliminará tu partida.\n\n");
        result.emplace("Allows Child Link to use a Bow with Arrows.\n", "Permite que Link niño use un arco con flechas.\n");
        result.emplace("Allows Link to freely change age by playing the Song of Time.\n",
                       "Permite que Link cambie libremente de edad al tocar la Canción del Tiempo.\n");
        result.emplace("Allows any item to be equipped, regardless of age.\n", "Permite equipar cualquier objeto sin importar la edad.\n");
        result.emplace("Hides most of the UI when not needed.\n", "Oculta la mayor parte de la interfaz cuando no es necesaria.\n");
        result.emplace("Classic: Original visual presentation.\n", "Clásico: presentación visual original.\n");
        result.emplace("Passive Infinite Sword Glitch\n", "Glitch de espada infinita pasivo\n");
        result.emplace("Disables sandstorm effect in Haunted Wasteland.", "Desactiva el efecto de tormenta de arena del Desierto Encantado.");
        result.emplace("Syncs the in-game time with the real world time.", "Sincroniza la hora del juego con la hora real.");
        result.emplace("Configure what happens when starting or resetting the game.\n\nDefault: LUS logo -> N64 logo\nAuthentic: N64 logo only\nFile Select: Skip to file select menu",
                       "Configura qué ocurre al iniciar o reiniciar el juego.\n\nPredeterminado: logo de LUS -> logo de N64\nAuténtico: solo logo de N64\nSelección de partida: saltar al menú de selección");
        result.emplace("Changes Heart Piece and Heart Container functionality.\n\n - Each Heart Container or full Heart Piece reduces Link's Hearts by 1.\n - Can be enabled retroactively after a File has already started.",
                       "Cambia el funcionamiento de los fragmentos y recipientes de corazón.\n\n - Cada recipiente o fragmento completo reduce en 1 los corazones de Link.\n - Se puede activar retroactivamente después de iniciar una partida.");
        result.emplace("Turns on OoT Beta Quest. *WARNING*: This will reset your game!",
                       "Activa Beta Quest de OoT. *ADVERTENCIA*: ¡esto reiniciará tu partida!");
        result.emplace("Modifies damage taken after falling into a void:\n2x: Can survive void damage from the start of the game.\n4x: Cannot survive void damage from the start of the game.\n8x: Can survive void damage twice with max health without double defense.\n16x: Can survive void damage with max health without double defense.\n32x: Can survive void damage with max health and double defense.",
                       "Modifica el daño recibido al caer al vacío:\n2x: puedes sobrevivir al daño del vacío desde el inicio.\n4x: no puedes sobrevivir al daño del vacío desde el inicio.\n8x: puedes sobrevivir dos veces con salud máxima sin defensa doble.\n16x: puedes sobrevivir con salud máxima sin defensa doble.\n32x: puedes sobrevivir con salud máxima y defensa doble.");
        result.emplace("Allows masks to be equipped normally from the pause menu as adult.", "Permite equipar máscaras normalmente desde el menú de pausa como adulto.");
        result.emplace("Skips Link's taking breath animation after coming up from water. This setting does not interfere with getting items from underwater.", "Omite la animación de respiración de Link al salir del agua. Esta opción no impide recoger objetos bajo el agua.");
        result.emplace("Makes Link always kick the chest to open it, instead of doing the longer chest opening animation for major items.", "Hace que Link siempre dé una patada al cofre para abrirlo, en vez de usar la animación larga para objetos importantes.");
        result.emplace("Replace Navi's overworld quest hints with rando-related gameplay hints.", "Sustituye las pistas de misiones de Navi en el mundo por pistas de juego relacionadas con el randomizer.");
        result.emplace("Prevents bugs from automatically despawning after a while when dropped.", "Evita que los insectos desaparezcan automáticamente después de un tiempo al soltarlos.");
        result.emplace("Prevents fish from automatically despawning after a while when dropped.", "Evita que los peces desaparezcan automáticamente después de un tiempo al soltarlos.");
        result.emplace("Instantly return the Boomerang to Link by pressing its item button while the Boomerang is out.", "Devuelve instantáneamente el bumerán a Link al pulsar su botón mientras está fuera.");
        result.emplace("The Pond Owner will not ask to confirm if you want to keep a smaller Fish.", "El dueño del estanque no pedirá confirmar si quieres conservar un pez más pequeño.");
        result.emplace("Restore the original red blood from NTSC 1.0/1.1. Disable for Green blood.", "Restaura la sangre roja original de NTSC 1.0/1.1. Desactívalo para usar sangre verde.");
        result.emplace("Prevent integer scaling from exceeding screen bounds.\n(Makes screen bounds take priority over specified factor.)", "Evita que el escalado entero supere los límites de la pantalla.\n(Los límites de pantalla tienen prioridad sobre el factor indicado.)");
        result.emplace("Prevents integer scaling factor from exceeding screen bounds.\n\nEnabled: Will clamp the scaling factor and display a gentle warning in the resolution editor.\nDisabled: Will allow scaling to exceed screen bounds, for users who want to crop overscan.\n\n " ICON_FA_INFO_CIRCLE " Please note that exceeding screen bounds may show a scroll bar on-screen.", "Evita que el factor de escalado entero supere los límites de la pantalla.\n\nActivado: limita el factor y muestra un aviso en el editor de resolución.\nDesactivado: permite superar los límites para recortar el overscan.\n\n " ICON_FA_INFO_CIRCLE " Ten en cuenta que superar los límites puede mostrar una barra de desplazamiento.");
        result.emplace("Allow integer scale factor to go +1 above maximum screen bounds.", "Permitir que el factor de escalado entero supere en +1 los límites máximos de pantalla.");
        result.emplace("A scroll bar may become visible if screen bounds are exceeded.", "Puede aparecer una barra de desplazamiento si se superan los límites de la pantalla.");
        result.emplace("Click to reset a console variable that may be causing this.", "Haz clic para restablecer una variable de consola que podría estar causando esto.");
        result.emplace("Aiming with the Boomerang will display a reticle as with the Hookshot.", "Apuntar con el bumerán mostrará una mira como con el gancho.");
        result.emplace("Prevents ReDeads and Gibdos from being able to freeze you with their scream.", "Evita que los Redead y Gibdo puedan congelarte con su grito.");
        result.emplace("Prevents immediately falling off climbable surfaces if climbing on the edges.", "Evita caer inmediatamente de superficies escalables al trepar por los bordes.");
        result.emplace("The number of Bombchus available at the start of the Bombchu Bowling minigame.", "La cantidad de Bombchus disponibles al comenzar el minijuego de Bombchu Bowling.");
        result.emplace("Allows multiple windows to be opened at once. Requires a reload to take effect.", "Permite abrir varias ventanas a la vez. Requiere reiniciar para aplicar el cambio.");
        result.emplace("Automatically sets scale factor to fit window. Only available in Pixel Perfect Mode.", "Ajusta automáticamente el factor para encajar en la ventana. Solo está disponible en modo de píxel perfecto.");
        result.emplace("Optimized Debug Warp Screen, with the added ability to chose entrances and time of day.", "Pantalla de teletransporte de depuración optimizada, con posibilidad de elegir entradas y hora del día.");
        result.emplace("Use Custom graphics for Dungeon Keys, Big and Small, so that they can be easily told apart.", "Usa gráficos personalizados para llaves de mazmorra grandes y pequeñas para distinguirlas fácilmente.");
        result.emplace("Override the resolution scale slider and use the settings below, irrespective of window size.", "Ignora el control de escala de resolución y usa las opciones siguientes sin importar el tamaño de la ventana.");
        result.emplace("Wearing the Bunny Hood grants a speed and jump boost like in Majora's Mask.\n", "Llevar la capucha de conejo otorga velocidad y salto mejorados como en Majora's Mask.\n");
        result.emplace("Allows the cursor on the pause menu to be over any slot. Sometimes required in Randomizer to select certain items.", "Permite colocar el cursor del menú de pausa sobre cualquier casilla. A veces es necesario en el randomizer para seleccionar ciertos objetos.");
        result.emplace("Resets the Navi timer on scene change. If you have already talked to her, she will try and talk to you again, instead of needing a save warp or death.", "Restablece el temporizador de Navi al cambiar de escena. Si ya hablaste con ella, intentará hablarte de nuevo sin necesitar teletransportarte con una partida o morir.");
        result.emplace("Aiming with a Bow or Slingshot will display a reticle as with the Hookshot when the projectile is ready to fire.", "Apuntar con un arco o resortera mostrará una mira como con el gancho cuando el proyectil esté listo para disparar.");
        result.emplace("Fixes camera slightly drifting to the left when standing still due to a math error. May impact certain glitches.", "Corrige la ligera deriva de la cámara hacia la izquierda al estar quieto debido a un error matemático. Puede afectar algunos glitches.");
        result.emplace("Fixes camera swing rate when the player falls off a ledge and the camera swings around. May impact certain glitches.", "Corrige la velocidad de giro de la cámara cuando el jugador cae de un saliente y la cámara gira. Puede afectar algunos glitches.");
        result.emplace("Enables Skulltula Debug, when moving the cursor in the menu above various map icons (boss key, compass, map screen locations, etc.) will set the GS bits in that area.\nUSE WITH CAUTION AS IT DOES NOT UPDATE THE GS COUNT!", "Activa la depuración de Skulltulas: al mover el cursor sobre ciertos iconos del mapa (llave de jefe, brújula, ubicaciones, etc.) activa los indicadores GS de esa zona.\n¡ÚSALO CON PRECAUCIÓN: NO ACTUALIZA EL CONTADOR DE GS!");
        result.emplace("Stops masks from automatically unequipping on certain situations:\n- When entering a new scene\n- When not in any C-Button or the D-pad\n- When saving and quitting\n- When dying\n- When traveling through time (if \"Masks Equippable as Adult\" is activated)", "Evita que las máscaras se quiten automáticamente en ciertas situaciones:\n- Al entrar en una escena nueva\n- Cuando no están asignadas a un botón C o a la cruceta\n- Al guardar y salir\n- Al morir\n- Al viajar en el tiempo (si está activado \"Máscaras equipables como adulto\")");
        result.emplace("Turns Bunny Hood Invisible while still maintaining its effects.", "Vuelve invisible la capucha de conejo, manteniendo sus efectos.");
        result.emplace("After completing the mask trading sub-quest, press A and any direction on the mask slot to change masks.", "Después de completar la misión secundaria de intercambio de máscaras, pulsa A y cualquier dirección sobre la casilla de máscara para cambiarla.");
        result.emplace("Modifies damage taken after falling into a void:\n2x: Can survive void damage from the start of the game.\n4x: Cannot survive void damage from the start of the game.\n8x: Can survive void damage twice with max health without double defense.\n16x: Can survive void damage with max health without double defense.\n32x: Can survive void damage with max health and double defense.\n64x: Cannot survive void damage.", "Modifica el daño recibido al caer al vacío:\n2x: puedes sobrevivir al daño del vacío desde el inicio.\n4x: no puedes sobrevivir al daño del vacío desde el inicio.\n8x: puedes sobrevivir dos veces con salud máxima sin defensa doble.\n16x: puedes sobrevivir con salud máxima sin defensa doble.\n32x: puedes sobrevivir con salud máxima y defensa doble.\n64x: no puedes sobrevivir al daño del vacío.");
        result.emplace("Automatically shows Debug Warp Screen when starting or resetting the game.\nThis option takes precedence over \"Boot Sequence\" option.", "Muestra automáticamente la pantalla de teletransporte de depuración al iniciar o reiniciar el juego.\nEsta opción tiene prioridad sobre \"Secuencia de inicio\".");
        result.emplace("Multiplies your output resolution by the value inputted, as a more intensive but effective form of anti-aliasing.", "Multiplica la resolución de salida por el valor indicado como una forma más exigente pero efectiva de antialiasing.");
        result.emplace("Activates MSAA (multi-sample anti-aliasing) from 2x up to 8x, to smooth the edges of rendered geometry.\nHigher sample count will result in smoother edges on models, but may reduce performance.", "Activa MSAA (antialiasing multimuestra) de 2x a 8x para suavizar los bordes de la geometría renderizada.\nUn mayor número de muestras suaviza más los modelos, pero puede reducir el rendimiento.");
        result.emplace("Replaces fixed enemies throughout the game with a random enemy. Bosses, Mini-Bosses and a few specific regular enemies are excluded.\nEnemies that need more than Deku Nuts & either Deku Sticks or a sword to kill are excluded from spawning in \"clear enemy\" rooms.\n\n- Random: Enemies are randomized every time you load a room.\n- Random (Seeded): Enemies are randomized based on the current randomizer seed/file.\n", "Sustituye los enemigos fijos del juego por enemigos aleatorios. Se excluyen jefes, minibosses y algunos enemigos normales.\nLos enemigos que requieren más que nueces Deku y palos Deku o una espada para morir no aparecen en salas de \"eliminar enemigos\".\n\n- Aleatorio: los enemigos se aleatorizan cada vez que cargas una sala.\n- Aleatorio (con semilla): se aleatorizan según la semilla o partida actual del randomizer.\n");
        result.emplace("Scales normal enemies Health with their randomized size. *This will NOT affect Bosses!*", "Escala la salud de los enemigos normales según su tamaño aleatorio. *¡Esto NO afecta a los jefes!*");
        result.emplace("Changes the behavior of debug file select creation (creating a save file on slot 1 with debug mode on):\n- Off: The debug save file will be a normal savefile.\n- Vanilla: The debug save file will be the debug save file from the original game.\n- Maxed: The debug save file will be a save file with all of the items & upgrades.", "Cambia el comportamiento al crear una partida de depuración en la ranura 1 con el modo de depuración activado:\n- Desactivado: será una partida normal.\n- Original: será la partida de depuración del juego original.\n- Máxima: tendrá todos los objetos y mejoras.");
        result.emplace("Don't skip cutscenes that are associated with useful glitches. Currently, it is only the Fire Temple Darunia CS, Forest Temple Poe Sisters CS, Dodongo Boss Door Switch CS, Water Temple Dragon Switch CS, the Box Skip One Point in Jabu, Early Hammer Switch CS in MQ Spirit, and Cow Switch Chest CS in MQ Jabu.", "No omite las cinemáticas relacionadas con glitches útiles. Actualmente son la cinemática de Darunia del Templo de Fuego, las Hermanas Poe del Templo del Bosque, el interruptor de puerta del jefe Dodongo, el interruptor dragón del Templo del Agua, el salto de caja de Jabu, el interruptor temprano del martillo en Espíritu MQ y el cofre del interruptor de vaca en Jabu MQ.");
        result.emplace("Skip Pickup Messages for Bottle Swipes.", "Omitir mensajes de recogida al usar botellas.");
        result.emplace("Sail is a networking protocol designed to facilitate remote control of the Ship of Harkinian client. It is intended to allow external programs to interact with the game. Click the clipboard icon to copy the Sail page to your clipboard.", "Sail es un protocolo de red diseñado para facilitar el control remoto del cliente Ship of Harkinian. Permite que programas externos interactúen con el juego. Pulsa el icono del portapapeles para copiar la página de Sail.");
        result.emplace("When viewers spawn enemies, the enemy will have a name tag above them with the viewer's name.", "Cuando los espectadores generan enemigos, estos tendrán encima una etiqueta con el nombre del espectador.");
        result.emplace("Enemies spawned by CrowdControl won't be considered for \"clear enemy rooms\", so they don't need to be killed to complete these rooms.", "Los enemigos generados por Crowd Control no se tendrán en cuenta para las \"salas de eliminar enemigos\", por lo que no es necesario matarlos para completarlas.");
        return result;
    }();

    const auto translated = catalog.find(text);
    return translated == catalog.end() ? text : translated->second.c_str();
#endif
}

// Automatically adds newlines to break up text longer than a specified number of characters
// Manually included newlines will still be respected and reset the line length
// If line is midword when it hits the limit, text should break at the last encountered space
std::string WrappedText(const char* text, unsigned int charactersPerLine) {
    std::string newText(text);
    const size_t tipLength = newText.length();
    int lastSpace = -1;
    int currentLineLength = 0;
    for (unsigned int currentCharacter = 0; currentCharacter < tipLength; currentCharacter++) {
        if (newText[currentCharacter] == '\n') {
            currentLineLength = 0;
            lastSpace = -1;
            continue;
        } else if (newText[currentCharacter] == ' ') {
            lastSpace = currentCharacter;
        }

        if ((currentLineLength >= charactersPerLine) && (lastSpace >= 0)) {
            newText[lastSpace] = '\n';
            currentLineLength = currentCharacter - lastSpace - 1;
            lastSpace = -1;
        }
        currentLineLength++;
    }

    return newText;
}

std::string WrappedText(const std::string& text, unsigned int charactersPerLine) {
    return WrappedText(text.c_str(), charactersPerLine);
}

void PaddedSeparator(bool padTop, bool padBottom, float extraVerticalTopPadding, float extraVerticalBottomPadding) {
    if (padTop) {
        Spacer(extraVerticalTopPadding);
    }
    ImGui::Separator();
    if (padBottom) {
        Spacer(extraVerticalBottomPadding);
    }
}

void Tooltip(const char* text) {
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", WrappedText(Translate(text)).c_str());
    }
}

void PushStyleMenu(const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(color.x, color.y, color.z, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ColorValues.at(Colors::DarkGray));
    ImGui::PushStyleColor(ImGuiCol_Border, ColorValues.at(Colors::DarkGray));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 15.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 3.0f);
}

void PushStyleMenu(Colors color) {
    PushStyleMenu(ColorValues.at(color));
}

void PopStyleMenu() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
}

bool BeginMenu(const char* label, Colors color) {
    bool dirty = false;
    PushStyleMenu(color);
    ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginMenu(Translate(label))) {
        dirty = true;
    }
    PopStyleMenu();
    return dirty;
}

void PushStyleMenuItem(const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, color);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20.0f, 15.0f));
}

void PushStyleMenuItem(Colors color) {
    PushStyleMenuItem(ColorValues.at(color));
}

void PopStyleMenuItem() {
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(1);
}

bool MenuItem(const char* label, const char* shortcut, Colors color) {
    bool dirty = false;
    PushStyleMenuItem(color);
    if (ImGui::MenuItem(Translate(label), shortcut)) {
        dirty = true;
    }
    PopStyleMenuItem();
    return dirty;
}

void PushStyleButton(const ImVec4& color, const ImVec2 padding) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 5.0f);
}

void PushStyleButton(Colors color, ImVec2 padding) {
    PushStyleButton(ColorValues.at(color), padding);
}

void PopStyleButton() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
}

void PushStyleInput(const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 5.0f);
}

void PushStyleInput(Colors color) {
    PushStyleInput(ColorValues.at(color));
}

void PopStyleInput() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(7);
}

void PushStyleHeader(const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(color.x, color.y, color.z, 0.6f));
}

void PushStyleHeader(Colors color) {
    PushStyleHeader(ColorValues.at(color));
}

void PopStyleHeader() {
    ImGui::PopStyleColor(3);
}

bool Button(const char* label, const ButtonOptions& options) {
    ImGui::BeginDisabled(options.disabled);
    PushStyleButton(options.color, options.padding);
    bool dirty = ImGui::Button(Translate(label), options.size);
    PopStyleButton();
    ImGui::EndDisabled();
    if (options.disabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        !Ship_IsCStringEmpty(options.disabledTooltip)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.disabledTooltip)).c_str());
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !Ship_IsCStringEmpty(options.tooltip)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.tooltip)).c_str());
    }
    return dirty;
}

bool WindowButton(const char* label, const char* cvarName, std::shared_ptr<Ship::GuiWindow> windowPtr,
                  const WindowButtonOptions& options) {
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, 0));
    std::string buttonText = label;
    bool dirty = false;
    if (CVarGetInteger(cvarName, 0)) {
        buttonText = ICON_FA_WINDOW_CLOSE " " + buttonText;
    } else {
        buttonText = ICON_FA_EXTERNAL_LINK_SQUARE " " + buttonText;
    }
    if (Button(buttonText.c_str(), { { options.tooltip, options.disabled, options.disabledTooltip },
                                     options.size,
                                     options.padding,
                                     options.color })) {
        windowPtr->ToggleVisibility();
        dirty = true;
    }
    ImGui::PopStyleVar();
    return dirty;
}

void PushStyleCheckbox(const ImVec4& color, ImVec2 padding) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 1.0f, 1.0f, 0.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 5.0f);
}

void PushStyleCheckbox(Colors color, ImVec2 padding) {
    PushStyleCheckbox(ColorValues.at(color), padding);
}

void PopStyleCheckbox() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
}

void Spacer(float height) {
    ImGui::Dummy(ImVec2(0.0f, height));
}

void Separator(bool padTop, bool padBottom, float extraVerticalTopPadding, float extraVerticalBottomPadding) {
    if (padTop) {
        Spacer(extraVerticalTopPadding);
    }
    ImGui::Separator();
    if (padBottom) {
        Spacer(extraVerticalBottomPadding);
    }
}

// Adds a "?" next to the previous ImGui item with a custom tooltip
void InsertHelpHoverText(const std::string& text) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "?");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("%s", WrappedText(Translate(text.c_str()), 60).c_str());
        ImGui::EndTooltip();
    }
}

void InsertHelpHoverText(const char* text) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "?");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("%s", WrappedText(Translate(text), 60).c_str());
        ImGui::EndTooltip();
    }
}

void RenderText(ImVec2 pos, const char* text, const char* text_end, bool hide_text_after_hash) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    // Hide anything after a '##' string
    const char* text_display_end;
    if (hide_text_after_hash) {
        text_display_end = ImGui::FindRenderedTextEnd(text, text_end);
    } else {
        if (!text_end)
            text_end = text + strlen(text); // FIXME-OPT
        text_display_end = text_end;
    }

    if (text != text_display_end) {
        window->DrawList->AddText(g.Font, g.FontSize, pos, ImGui::GetColorU32(ImGuiCol_Text), text, text_display_end);
        if (g.LogEnabled)
            ImGui::LogRenderedText(&pos, text, text_display_end);
    }
}

bool Checkbox(const char* _label, bool* value, const CheckboxOptions& options) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGui::BeginDisabled(options.disabled);

    bool above = options.labelPosition == LabelPositions::Above;
    bool lpFar = options.labelPosition == LabelPositions::Far;
    bool right = options.alignment == ComponentAlignments::Right;
    bool none = options.labelPosition == LabelPositions::None;

    std::string labelStr = (none ? "##" : "");
    labelStr.append(Translate(_label));

    const char* label = labelStr.c_str();

    PushStyleCheckbox(options.color, options.padding);
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    const float square_sz = ImGui::GetFrameHeight();
    ImVec2 pos = window->DC.CursorPos;

    if (right) {
        float labelOffsetX = (above ? 0 : (style.ItemInnerSpacing.x * 2.0f) + square_sz);
        if (!lpFar) {
            pos.x += ImGui::GetContentRegionAvail().x - (label_size.x + labelOffsetX);
        }
    }
    float bbAboveX = lpFar ? ImGui::GetContentRegionAvail().x
                           : (label_size.x + (above ? 0 : (style.ItemInnerSpacing.x * 2.0f) + square_sz));
    float bbAboveY = label_size.y + (above ? square_sz : 0) + (style.FramePadding.y * 2.0f);
    const ImRect total_bb(pos, pos + ImVec2(bbAboveX, bbAboveY));

    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id)) {
        PopStyleCheckbox();
        ImGui::EndDisabled();
        return false;
    }
    bool hovered, held, pressed;
    pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *value = !(*value);
        ImGui::MarkItemEdited(id);
    }
    ImVec2 checkPos = pos;
    ImVec2 labelPos = pos;
    if (options.labelPosition == LabelPositions::Above) {
        checkPos.y += label_size.y + (style.ItemInnerSpacing.y * 2.0f);
    } else {
        // Center with checkbox automatically
        labelPos.y += ImGui::GetStyle().FramePadding.y;
    }
    if (options.alignment == ComponentAlignments::Right) {
        checkPos.x = total_bb.Max.x - square_sz;
    } else {
        float labelFarOffset = ImGui::GetContentRegionAvail().x - label_size.x;
        float labelOffsetX = above ? 0 : (lpFar ? labelFarOffset : (style.ItemInnerSpacing.x * 2.0f) + square_sz);
        labelPos.x += labelOffsetX;
    }
    const ImRect check_bb(checkPos, checkPos + ImVec2(square_sz, square_sz));
    ImGui::RenderNavHighlight(total_bb, id);
    ImGui::RenderFrame(check_bb.Min, check_bb.Max,
                       ImGui::GetColorU32((held && hovered) ? ImGuiCol_FrameBgActive
                                          : hovered         ? ImGuiCol_FrameBgHovered
                                                            : ImGuiCol_FrameBg),
                       true, style.FrameRounding);
    ImU32 check_col = ImGui::GetColorU32(ImGuiCol_CheckMark);
    bool mixed_value = (g.LastItemData.ItemFlags & ImGuiItemFlags_MixedValue) != 0;
    if (mixed_value) {
        // Undocumented tristate/mixed/indeterminate checkbox (#2644)
        // This may seem awkwardly designed because the aim is to make ImGuiItemFlags_MixedValue supported by all
        // widgets (not just checkbox)
        ImVec2 pad(ImMax(1.0f, IM_TRUNC(square_sz / 3.6f)), ImMax(1.0f, IM_TRUNC(square_sz / 3.6f)));
        window->DrawList->AddRectFilled(check_bb.Min + pad, check_bb.Max - pad, check_col, style.FrameRounding);
    } else if (*value) {
        const float pad = ImMax(1.0f, IM_TRUNC(square_sz / 6.0f));
        ImGui::RenderCheckMark(window->DrawList, check_bb.Min + ImVec2(pad, pad), check_col, square_sz - pad * 2.0f);
    }
    RenderText(labelPos, label, ImGui::FindRenderedTextEnd(label), true);
    PopStyleCheckbox();
    ImGui::EndDisabled();
    if (options.disabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        !Ship_IsCStringEmpty(options.disabledTooltip)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.disabledTooltip)).c_str());
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !Ship_IsCStringEmpty(options.tooltip)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.tooltip)).c_str());
    }
    return pressed;
}

bool CVarCheckbox(const char* label, const char* cvarName, const CheckboxOptions& options) {
    bool dirty = false;
    bool value = (bool)CVarGetInteger(cvarName, options.defaultValue);
    if (Checkbox(label, &value, options)) {
        CVarSetInteger(cvarName, value);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        ShipInit::Init(cvarName);
        dirty = true;
    }
    return dirty;
}

bool StateButton(const char* str_id, const char* label, ImVec2 size, ButtonOptions options, ImGuiButtonFlags flags) {

    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }

    const ImGuiStyle& style = g.Style;
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    const ImGuiID id = window->GetID(str_id);
    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
    const float default_size = ImGui::GetFrameHeight();
    ImGui::ItemSize(size, (size.y >= default_size) ? g.Style.FramePadding.y : -1.0f);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    if (g.LastItemData.ItemFlags & ImGuiItemFlags_ButtonRepeat) {
        ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    }

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

    if (g.LastItemData.ItemFlags & ImGuiItemFlags_ButtonRepeat) {
        ImGui::PopItemFlag(); // ImGuiItemFlags_ButtonRepeat;
    }
    PushStyleButton(options.color);
    // Render
    const ImU32 bg_col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive
                                            : hovered         ? ImGuiCol_ButtonHovered
                                                              : ImGuiCol_Button);
    // const ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::RenderNavHighlight(bb, id);
    ImGui::RenderFrame(bb.Min, bb.Max, bg_col, true, g.Style.FrameRounding);
    ImGui::RenderTextClipped(bb.Min + (style.FramePadding * 0.35f), bb.Max - (style.FramePadding / 4), label, NULL,
                             &label_size, style.ButtonTextAlign, &bb);
    PopStyleButton();
    /*ImGui::RenderArrow(window->DrawList,
    bb.Min +
    ImVec2(ImMax(0.0f, (size.x - g.FontSize) * 0.5f), ImMax(0.0f, (size.y - g.FontSize) * 0.5f)),
    text_col, dir);*/

    IMGUI_TEST_ENGINE_ITEM_INFO(id, str_id, g.LastItemData.StatusFlags);
    return pressed;
}

float CalcComboWidth(const char* preview_value, ImGuiComboFlags flags) {
    ImGuiContext& g = *GImGui;

    const ImGuiStyle& style = g.Style;
    IM_ASSERT((flags & (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)) !=
              (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)); // Can't use both flags together
    if (flags & ImGuiComboFlags_WidthFitPreview)
        IM_ASSERT((flags & (ImGuiComboFlags_NoPreview | (ImGuiComboFlags)ImGuiComboFlags_CustomPreview)) == 0);

    const float arrow_size = (flags & ImGuiComboFlags_NoArrowButton) ? 0.0f : ImGui::GetFrameHeight();
    const float preview_width = ImGui::CalcTextSize(preview_value, NULL, true).x;
    float w = arrow_size + preview_width + (style.FramePadding.x * 2.0f);
    return w;
}

void PushStyleCombobox(const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(color.x, color.y, color.z, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
}

void PushStyleCombobox(Colors color) {
    PushStyleCombobox(ColorValues.at(color));
}

void PopStyleCombobox() {
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(9);
}

void PushStyleTabs(const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(color.x, color.y, color.z, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
}

void PushStyleTabs(Colors color) {
    PushStyleTabs(ColorValues.at(color));
}

void PopStyleTabs() {
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
}

void PushStyleSlider(Colors color_) {
    const ImVec4& color = ColorValues.at(color_);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(1.0, 1.0, 1.0, 0.4f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0, 1.0, 1.0, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
}

void PopStyleSlider() {
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(6);
}

bool SliderInt(const char* label, int32_t* value, const IntSliderOptions& options) {
    bool dirty = false;
    std::string invisibleLabelStr = "##" + std::string(label);
    const char* invisibleLabel = invisibleLabelStr.c_str();
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImGui::BeginDisabled(options.disabled);
    PushStyleSlider(options.color);
    float width = (options.size == ImVec2(0, 0)) ? ImGui::GetContentRegionAvail().x : options.size.x;
    if (options.labelPosition == LabelPositions::Near || options.labelPosition == LabelPositions::Far) {
        width = width - (ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x);
    }
    ImGui::AlignTextToFramePadding();
    if (options.alignment == ComponentAlignments::Right) {
        ImGui::Text(Translate(label), *value);
        if (options.labelPosition == LabelPositions::Above) {
            ImGui::NewLine();
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - width);
        } else if (options.labelPosition == LabelPositions::Near) {
            ImGui::SameLine();
        } else if (options.labelPosition == LabelPositions::Far || options.labelPosition == LabelPositions::None) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - width);
        }
    } else if (options.alignment == ComponentAlignments::Left) {
        if (options.labelPosition == LabelPositions::Above) {
            ImGui::Text(Translate(label), *value);
        }
    }
    if (options.showButtons) {
        if (Button("-", ButtonOptions{ .color = options.color }.Size(Sizes::Inline)) && *value > options.min) {
            *value -= options.step;
            if (options.clamp) {
                if (*value < options.min) {
                    *value = options.min;
                }
            }
            dirty = true;
        }
        ImGui::SameLine(0, 3.0f);
        ImGui::SetNextItemWidth(width - (ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 2 + 3) * 2);
    } else {
        ImGui::SetNextItemWidth(width);
    }
    if (ImGui::SliderScalar(invisibleLabel, ImGuiDataType_S32, value, &options.min, &options.max, options.format,
                            options.flags)) {
        if (options.clamp) {
            if (*value < options.min) {
                *value = options.min;
            }
            if (*value > options.max)
                *value = options.max;
        }
        dirty = true;
    }
    if (options.showButtons) {
        ImGui::SameLine(0, 3.0f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (Button("+", ButtonOptions{ .color = options.color }.Size(Sizes::Inline)) && *value < options.max) {
            *value += options.step;
            if (options.clamp) {
                if (*value > options.max)
                    *value = options.max;
            }
            dirty = true;
        }
    }

    if (options.alignment == ComponentAlignments::Left) {
        if (options.labelPosition == LabelPositions::Near) {
            ImGui::SameLine();
            ImGui::Text(Translate(label), *value);
        } else if (options.labelPosition == LabelPositions::Far || options.labelPosition == LabelPositions::None) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x +
                            ImGui::GetStyle().ItemSpacing.x);
            ImGui::Text(Translate(label), *value);
        }
    }
    PopStyleSlider();
    ImGui::EndDisabled();
    ImGui::EndGroup();
    if (options.disabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        !Ship_IsCStringEmpty(options.disabledTooltip)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.disabledTooltip)).c_str());
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !Ship_IsCStringEmpty(options.tooltip)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.tooltip)).c_str());
    }
    ImGui::PopID();
    return dirty;
}

bool CVarSliderInt(const char* label, const char* cvarName, const IntSliderOptions& options) {
    bool dirty = false;
    int32_t value = CVarGetInteger(cvarName, options.defaultValue);
    if (SliderInt(label, &value, options)) {
        CVarSetInteger(cvarName, value);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        ShipInit::Init(cvarName);
        dirty = true;
    }
    return dirty;
}

void ClampFloat(float* value, float min, float max, float step) {
    int factor = 1;
    if (step < 1.0f) {
        factor *= 10;
    }
    if (step < 0.1f) {
        factor *= 10;
    }
    if (step < 0.01f) {
        factor *= 10;
    }
    if (step < 0.001f) {
        factor *= 10;
    }
    if (step < 0.0001f) {
        factor *= 10;
    }
    if (step < 0.00001f) {
        factor *= 10;
    }
    if (*value < min) {
        *value = min;
    } else if (*value > max) {
        *value = max;
    } else {
        int trunc = (int)std::round(*value * factor);
        *value = (float)trunc / factor;
    }
}

bool SliderFloat(const char* label, float* value, const FloatSliderOptions& options) {
    bool dirty = false;
    std::string invisibleLabelStr = "##" + std::string(label);
    const char* invisibleLabel = invisibleLabelStr.c_str();
    float valueToDisplay = options.isPercentage ? *value * 100.0f : *value;
    float maxToDisplay = options.isPercentage ? options.max * 100.0f : options.max;
    float minToDisplay = options.isPercentage ? options.min * 100.0f : options.min;
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImGui::BeginDisabled(options.disabled);
    PushStyleSlider(options.color);
    float labelSpacing = ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x;
    float width = (options.size == ImVec2(0, 0)) ? ImGui::GetContentRegionAvail().x : options.size.x;
    if (options.labelPosition == LabelPositions::Near || options.labelPosition == LabelPositions::Far) {
        width = width - (ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x);
    }
    ImGui::AlignTextToFramePadding();
    if (options.alignment == ComponentAlignments::Right) {
        ImGui::Text(Translate(label), *value);
        if (options.labelPosition == LabelPositions::Above) {
            ImGui::NewLine();
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - width);
        } else if (options.labelPosition == LabelPositions::Near) {
            width -= labelSpacing;
            ImGui::SameLine();
        } else if (options.labelPosition == LabelPositions::Far || options.labelPosition == LabelPositions::None) {
            width -= labelSpacing;
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - width);
        }
    } else if (options.alignment == ComponentAlignments::Left) {
        if (options.labelPosition == LabelPositions::Above) {
            ImGui::Text(Translate(label), *value);
        }
    }
    if (options.showButtons) {
        if (Button("-", ButtonOptions{ .color = options.color }.Size(Sizes::Inline)) && *value > options.min) {
            *value -= options.step;
            if (options.clamp) {
                ClampFloat(value, options.min, options.max, options.step);
            }
            dirty = true;
        }
        ImGui::SameLine(0, 3.0f);
        ImGui::SetNextItemWidth(width - (ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 2 + 3) * 2);
    } else {
        ImGui::SetNextItemWidth(width);
    }
    if (ImGui::SliderScalar(invisibleLabel, ImGuiDataType_Float, &valueToDisplay, &minToDisplay, &maxToDisplay,
                            options.format, options.flags)) {
        *value = options.isPercentage ? valueToDisplay / 100.0f : valueToDisplay;
        if (options.clamp) {
            ClampFloat(value, options.min, options.max, options.step);
        }
        dirty = true;
    }
    if (options.showButtons) {
        ImGui::SameLine(0, 3.0f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (Button("+", ButtonOptions{ .color = options.color }.Size(Sizes::Inline)) && *value < options.max) {
            *value += options.step;
            if (options.clamp) {
                ClampFloat(value, options.min, options.max, options.step);
            }
            dirty = true;
        }
    }

    if (options.alignment == ComponentAlignments::Left) {
        if (options.labelPosition == LabelPositions::Near) {
            ImGui::SameLine();
            ImGui::Text(Translate(label), *value);
        } else if (options.labelPosition == LabelPositions::Far || options.labelPosition == LabelPositions::None) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - labelSpacing);
            ImGui::Text(Translate(label), *value);
        }
    }
    PopStyleSlider();
    ImGui::EndDisabled();
    ImGui::EndGroup();
    if (options.disabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        !Ship_IsCStringEmpty(options.disabledTooltip)) {
        ImGui::SetTooltip("%s", WrappedText(options.disabledTooltip).c_str());
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !Ship_IsCStringEmpty(options.tooltip)) {
        ImGui::SetTooltip("%s", WrappedText(options.tooltip).c_str());
    }
    ImGui::PopID();
    return dirty;
}

bool CVarSliderFloat(const char* label, const char* cvarName, const FloatSliderOptions& options) {
    bool dirty = false;
    float value = CVarGetFloat(cvarName, options.defaultValue);
    if (SliderFloat(label, &value, options)) {
        CVarSetFloat(cvarName, value);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        ShipInit::Init(cvarName);
        dirty = true;
    }
    return dirty;
}

int InputTextResizeCallback(ImGuiInputTextCallbackData* data) {
    std::string* value = (std::string*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        value->resize(data->BufTextLen);
        data->Buf = (char*)value->c_str();
    }
    return 0;
}

bool InputString(const char* label, std::string* value, const InputOptions& options) {
    bool dirty = false;
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImGui::BeginDisabled(options.disabled);
    PushStyleInput(options.color);
    if (options.hasError) {
        ImGui::PushStyleColor(ImGuiCol_Border, ColorValues.at(Colors::Red));
    }
    float width = (options.size == ImVec2(0, 0)) ? ImGui::GetContentRegionAvail().x : options.size.x;
    if (options.alignment == ComponentAlignments::Left) {
        if (options.labelPosition == LabelPositions::Above) {
            ImGui::Text(Translate(label), *value->c_str());
        }
    } else if (options.alignment == ComponentAlignments::Right) {
        if (options.labelPosition == LabelPositions::Above) {
            ImGui::NewLine();
            ImGui::SameLine(width - ImGui::CalcTextSize(Translate(label)).x);
            ImGui::Text(Translate(label), *value->c_str());
        }
    }
    ImGui::SetNextItemWidth(width);
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
    if (options.secret) {
        flags |= ImGuiInputTextFlags_Password;
    }
    flags |= options.addedFlags;
    if (ImGui::InputText(Translate(label), (char*)value->c_str(), value->capacity() + 1, flags, InputTextResizeCallback, value)) {
        dirty = true;
    }
    if (value->empty() && !options.placeholder.empty()) {
        ImGui::SameLine(17.0f);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.4f), "%s", Translate(options.placeholder.c_str()));
    }
    if (options.hasError) {
        ImGui::PopStyleColor();
    }
    PopStyleInput();
    ImGui::EndDisabled();
    ImGui::EndGroup();
    if (options.hasError && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        !Ship_IsCStringEmpty(options.errorText)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.errorText)).c_str());
    } else if (options.disabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
               !Ship_IsCStringEmpty(options.disabledTooltip)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.disabledTooltip)).c_str());
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !Ship_IsCStringEmpty(options.tooltip)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.tooltip)).c_str());
    }
    ImGui::PopID();
    return dirty;
}

bool CVarInputString(const char* label, const char* cvarName, const InputOptions& options) {
    bool dirty = false;
    std::string value = CVarGetString(cvarName, options.defaultValue.c_str());
    if (InputString(label, &value, options)) {
        CVarSetString(cvarName, value.c_str());
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        ShipInit::Init(cvarName);
        dirty = true;
    }
    return dirty;
}

bool InputInt(const char* label, int32_t* value, const InputOptions& options) {
    bool dirty = false;
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImGui::BeginDisabled(options.disabled);
    PushStyleInput(options.color);
    float width = (options.size == ImVec2(0, 0)) ? ImGui::GetContentRegionAvail().x : options.size.x;
    if (options.alignment == ComponentAlignments::Left) {
        if (options.labelPosition == LabelPositions::Above) {
            ImGui::Text(Translate(label), *value);
        }
    } else if (options.alignment == ComponentAlignments::Right) {
        if (options.labelPosition == LabelPositions::Above) {
            ImGui::NewLine();
            ImGui::SameLine(width - ImGui::CalcTextSize(Translate(label)).x);
            ImGui::Text(Translate(label), *value);
        }
    }
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputScalar(Translate(label), ImGuiDataType_S32, value, nullptr, nullptr, nullptr, options.addedFlags)) {
        dirty = true;
    }
    if ((ImGui::GetItemStatusFlags() & ImGuiItemStatusFlags_Edited) && !options.placeholder.empty()) {
        ImGui::SameLine(17.0f);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.4f), "%s", Translate(options.placeholder.c_str()));
    }
    PopStyleInput();
    ImGui::EndDisabled();
    ImGui::EndGroup();
    if (options.disabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        !Ship_IsCStringEmpty(options.disabledTooltip)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.disabledTooltip)).c_str());
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !Ship_IsCStringEmpty(options.tooltip)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.tooltip)).c_str());
    }
    ImGui::PopID();
    return dirty;
}

bool CVarInputInt(const char* label, const char* cvarName, const InputOptions& options) {
    bool dirty = false;
    int32_t defaultValue = std::stoi(options.defaultValue);
    int32_t value = CVarGetInteger(cvarName, defaultValue);
    if (InputInt(label, &value, options)) {
        CVarSetInteger(cvarName, value);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        ShipInit::Init(cvarName);
        dirty = true;
    }
    return dirty;
}

bool CVarColorPicker(const char* label, const char* cvarName, Color_RGBA8 defaultColor, bool hasAlpha,
                     uint8_t modifiers, UIWidgets::Colors themeColor) {
    std::string valueCVar = std::string(cvarName) + ".Value";
    std::string rainbowCVar = std::string(cvarName) + ".Rainbow";
    std::string lockedCVar = std::string(cvarName) + ".Locked";
    Color_RGBA8 color = CVarGetColor(valueCVar.c_str(), defaultColor);
    ImVec4 colorVec = ImVec4(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
    bool changed = false;
    bool showReset = modifiers & ColorPickerResetButton;
    bool showRandom = modifiers & ColorPickerRandomButton;
    bool showRainbow = modifiers & ColorPickerRainbowCheck;
    bool showLock = modifiers & ColorPickerLockCheck;
    bool locked = CVarGetInteger(lockedCVar.c_str(), 0);
    ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs;
    ImGui::BeginDisabled(locked);
    PushStyleCombobox(UIWidgets::Colors::DarkGray);
    if (hasAlpha) {
        changed = ImGui::ColorEdit4(label, (float*)&colorVec,
                                    flags | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview);
    } else {
        changed = ImGui::ColorEdit3(label, (float*)&colorVec, flags | ImGuiColorEditFlags_NoAlpha);
    }
    PopStyleCombobox();
    ImGui::AlignTextToFramePadding();
    if (showReset) {
        ImGui::SameLine();
        std::string uniqueTag = "Reset##" + std::string(label);
        if (UIWidgets::Button(uniqueTag.c_str(),
                              UIWidgets::ButtonOptions({ { .tooltip = "Resets this color to its default value" } })
                                  .Color(themeColor)
                                  .Size(UIWidgets::Sizes::Inline))) {
            // TODO: Remove for next minor or major version, temporary fix for already migrated configs to 3 for 9.0.0
            CVarClear((std::string(cvarName) + ".R").c_str());
            CVarClear((std::string(cvarName) + ".G").c_str());
            CVarClear((std::string(cvarName) + ".B").c_str());
            CVarClear((std::string(cvarName) + ".A").c_str());
            CVarClear((std::string(cvarName) + ".Type").c_str());
            CVarClearBlock(valueCVar.c_str());
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
    if (showRandom) {
        ImGui::SameLine();
        std::string uniqueTag = "Random##" + std::string(label);
        if (UIWidgets::Button(uniqueTag.c_str(),
                              UIWidgets::ButtonOptions({ { .tooltip = "Generates a random color value to use" } })
                                  .Color(themeColor)
                                  .Size(UIWidgets::Sizes::Inline))) {
            colorVec = GetRandomValue();
            color.r = fmin(fmax(colorVec.x * 255, 0), 255);
            color.g = fmin(fmax(colorVec.y * 255, 0), 255);
            color.b = fmin(fmax(colorVec.z * 255, 0), 255);
            CVarSetColor(valueCVar.c_str(), color);
            CVarSetInteger(rainbowCVar.c_str(), 0); // On click disable rainbow mode.
            ShipInit::Init(rainbowCVar.c_str());
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
    if (showRainbow) {
        ImGui::SameLine();
        std::string uniqueTag = "Rainbow##" + std::string(cvarName) + "Rainbow";

        UIWidgets::CVarCheckbox(
            uniqueTag.c_str(), rainbowCVar.c_str(),
            UIWidgets::CheckboxOptions(
                { { .tooltip = "Cycles through colors on a timer\nOverwrites previously chosen color" } })
                .Color(themeColor));
    }
    ImGui::EndDisabled();
    if (showLock) {
        ImGui::SameLine();
        std::string uniqueTag = "Lock##" + std::string(cvarName) + "Locked";

        UIWidgets::CVarCheckbox(
            uniqueTag.c_str(), lockedCVar.c_str(),
            UIWidgets::CheckboxOptions({ { .tooltip = "Prevents this color from being changed" } }).Color(themeColor));
    }
    if (changed) {
        color.r = (uint8_t)(colorVec.x * 255.0f);
        color.g = (uint8_t)(colorVec.y * 255.0f);
        color.b = (uint8_t)(colorVec.z * 255.0f);
        color.a = (uint8_t)(colorVec.w * 255.0f);
        CVarSetColor(valueCVar.c_str(), color);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        ShipInit::Init(valueCVar.c_str());
        changed = true;
    }

    return changed;
}

bool RadioButton(const char* label, bool active, const RadioButtonsOptions& options) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const char* translatedLabel = Translate(label);
    const ImGuiID id = window->GetID(translatedLabel);
    const ImVec2 label_size = ImGui::CalcTextSize(translatedLabel, NULL, true);

    const float square_sz = ImGui::GetFrameHeight();
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect check_bb(pos, pos + ImVec2(square_sz, square_sz));
    const ImRect total_bb(
        pos, pos + ImVec2(square_sz + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f),
                          label_size.y + style.FramePadding.y * 2.0f));
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id))
        return false;

    ImVec2 center = check_bb.GetCenter();
    center.x = IM_ROUND(center.x);
    center.y = IM_ROUND(center.y);
    const float radius = (square_sz - 1.0f) * 0.5f;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed)
        ImGui::MarkItemEdited(id);

    ImGui::RenderNavCursor(total_bb, id);
    const int num_segment = window->DrawList->_CalcCircleAutoSegmentCount(radius);
    window->DrawList->AddCircleFilled(center, radius,
                                      ImGui::GetColorU32((held && hovered) ? ImGuiCol_FrameBgActive
                                                         : hovered         ? ImGuiCol_FrameBgHovered
                                                                           : ImGuiCol_FrameBg),
                                      num_segment);
    if (active) {
        const float pad = ImMax(1.0f, IM_TRUNC(square_sz / 6.0f));
        window->DrawList->AddCircleFilled(center, radius - pad, ImGui::GetColorU32(ImGuiCol_CheckMark));
    }

    if (style.FrameBorderSize > 0.0f) {
        window->DrawList->AddCircle(center + ImVec2(1, 1), radius, ImGui::GetColorU32(ImGuiCol_BorderShadow),
                                    num_segment, style.FrameBorderSize);
        window->DrawList->AddCircle(center, radius, ImGui::GetColorU32(ImGuiCol_Border), num_segment,
                                    style.FrameBorderSize);
    }

    ImVec2 label_pos = ImVec2(check_bb.Max.x + style.ItemInnerSpacing.x, check_bb.Min.y + style.FramePadding.y);
    if (g.LogEnabled)
        ImGui::LogRenderedText(&label_pos, active ? "(x)" : "( )");
    if (label_size.x > 0.0f)
        RenderText(label_pos, translatedLabel, ImGui::FindRenderedTextEnd(translatedLabel), true);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
    return pressed;
}

bool CVarRadioButton(const char* text, const char* cvarName, int32_t id, const RadioButtonsOptions& options) {
    std::string make_invisible = "##" + std::string(text) + std::string(cvarName);

    bool ret = false;
    int val = CVarGetInteger(cvarName, options.defaultIndex);
    PushStyleCheckbox(options.color);
    if (ImGui::RadioButton(make_invisible.c_str(), id == val)) {
        CVarSetInteger(cvarName, id);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        ret = true;
    }
    ImGui::SameLine();
    ImGui::Text("%s", Translate(text));
    PopStyleCheckbox();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !Ship_IsCStringEmpty(options.tooltip)) {
        ImGui::SetTooltip("%s", WrappedText(Translate(options.tooltip)).c_str());
    }

    return ret;
}

void DrawFlagArray32(const std::string& name, uint32_t& flags, Colors color) {
    ImGui::PushID(name.c_str());
    for (int32_t flagIndex = 0; flagIndex < 32; flagIndex++) {
        if ((flagIndex % 8) != 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(flagIndex);
        uint32_t bitMask = 1 << flagIndex;
        bool flag = (flags & bitMask) != 0;
        PushStyleCheckbox(color);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
        std::string id = fmt::format("##{}{}", name, flagIndex);
        if (ImGui::Checkbox(id.c_str(), &flag)) {
            if (flag) {
                flags |= bitMask;
            } else {
                flags &= ~bitMask;
            }
        }
        ImGui::PopStyleVar();
        PopStyleCheckbox();
        ImGui::PopID();
    }
    ImGui::PopID();
}

void DrawFlagArray16(const std::string& name, uint16_t& flags, Colors color) {
    ImGui::PushID(name.c_str());
    for (int16_t flagIndex = 0; flagIndex < 16; flagIndex++) {
        if ((flagIndex % 8) != 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(flagIndex);
        uint16_t bitMask = 1 << flagIndex;
        bool flag = (flags & bitMask) != 0;
        PushStyleCheckbox(color);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
        std::string id = fmt::format("##{}{}", name, flagIndex);
        if (ImGui::Checkbox(id.c_str(), &flag)) {
            if (flag) {
                flags |= bitMask;
            } else {
                flags &= ~bitMask;
            }
        }
        ImGui::PopStyleVar();
        PopStyleCheckbox();
        ImGui::PopID();
    }
    ImGui::PopID();
}

void DrawFlagArray8(const std::string& name, uint8_t& flags, Colors color) {
    ImGui::PushID(name.c_str());
    for (int8_t flagIndex = 0; flagIndex < 8; flagIndex++) {
        if ((flagIndex % 8) != 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(flagIndex);
        uint8_t bitMask = 1 << flagIndex;
        bool flag = (flags & bitMask) != 0;
        PushStyleCheckbox(color);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
        std::string id = fmt::format("##{}{}", name, flagIndex);
        if (ImGui::Checkbox(id.c_str(), &flag)) {
            if (flag) {
                flags |= bitMask;
            } else {
                flags &= ~bitMask;
            }
        }
        ImGui::PopStyleVar();
        PopStyleCheckbox();
        ImGui::PopID();
    }
    ImGui::PopID();
}

void DrawFlagArray8Mask(const std::string& name, uint8_t& flags, Colors color) {
    ImGui::PushID(name.c_str());
    for (int8_t flagIndex = 0; flagIndex < 8; flagIndex++) {
        if ((flagIndex % 8) != 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(flagIndex);
        uint8_t bitMask = 1 << flagIndex;
        bool flag = (flags & bitMask) != 0;
        PushStyleCheckbox(color);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
        std::string id = fmt::format("##{}{}", name, flagIndex);
        if (ImGui::Checkbox(id.c_str(), &flag)) {
            if (flag) {
                flags |= bitMask;
            } else {
                flags &= ~bitMask;
            }
        }
        ImGui::PopStyleVar();
        PopStyleCheckbox();
        ImGui::PopID();
    }
    ImGui::PopID();
}
} // namespace UIWidgets

ImVec4 GetRandomValue() {
#if !defined(__SWITCH__) && !defined(__WIIU__)
    std::random_device rd;
    std::mt19937 rng(rd());
#else
    size_t seed = std::hash<std::string>{}(std::to_string(rand()));
    std::mt19937_64 rng(seed);
#endif
    std::uniform_int_distribution<int> dist(0, 255 - 1);

    ImVec4 NewColor;
    NewColor.x = (float)(dist(rng)) / 255.0f;
    NewColor.y = (float)(dist(rng)) / 255.0f;
    NewColor.z = (float)(dist(rng)) / 255.0f;
    return NewColor;
}

Color_RGBA8 RGBA8FromVec(ImVec4 vec) {
    Color_RGBA8 color = { vec.x * 255, vec.y * 255, vec.z * 255, vec.w * 255 };
    return color;
}

ImVec4 VecFromRGBA8(Color_RGBA8 color) {
    ImVec4 vec = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
    return vec;
}
