# Ultrawide cinematics audit

## Evidence

Cutscene data is loaded through `soh/soh/z_scene_otr.cpp` and `soh/src/code/code_800BB0A0.c` contains spline camera evaluation (`CutsceneCameraPoint`, position, roll and FOV). Scene resources also contain cutscene commands. The audit did not find a single global cinematic policy that proves every scene is authored for 21:9.

## Risk areas

- Camera spline FOV and framing may expose extra world space at the sides.
- Letterbox/wipe and framebuffer effects may retain 4:3 assumptions.
- Screen-space subtitles, message boxes and scripted overlays can have independent coordinates.
- Extra visibility can reveal unloaded/culling-sensitive or intentionally hidden content.

## Recommendation

Use global aspect-safe viewport/camera rules only after comparing representative cutscenes. Keep per-cutscene exceptions available for fixed framing, wipes and overlays. Acceptance must include gameplay, first-person, cutscenes, transitions and pause UI at 4:3, 16:9 and 3440x1440. Do not edit cinematic data in this audit.

**Global fixes:** projection/aspect policy and safe-area primitives. **Exception work:** authored FOV/framing, letterbox, overlay and scene-specific camera cases.
