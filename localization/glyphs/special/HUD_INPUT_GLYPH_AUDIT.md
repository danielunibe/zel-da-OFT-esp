# HUD/input glyph audit

`InputViewer.cpp` already has texture resources for A, B, L, R, Z, Start, C directions, analog stick, D-pad, modifiers and right stick, including outline variants. It renders the N64 logical state and is an existing visual vocabulary, not proof of dynamic device-family resolution.

HUD consumers must preserve logical action meaning. Mapping C actions to right-stick arrows is useful for Xbox users, but the asset should visibly communicate both direction and the fact that it is the C action when ambiguity matters. A universal replacement of every C glyph with a right-stick glyph would harm classic compatibility and localization comprehension.

Recommended order: message pilot, then a dedicated Input Viewer preview mode, then HUD action slots. Ocarina remains separate.

