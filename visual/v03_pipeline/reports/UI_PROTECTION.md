# UI Protection — V03

## Architecture
- UI (HUD, hearts, rupees, minimap, pause, inventory, message boxes, font glyphs, controller glyphs, title cards, file select) is rendered via ImGui
- ImGui uses its own draw path (separate from Fast3D interpreter)
- ACES tonemapping runs AFTER interpreter::EndFrame() → before Present()
- ACES pass copies the FULL backbuffer (scene + UI composited) and processes it uniformly

## Protection Analysis
- ACES tonemapping affects ALL pixels in the backbuffer equally
- UI is already composited onto the backbuffer by the time ACES runs
- ACES is a contrast/tone curve — it will affect UI brightness slightly
- Atmospheric fog reads depth buffer — UI has no depth (depth buffer is cleared for UI)
- Fog factor = 0 for UI pixels (depth = 0 at UI plane) → no fog on UI

## Assessment
- UI is SAFE from atmospheric fog (depth-based, UI has no depth)
- UI IS affected by ACES tonemapping (applied uniformly)
- ACES is a mild contrast curve — UI visibility should not be significantly impacted
- HUMAN VISUAL VALIDATION REQUIRED for UI brightness in Enhanced mode
