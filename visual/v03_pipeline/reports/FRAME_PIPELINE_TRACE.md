# Frame Pipeline Trace — V03

## Frame Flow
```
Interpreter::StartFrame()                    [interpreter.cpp:4288]
  ├─ Read VisualProfile CVar → SetProfile()  [interpreter.cpp:4289]
  ├─ Get window dimensions
  ├─ Resize framebuffers if needed
  └─ Set mRendersToFb

Interpreter::Run()                           [interpreter.cpp:4341]
  ├─ SpReset()
  ├─ DX11: UpdateFramebufferParameters(0)
  ├─ DX11: StartFrame() → PSConstBuf bind
  ├─ DX11: StartDrawToFramebuffer(fb0/gameFb)
  ├─ DX11: ClearFramebuffer(depth)
  ├─ Display list loop:
  │   g_exec_stack.start()
  │   while (!empty):
  │     gfx_step() → decode opcode
  │       → GfxSpTri1() → fill mBufVbo
  │         → Flush() → MaterialRegistry::RecordDrawCall() [NEW]
  │           → mRapi->DrawTriangles() → mContext->Draw()
  ├─ Flush() → final batch
  └─ Resolve MSAA if needed

Interpreter::EndFrame()                       [interpreter.cpp:4406]
  ├─ DX11: EndFrame() → mContext->Flush()
  ├─ [NEW] If ENHANCED: RunTonemappingPass()
  │   ├─ Copy backbuffer → intermediate SRV
  │   ├─ Linearize (SRGBToLinear)
  │   ├─ ACES filmic tonemapping
  │   ├─ Atmospheric fog (depth-based)
  │   └─ Re-gamma (LinearToSRGB)
  ├─ [NEW] If ENHANCED: FlushFrame()
  ├─ DXGI: SwapBuffersBegin() → Present()
  ├─ DX11: FinishRender() → no-op
  └─ DXGI: SwapBuffersEnd()
```

## Key Insertion Points
1. **VisualProfile CVar read**: `interpreter.cpp:4289` — reads CVar at frame start
2. **MaterialRegistry recording**: `interpreter.cpp:132` — records draw call in Flush()
3. **ACES tonemapping**: `gfx_direct3d11.cpp:811` — RunTonemappingPass()
4. **Atmospheric fog**: Integrated into ACES pass via depth buffer sampling
