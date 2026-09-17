# Haptics baseline — Task 03

## BODY_RUMBLE

`YES` at the source/backend level. The exact path is:

`PadMgr_Rumble* -> OTRControllerCallback -> ControllerRumble -> SDLRumbleMapping -> SDL_GameControllerRumble`.

The build-test configuration preserves `Port1.RumbleMappingIds=P0`, `RumbleMappingClass=SDLRumbleMapping`, `LowFrequencyIntensity=50` and `HighFrequencyIntensity=50`.

## TRIGGER_RUMBLE

Unsupported by the current 9.1.1 backend. No trigger-rumble API or XInput trigger path was found. Not implemented in Task 03.

## CURRENT CONFIG

```text
Port1.RumbleMappingIds = P0,
P0.RumbleMappingClass = SDLRumbleMapping
P0.LowFrequencyIntensity = 50
P0.HighFrequencyIntensity = 50
```

## CURRENT CALL PATH

The legacy game callback carries an on/off rumble request. It does not carry semantic event identity or a pattern. SDL receives body low/high intensity and an explicit stop call.

## PRECISION HAPTICS

NOT IMPLEMENTED YET. No HapticEngine, custom patterns, trigger motors or call-site changes were introduced.

