# Task 02 — Development workspace and exact source baseline

Date: 2026-09-15

## Result

`PASS` for workspace separation, runtime-copy integrity, exact upstream source checkout, branch creation, submodule initialization, source ignore protection and architecture reconnaissance.

`BLOCKED_EXTERNAL` for a Windows build gate: the current shell does not expose MSVC `cl.exe` or an activated Visual Studio Developer environment. No build was attempted.

## Workspace

- DEV_ROOT: `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV`
- Runtime copy: `runtime\working`, copied exclusively from Baseline 00 `PROJECT_SNAPSHOT`
- Source: `source\shipwright`
- Documentation: `docs\architecture`, `docs\research`, `docs\decisions`
- Reports: `reports`
- Live `PROJECT_ROOT` was not modified.

## Source identity

- Repository: `https://github.com/HarbourMasters/Shipwright.git`
- Tag: `9.1.1`
- Commit: `4aaad850bd5540cd77c2d83f3ad348d3b38605b2`
- Local branch: `couch-edition`
- Match confidence: `EXACT`
- Functional source changes: none
- Administrative source change: root `.gitignore` couch-private exclusions

## Runtime preservation

All eight critical hashes in `TASK02_HASHES.csv` match between the baseline snapshot and `runtime\working`: executable, O2R resources, ROM, configuration and three save files. The runtime copy includes `.kilo\.gitignore`; it remains runtime/editor metadata and was not integrated into source.

## Architecture conclusions

Existing SDL mappings can express the requested left-stick, right-stick-to-C-button, face-button, shoulder, trigger and menu contract. Body rumble exists through SDL low/high motors. Static glyphs exist, but dynamic prompts that follow active bindings need source changes. Refresh matching, VSync and MSAA already have settings and window abstractions. The exact 9.1.1 source exposes English, German and French language branches; Spanish and pre-generated voice-over are not confirmed by this source and are therefore not marked config-only.

## Prohibited actions respected

No `soh.exe`, `START_GAME.exe` or project scripts were run. No ROM, save or live runtime file was edited. No build, packaging, installer, signing or release operation was attempted.

The official source does contain its own tracked randomizer implementation under `soh/soh/Enhancements/randomizer`; that is source code, not a copied user randomizer output. No user `rom.n64`, save file or `.o2r` artifact is tracked in the source checkout.
