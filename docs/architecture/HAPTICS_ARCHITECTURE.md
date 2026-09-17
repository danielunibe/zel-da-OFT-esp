# Haptics architecture — Shipwright 9.1.1

## Current path

The game-side N64 rumble request is exposed by `PadMgr_Rumble*` in `soh/src/code/padmgr.c` and by gameplay helpers such as `Player_RequestRumble` in `soh/src/overlays/actors/ovl_player_actor/z_player.c`. The bridge is:

```text
game rumble request
  -> PadMgr_RumbleSet / PadMgr_RumbleControl
  -> OTRControllerCallback(uint8_t rumble)
  -> OTRGlobals::OTRControllerCallback
  -> Controller::GetRumble()->StartRumble / StopRumble
  -> SDLRumbleMapping::StartRumble / StopRumble
  -> SDL_GameControllerRumble
```

The legacy callback carries an on/off request rather than a rich event identity. `ControllerRumble` stores configured rumble mappings and `SDLRumbleMapping` sends low/high motor intensity through SDL. The source uses duration `0` for the SDL rumble call and stops explicitly; no trigger-rumble call was found.

## Capability assessment

| Requirement | Current path | Status |
|---|---|---|
| Body rumble | SDL low/high frequency motors | Supported |
| Event-specific pattern | Not represented by the legacy callback | Source change required |
| Independent trigger motors | No `SDL_GameControllerRumbleTriggers` or XInput path found | Not supported by current source |
| Intensity policy | Configured low/high percentages | Supported, but global per mapping |
| Device reconnect | Owned by physical-device manager | Existing infrastructure |

## Recommended future design

Keep a central haptic service above `ControllerRumble` that accepts semantic events (`damage`, `impact`, `item`, `menu`, `low-health`) and resolves them into short patterns. The service should mix or prioritize events, apply a global intensity scalar, cancel on disconnect, and fall back to body rumble when trigger motors are unavailable. This is a source-level change because the current game callback loses event identity before it reaches SDL.

Do not promise trigger haptics for the first couch edition. The honest first target is body low/high rumble through the existing SDL mapping, with the semantic service deferred until there is a stable event API and device capability query.

## Relevant files

- `soh/src/code/padmgr.c`
- `soh/src/code/code_800A9F30.c`
- `soh/src/overlays/actors/ovl_player_actor/z_player.c`
- `soh/soh/OTRGlobals.cpp`
- `libultraship/src/ship/controller/controldevice/controller/ControllerRumble.cpp`
- `libultraship/src/ship/controller/controldevice/controller/mapping/sdl/SDLRumbleMapping.cpp`

