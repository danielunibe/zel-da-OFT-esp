# Task 03 configuration diff

Compared byte-for-byte against `runtime/build-test/shipofharkinian.TASK03_BEFORE.json`.

| KEY/PATH | OLD VALUE | NEW VALUE | REASON | SOURCE DOCUMENT | EXPECTED EFFECT | RISK |
|---|---|---|---|---|---|---|
| `CVars.gFreeCamera` | `1` | `0` | Right Stick is reserved for C-buttons; the baseline free-camera enhancement would conflict conceptually. | `docs/architecture/INPUT_ARCHITECTURE.md`, Task 03 contract | Disable Free Camera in build-test only. | Free Camera is unavailable in this profile; source feature remains intact. Legacy key is retained for migration. |
| `CVars.gSettings.Controllers.ButtonMappings.P0-B16-SDLA5-ADP` | SDL axis 5 positive (`RT`) to bitmask 16 (`R`) | Replaced by `P0-B16-SDLB10` | Xbox contract assigns RB to N64 R and reserves RT. | `docs/architecture/INPUT_ARCHITECTURE.md` | RB/SDL button 10 drives N64 R. | SDL controller mapping must expose the Xbox-compatible button index. |
| `CVars.gSettings.Controllers.ButtonMappings.P0-B8-SDLB10` | SDL button 10 to bitmask 8 (`C-Up`) | Removed | RB must not also drive C-Up. | `docs/architecture/INPUT_ARCHITECTURE.md` | C-Up remains Right Stick Up only. | No extra RB-to-C-Up fallback. |
| `CVars.gSettings.Controllers.Port1.Buttons.16ButtonMappingIds` | `P0-B16-KB19,P0-B16-SDLA5-ADP,` | `P0-B16-KB19,P0-B16-SDLB10,` | Keep keyboard fallback while changing Xbox physical binding. | `docs/architecture/INPUT_ARCHITECTURE.md` | R remains available on keyboard and RB. | No material risk beyond controller SDL mapping identity. |
| `CVars.gSettings.Controllers.Port1.Buttons.8ButtonMappingIds` | `P0-B8-KB328,P0-B8-SDLA3-ADN,P0-B8-SDLB10,` | `P0-B8-KB328,P0-B8-SDLA3-ADN,` | Remove the conflicting RB-to-C-Up entry. | `docs/architecture/INPUT_ARCHITECTURE.md` | C-Up is Right Stick Up/keyboard only. | No hysteresis/dominance added; native threshold behavior remains. |
| `CVars.gSettings.MatchRefreshRate` | `0` | `1` | Activate the current setting identified by the display architecture. | `docs/architecture/DISPLAY_AND_FRAMERATE_ARCHITECTURE.md` | Interpolation target follows active monitor refresh. | Does not change simulation timing. |

## Preserved values

- `CVars.gSettings.InterpolationFPS`: `170` preserved as fallback.
- `CVars.gMSAA`: `8` preserved; no arbitrary renderer change.
- Fullscreen baseline: `2560x1440`; not changed to 4K or forced to 3440x1440 because that resolution was not present in the copied baseline config.
- Keyboard mappings and D-pad mappings remain present.
- `Port1.RumbleMappingIds=P0` and low/high body intensities remain `50/50`.
- No ROM, O2R resource or save was changed.

