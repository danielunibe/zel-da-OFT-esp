# G02 — Performance Notes

- The resolver performs no filesystem access, texture decoding, or resource reload. It returns a stable OTR path for the LT asset.
- The call occurs in the existing font character-load path while a message is decoded, not in the per-frame HUD draw loop.
- No persistent cache or per-frame allocation was introduced. The current pilot queries the existing controller APIs when the Z character is loaded; a future broader glyph system may memoize a resolved descriptor after runtime behavior is confirmed.
- The connected-device name check and mapping walk are bounded by the existing Player 1 controller/mapping collections.
- The asset is 16x16 P3 text, included during OTR generation. Runtime decode/load performance was not measured in this run.
- No gameplay, controller mapping, simulation, save, audio, localization, HUD, or Ocarina performance path was changed.
