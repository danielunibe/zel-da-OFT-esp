# Xbox Controller Profile V1 — Couch Edition

## Frozen contract

| Xbox control | N64 target | Actual config path |
|---|---|---|
| Left Stick | N64 Control Stick | `Port1.LeftStick`, existing SDL axes 0/1 |
| A | A | existing `P0-B32768-SDLB0` |
| B | B | existing `P0-B16384-SDLB1` |
| LT | Z | existing `P0-B8192-SDLA4-ADP` |
| LB | L | existing `P0-B32-SDLB9` |
| RB | R | new `P0-B16-SDLB10` |
| Menu/Start | Start | existing `P0-B4096-SDLB6` |
| Right Stick Up | C-Up | existing `P0-B8-SDLA3-ADN` |
| Right Stick Down | C-Down | existing `P0-B4-SDLA3-ADP` |
| Right Stick Left | C-Left | existing `P0-B2-SDLA2-ADN` |
| Right Stick Right | C-Right | existing `P0-B1-SDLA2-ADP` |

## Reserved controls

X, Y, RT, View, L3, R3 and D-pad are not assigned new Couch Edition functions. Existing keyboard/D-pad mappings were preserved rather than removed for cosmetic cleanup.

## Applied settings

- Left-stick deadzone: 20% (baseline native value).
- Left/right-stick sensitivity: 100% (baseline native value).
- Right-stick axis button behavior: native `SDLAxisDirectionToButtonMapping`; no invented hysteresis or dominance fields.
- Right-stick threshold: native configured/default behavior; the source path documents a 25% default threshold and no release hysteresis.
- `gSettings.MatchRefreshRate`: enabled.
- Free Camera: disabled only in `runtime/build-test` because it conflicts with the profile's Right Stick ownership.
- Body rumble: existing P0 SDL mapping preserved at 50% low/high intensity.

## Limitations

The profile depends on SDL's logical controller mapping and therefore must be tested with the actual Xbox controller. Axis/button indices are not a universal guarantee for arbitrary gamepads. Hardware validation was not available in Task 03; all individual physical-control checks remain `HARDWARE_TEST_REQUIRED`.

Dynamic Xbox glyphs, trigger rumble, modifier layers, hysteresis/axis dominance and gameplay changes are explicitly deferred.

