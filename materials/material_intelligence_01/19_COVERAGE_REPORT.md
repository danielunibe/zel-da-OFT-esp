# 19 - Coverage Report

## Headline numbers

| Metric | Value |
|---|---|
| Total discovered visual resources | 24,372 |
| Classified HIGH | 7,541 |
| Classified MEDIUM | 719 |
| Classified LOW | 16,112 |
| UNKNOWN category | 15,536 |
| UI protected | 6,102 |
| Emissive candidates | 358 |
| Water candidates | 237 |
| Metal candidates | 163 |

## Resource-type mix

| Type | Count | Material-relevant? |
|---|---|---|
| TEXTURE | 12,872 | YES - classification target |
| DISPLAY_LIST | 8,304 | Indirect (material via referenced textures) |
| PALETTE | 421 | No (color data for CI textures) |
| GEOMETRY | 74 | No (vertex data) |
| ASSET_REF | 2,629 | No (non-visual declarations) |
| PNG_FILE | 72 | YES (custom replacements) |

## Why LOW + UNKNOWN dominate (honest analysis)

- 8,034 scene records are offset-named by design (`*_room_NTex_00xxxx`) - the source
  headers carry no semantic names for them. This is the correct, honest default.
- 2,629 ASSET_REFs are non-visual declarations (animation, collision, skeletons).
- DLs intentionally stay UNKNOWN: their materials come from the textures they
  reference; classifying DL names would be fabrication.

## Textures-only view

- 12,944 textures/PNGs classified.
- 7,451 HIGH, 376 MEDIUM,
  5,117 LOW.
- 4,537 flagged needs_manual_review.

## Quality guarantees kept

- No resource names invented; everything traces to headers/XML/PNGs on disk.
- LOW confidence never presented as confirmed; UNKNOWN = classic fallback.
- No game/runtime/live-root files modified; scripts + outputs live only in
  visual_workspace/material_intelligence_01.
