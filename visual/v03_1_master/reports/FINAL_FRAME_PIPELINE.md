# FINAL FRAME PIPELINE REPORT — V03.1 MASTER CONSOLIDATION
**Date:** 2026-09-17  
**Project:** Ocarina of Time PC — Couch Edition  
**Owner:** Gemini 3.8 High / High Flash  

---

## 1. Frame Pipeline Architecture Overview

The V03.1 master consolidation establishes a verified, clean separation between 3D world geometry rendering, enhanced scene postprocessing (ACES filmic tonemapping + dynamic atmospheric fog), and 2D UI composition (in-game HUD, dialogue, pause screen, and ImGui overlays).

---

## 2. Exact Execution Order

```
Frame Start
  │
  ├─ 1. Fast3dWindow::DrawAndRunGraphicsCommands
  │      ├─ gui->StartDraw()                        [ImGui frame initialization]
  │      ├─ Interpreter::StartFrame()               [Reads VisualProfile CVar, sets dimensions, resets flags]
  │      │     ├─ mPostprocessRunThisFrame = false
  │      │     ├─ mHasDrawn3DThisFrame = false
  │      │     ├─ mCurrentTextureHash = 0
  │      │     └─ mSceneHasFog = false
  │      │
  │      ├─ 2. Interpreter::Run(commands)           [Executes N64 Display List commands]
  │      │     │
  │      │     ├─ [Phase A: 3D World Geometry]
  │      │     │    ├─ Skybox / VR box rendering
  │      │     │    ├─ Room / terrain geometry (with G_ZBUFFER enabled)
  │      │     │    ├─ Actor models (Link, NPCs, enemies with G_ZBUFFER enabled)
  │      │     │    ├─ Particle & transparent effects (POLY_XLU_DISP)
  │      │     │    ├─ mHasDrawn3DThisFrame marked true on first depth-tested triangle
  │      │     │    └─ Scene fog parameters captured from mRdp->fog_color and mRsp->fog_mul/offset
  │      │     │
  │      │     ├─ [Phase B: 3D Scene Completion & Postprocess Trigger]
  │      │     │    ├─ Detection 1: gfx_noop_handler_f3dex2 intercepts "OVERLAY_DISP 開始"
  │      │     │    ├─ Detection 2: GfxSpTri1 detects transition to (geometry_mode & G_ZBUFFER == 0)
  │      │     │    ├─ Interpreter::Flush() flushes any pending 3D triangles
  │      │     │    └─ Interpreter::TriggerScenePostprocess() executes:
  │      │     │         ├─ If profile == ENHANCED (1):
  │      │     │         │    ├─ Resolves scene fog color and dynamic density
  │      │     │         │    └─ GfxRenderingAPIDX11::RunTonemappingPass(...)
  │      │     │         │         ├─ Copies 3D scene backbuffer to SRV copy texture
  │      │     │         │         ├─ Saves D3D11 state (including DSV, viewport, scissor)
  │      │     │         │         ├─ Unbinds DSV and binds backbuffer RTV
  │      │     │         │         ├─ Binds depth buffer SRV (from mFrameBuffers)
  │      │     │         │         ├─ Uploads FogConstants constant buffer
  │      │     │         │         ├─ Renders fullscreen triangle with ACES Film + Fog HLSL
  │      │     │         │         └─ Restores complete D3D11 state
  │      │     │         └─ If profile == CLASSIC (0):
  │      │     │              └─ Postprocess is completely bypassed (0 cost, 0 effect)
  │      │     │
  │      │     └─ [Phase C: In-Game 2D UI Rendering]
  │      │          ├─ OVERLAY_DISP display list executes
  │      │          ├─ Hearts, magic meter, rupee counters
  │      │          ├─ Action icons, C-buttons, A/B buttons
  │      │          ├─ Minimap and dungeon maps
  │      │          ├─ Text message boxes and font glyphs
  │      │          ├─ Pause menu / inventory / equipment (Kaleido Scope)
  │      │          └─ All drawn DIRECTLY ON TOP of postprocessed 3D scene
  │      │
  │      ├─ 3. gui->EndDraw()                       [Draws ImGui overlay on top]
  │      │     ├─ DrawGame()                        [Presents game frame buffer if rendered to FB]
  │      │     ├─ ImGui::Render()                   [Renders ImGui draw data]
  │      │     └─ Menu bars, controllers, diagnostics composited untouched
  │      │
  │      └─ 4. Interpreter::EndFrame()              [Presentation]
  │             ├─ MaterialRegistry::FlushFrame()   [Clears frame draw call log]
  │             ├─ mRapi->EndFrame()                [Flushes D3D11 device context]
  │             ├─ SwapBuffersBegin()
  │             ├─ FinishRender()
  │             └─ SwapBuffersEnd()                 [Presents backbuffer to display]
  │
Frame Complete
```

---

## 3. UI Isolation Verification

- **In-Game 2D HUD Contamination:** `0%`. The tonemapping quad runs prior to `OVERLAY_DISP`.
- **ImGui Contamination:** `0%`. ImGui renders during `gui->EndDraw()`, well after the scene postprocess has completed.
- **Classic Parity:** In CLASSIC profile, `TriggerScenePostprocess()` performs zero rendering passes. Frame output matches RC1.1 bit-for-bit.
