# Glyph implementation roadmap

## G02 — resolver foundation

Implement semantic actions, mapping descriptor, port-scoped lookup, device family classification, cache invalidation and classic fallback. No renderer replacement yet.

## G03 — message pilot

Add one optional dynamic message token and one Z-action message. Validate text width, Spanish strings, mixed classic/dynamic prompts, missing assets and rebinding.

## G04 — SoH/Input Viewer preview

Expose the resolver in the existing ImGui input viewer; verify assets and device family presentation across two controllers.

## G05 — HUD

Migrate one HUD action slot at a time, preserving N64 mode and explicit C-direction semantics.

## G06 — Ocarina decision

Only after visual/usability evidence, decide whether action glyphs may appear around Ocarina. Do not replace note glyphs by default.

## Release gates

Static tests are insufficient. Require message rendering, language/layout, hot rebinding, unplug/replug, multiple ports, controller family fallback, and vanilla compatibility evidence.

