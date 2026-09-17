# Glyph quick wins

1. Define the semantic action enum and classic fallback table.
2. Build a read-only resolver over `Controller::GetAllMappings`, button mappings and stick direction mappings.
3. Add explicit `TRIGGER`/`AXIS_DIRECTION` metadata instead of flattening LT to a generic button.
4. Add resolver diagnostics to the SoH input editor without changing mappings.
5. Reuse the existing InputViewer texture registry for a preview-only Xbox family.
6. Add a single message control token behind a feature flag.
7. Test port isolation, unplug/replug, rebinding, and unresolved-device fallback.
8. Keep Ocarina and HUD replacement out of the first pilot.

