# G02 — Dynamic Xbox Glyph Pilot: Implementation Summary

Status: PARTIAL — source implementation and reproducible Release build complete; live controller/runtime visual proof is not available in this run.

## Scope

The pilot resolves the Player 1 N64 `Z` semantic action to an Xbox `LT` message glyph only when all supported conditions are true. The selected validation message is `0x1036`, an existing tutorial prompt that already contains the Z action icon. No message corpus, localization, HUD, Ocarina, mapping, or controller backend files were changed.

## Implementation

- `soh/include/glyph_resolver.h` exposes the small C ABI used by the C message/font path.
- `soh/soh/Enhancements/glyphs/GlyphResolver.cpp` queries the active Player 1 controller, its `BTN_Z` mappings, the physical mapping type, trigger direction, and connected SDL controller name. Xbox family detection is deliberately explicit (`Xbox` or `XInput`); a generic SDL name is not guessed as Xbox.
- `soh/src/code/z_kanfont.c` keeps the existing `fontTbl[0x84]` N64 Z route as the fallback and substitutes the OTR texture path only for that central Z character path.
- `soh/assets/custom/textures/buttons/LTBtn.ppm` is a new original 16x16 monochrome pilot asset. The OTR generation log included `textures/buttons/LTBtn.ppm`.

`GlyphProfile` is read as an integer CVar: `0` is CLASSIC and returns the original glyph; `1` is DYNAMIC and is the build-test default. Unsupported, unknown, missing, digital, or non-LT bindings return the classic path.

## Build evidence

The prescribed build completed with exit code `0` and produced `source/shipwright/x64/Release/soh.exe`. The build still emitted existing project/toolchain warnings, including MSVC conversion warnings and the existing `/W3` overridden by `/w` warning. The generated OTR step included the new asset.

## Verification boundary

Static source inspection and compilation are complete. Runtime controller enumeration, an actual Xbox/XInput device, the `0x1036` prompt, 3440x1440 screenshots, and save/load smoke were not executed here. Therefore G02 is not presented as a fully runtime-certified PASS.
