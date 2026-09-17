# Glyph and button-prompt architecture — Shipwright 9.1.1

## Existing rendering path

Button glyphs are not currently resolved from the active SDL binding. They are static N64-era message assets and fixed control symbols:

```text
message data / control byte
  -> Message_DrawText / Message_DrawTextJPN
  -> Font_LoadChar via fontTbl in z_kanfont.c
  -> static message texture or icon display list
```

`soh/src/code/z_kanfont.c` contains the message font table. Entries include A, B, C directions, L, R, Z and control-stick/control-pad symbols. `Font_LoadChar` loads the selected entry into the message buffer. `z_message_PAL.c` owns the message text state and control-code interpretation. Item/button display lists are declared in `soh/assets/textures/icon_item_static/icon_item_static.h`, including `gAButtonIconDL`, `gBButtonIconDL`, `gCButtonIconsDL`, `gLButtonIconDL` and `gRButtonIconDL`.

## Consequence for Couch Edition

An asset-only replacement can restyle static A/B/C/L/R/Z glyphs, but it cannot make prompts follow arbitrary Xbox bindings. A dynamic Xbox prompt requires a binding-aware lookup at draw time, plus a glyph family and a stable fallback when a binding is reserved or unavailable.

Classification:

- Static visual reskin: asset/config work, subject to OTR asset extraction and message texture compatibility.
- Prompts that reflect actual active bindings: source change required in the message/glyph path.
- New language-specific prompt variants: asset work plus likely source changes if the language or glyph encoding is not already represented.

## Recommended seam

Introduce a small binding-to-glyph resolver at the message rendering boundary, conceptually between message control-code decoding and `Font_LoadChar`/icon rendering. It should return a logical glyph token (`A`, `B`, `C_UP`, `Z`, `START`, `LEFT_STICK`, etc.) and then select an asset from the active visual theme. The resolver must never read raw SDL axes directly; it should ask the controller mapping layer for the current logical binding.

This seam is not implemented in Task 02. It is recorded to prevent an asset-only change from being mistaken for dynamic binding support.

