# Pilot recommendation

## Recommended pilot

One message prompt for the Z action, using the existing message renderer and an optional dynamic token. Use the Xbox family asset only when the active port resolves an Xbox-compatible mapping; otherwise render the classic N64 Z glyph.

## Why this pilot

It exercises semantic action lookup, a physical button/trigger distinction, message layout, Spanish compatibility, fallback, and rebinding without modifying HUD geometry or Ocarina note semantics. It is small enough to revert and broad enough to expose the core architectural risks.

## Not in pilot

No global 137-message rewrite, no HUD-wide replacement, no Ocarina note replacement, no camera changes, no controller mapping changes, and no live-install changes.

