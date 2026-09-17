# V03 Prestate — Visual Pipeline Activation

## Recovery Status
All RC1.1 work verified COMPLETE_VALID. No regressions detected.

## V03 Scope
Visual-only pipeline activation:
- VisualProfile runtime consumer
- MaterialRegistry wired to draw calls
- ACES tonemapping (Enhanced only)
- Atmospheric fog (Enhanced only)
- PBR-Lite material metadata

## Color Pipeline Finding
- Backbuffer format: `DXGI_FORMAT_R8G8B8A8_UNORM` (NOT sRGB)
- Shader default: `srgb_mode = false` → outputs linear values
- No hardware sRGB conversion present
- Monitor receives gamma-encoded textures as linear → appears washed out in Enhanced
- Fix: Enable `srgb_mode` when ENHANCED profile active

## Frame Pipeline Summary
1. `Interpreter::StartFrame()` — resize, set mRendersToFb
2. `Interpreter::Run()` — display list loop, `gfx_step()` per opcode
3. `GfxSpTri1()` — accumulate vertices into mBufVbo
4. `Flush()` → `mRapi->DrawTriangles()` → `mContext->Draw()`
5. `Interpreter::EndFrame()` — `mContext->Flush()` then `Present()`
6. Post-process insertion: between line 4407 (EndFrame) and 4408 (SwapBuffersBegin)

## Files Modified (Planned)
- `gfx_direct3d11.cpp` — wire EndFrame, fog pass
- `gfx_direct3d_common.h` — fog constants
- `gfx_rendering_api.h` — new virtual methods
- `interpreter.cpp` — fog computation, sRGB enable
- `MaterialRegistry.h/.cpp` — PBR-Lite fields
- `default.shader.hlsl` — fog in fragment shader
