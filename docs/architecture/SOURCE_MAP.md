# Source map — Shipwright 9.1.1

| Concern | Primary source area | Task 02 finding |
|---|---|---|
| SDL device discovery | `libultraship/src/ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.cpp` | Existing |
| Logical controller | `libultraship/src/ship/controller/controldevice/controller/Controller.cpp` | Existing |
| Stick normalization | `libultraship/src/ship/controller/controldevice/controller/ControllerStick.cpp` | Existing deadzone/sensitivity; no hysteresis |
| SDL axis mappings | `libultraship/src/ship/controller/controldevice/controller/mapping/sdl/` | Existing |
| Mapping UI | `soh/soh/Enhancements/controls/SohInputEditorWindow.cpp` | Existing |
| Rumble bridge | `soh/soh/OTRGlobals.cpp`, `libultraship/src/ship/controller/controldevice/controller/mapping/sdl/SDLRumbleMapping.cpp` | Body rumble existing; trigger rumble not found |
| N64 rumble request | `soh/src/code/padmgr.c`, `soh/src/code/code_800A9F30.c` | Legacy on/off bridge |
| Message state machine | `soh/src/code/z_message_PAL.c`, `soh/src/code/z_play.c` | Existing static glyph/message path |
| Message glyph table | `soh/src/code/z_kanfont.c` | Fixed A/B/C/L/R/Z/control glyphs |
| Button icon assets | `soh/assets/textures/icon_item_static/icon_item_static.h` | Existing static display lists |
| Custom text | `soh/soh/Enhancements/custom-message/`, `soh/soh/OTRGlobals.cpp` | ENG/GER/FRA branches in this tag |
| Display timing | `soh/soh/OTRGlobals.cpp`, `soh/soh/frame_interpolation.cpp` | Interpolation target, not simulation rate |
| Window backend | `libultraship/src/fast/`, `libultraship/include/ship/window/Window.h` | Refresh/VSync/MSAA abstraction |
| Save persistence | `soh/soh/SaveManager.cpp`, `soh/soh/Enhancements/Autosave.cpp` | Temp-file path; Windows replacement risk needs later test |
| Mods | `soh/soh/OTRGlobals.cpp`, mod enhancement sources, `docs/MODDING.md` | Existing mod folder/OTR path |
| Build | `docs/BUILDING.md`, `CMakeLists.txt`, `libultraship/cmake/` | VS 2022/v143/x64 documented |

## Call-flow anchors

The three future features with the highest source-change risk are dynamic glyphs, semantic haptics and voice-over. Their current seams are respectively message draw (`Font_LoadChar`), the rumble callback (`OTRControllerCallback`) and message state transitions (`Message_StartTextbox` / `Message_DrawText`).

