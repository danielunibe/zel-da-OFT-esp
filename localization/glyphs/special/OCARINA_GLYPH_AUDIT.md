# Ocarina glyph audit

## Findings

Ocarina handling is embedded in `z_message_PAL.c`, with `Message_HandleOcarina`, `Message_UpdateOcarinaGame`, note state reset, staff display/playback, and explicit colors for A, C and directional notes. The note presentation is therefore a specialized renderer/state machine, not only a generic text prompt.

The current implementation can support a semantic mapping layer conceptually, but replacing the visual note vocabulary with Xbox glyphs would change recognition and must be separately validated. A pilot should keep the existing colored note glyphs and test only an adjacent action prompt, not Ocarina note rendering.

## Recommendation

Do not modify Ocarina in G01/G02. First expose a read-only semantic resolver, verify that `A/B/C/L/R/Z` remain stable under rebinding, then design a compatibility mode where physical glyphs are opt-in and the classic note palette remains the default.

