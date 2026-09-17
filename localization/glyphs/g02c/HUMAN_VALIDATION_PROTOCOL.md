# G02C human validation protocol

## Guardrails

Use only `runtime\build-test\soh.exe` through `RUN_G02C_VALIDATION.bat`. Do not launch the live installation. Do not edit localization, source, mappings, saves, or other workspaces. Avoid saving game state.

## Exact sequence

1. Connect the Xbox Series X Controller and report `LISTO`.
2. Launch the canonical G02C launcher and report whether the build-test game opens normally.
3. Press A, B, move the left stick, and open/close Menu. Report whether normal input works.
4. Reach the existing Kokiri tutorial prompt `0x100D` through ordinary gameplay. Do not use a new injector or alter progression. Report when the message is open.
5. Set the G02 glyph profile to `CLASSIC` using the existing G02 mechanism. In the same `0x100D` message, visually check that the inline glyph is the original N64 Z and not LT.
6. Capture `screenshots\classic_z_3440x1440.png` manually. Report the file is saved.
7. Check Classic layout: visible, not clipped/stretched/overlapping, readable at couch distance, and no crash.
8. Set the profile to `DYNAMIC`, keep the Xbox controller active, and reopen the same `0x100D` message.
9. Confirm explicitly whether LT is visible instead of N64 Z. Capture `screenshots\dynamic_xbox_lt_3440x1440.png` manually and report it saved.
10. Check LT orientation, clipping, stretching, overlap, size, readability, stutter, flicker, and message progression.
11. In relevant gameplay, press LT and confirm it still performs N64 Z behavior.
12. Close the game without saving if possible. The coordinator hashes build-test saves after the session.

The coordinator must stop after each human-required action and request only the next single action. No visual result may be inferred from launcher output or source inspection.

## Static carry-forward

Unknown-device fallback and unsupported-binding fallback may be recorded `PASS_STATIC` from G02 evidence. Hot rebind is optional and should be `NOT_TESTED` unless it is safely available.
