# Spanish Integration Staging & Font Encoding Package (L03A)

This directory contains the staged, verified, and packaged Latin American Spanish (ES-419) localization for **Ship of Harkinian (Couch Edition)**.

## Directory Layout

- `packages/`: Staged distribution packages for `main_game`, `randomizer`, and `soh_ui` (containing normalized JSONL corpora, encoded binaries, and SHA-256 manifests).
- `font/`: Font slot assignments in `SPANISH_FONT_SLOT_MAP.csv` remapping 11 required Spanish glyphs into European slots `0x80..0x9E` with zero button glyph collisions.
- `architecture/`: Architectural blueprints for `LANGUAGE_ESP`, save compatibility (`global.sav`), UI catalog integration, Randomizer integration, and L03B entry points.
- `qa/`: QA certification matrices including round-trip verification, 100% character coverage, collision matrix, layout preflight, runtime test queue, and regression tracking.
- `reports/`: G02 conflict matrix, SHA-256 hash manifest, and final L03A certification report.
- `tools/`: Fully reproducible Python tools for encoding, decoding, building packages, and executing the validation gate.

## Validation Command

To validate all packages, hashes, character coverage, and codec round-trip:
```bash
python localization_workspace/spanish/integration_staging/tools/validate_spanish_package.py
```
Expected output: `ALL GATES PASSED! Package is 100% verified and reproducible.` (Exit code `0`).
