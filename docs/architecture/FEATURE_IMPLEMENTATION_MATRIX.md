# Feature implementation matrix

Classification is based on the exact Shipwright 9.1.1 source checkout, not on the existing runtime configuration alone.

| Feature | Config-only | Asset-only | Source change | Current evidence / boundary |
|---|---:|---:|---:|---|
| Xbox-style physical mapping | Yes | No | No, for supported SDL mappings | Existing SDL mapping editor and JSON mapping schema |
| Right stick as C buttons | Yes | No | No | `SDLAxisDirectionToButtonMapping` supports axis-direction-to-button mappings |
| Stick deadzone/sensitivity tuning | Yes | No | No | `ControllerStick` settings and runtime JSON |
| Stick hysteresis / advanced response curve | No | No | Yes | No hysteresis or curve layer found in current path |
| Body rumble | Yes | No | No | Existing `ControllerRumble` + SDL rumble |
| Semantic haptic patterns | No | No | Yes | Current callback collapses to on/off |
| Trigger-specific haptics | No | No | Yes / backend capability work | No trigger-rumble call found |
| Static button glyph reskin | No | Yes | Possibly | Static N64 glyph/icon assets are present |
| Glyphs that follow active bindings | No | No | Yes | Current message path uses fixed glyph table/icons |
| Display refresh matching | Yes | No | No | `MatchRefreshRate` and window refresh API already exist |
| MSAA/VSync tuning | Yes | No | No | Existing settings and window API |
| Spanish text | No | Yes + source likely | Likely | Exact tag exposes ENG/GER/FRA paths; Spanish is not confirmed |
| Pre-generated voice-over | No | Voice files alone insufficient | Yes | Needs message-ID hooks, loader, cancellation and packaging |
| Save safety improvements | No | No | Yes | `SaveManager` uses temp/copy flow; needs dedicated acceptance test/change |
| Mod packaging | Settings/assets | Yes | Maybe | Existing mod folder and OTR resource model |

## Decision rule

The first couch milestone should use the existing SDL mapping and runtime settings, then add asset presentation only after the input contract is stable. Source changes should be isolated behind small seams and individually gated; no feature is considered complete because a JSON key or asset exists.

