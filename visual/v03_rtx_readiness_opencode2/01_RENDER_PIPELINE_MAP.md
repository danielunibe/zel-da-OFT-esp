# 01 — RENDER PIPELINE MAP

## Fast3D → D3D11 Complete Rendering Path

---

### 1. Frame Entry

```
Fast3dWindow::DrawAndRunGraphicsCommands()     [Fast3dWindow.cpp:~line 150]
  ├─ gui->StartDraw()
  ├─ Interpreter::StartFrame()                  [interpreter.cpp]
  │   └─ Resize framebuffers if needed
  │   └─ Calculate viewport dimensions
  ├─ Interpreter::Run(commands, mtx_replacements) [interpreter.cpp]
  ├─ gui->EndDraw()
  └─ Interpreter::EndFrame()
      ├─ mRapi->EndFrame()                      → mContext->Flush()
      ├─ mWapi->SwapBuffersBegin()              → DXGI present
      ├─ mRapi->FinishRender()
      └─ mWapi->SwapBuffersEnd()
```

---

### 2. Interpreter::Run() — Display List Execution

```
Interpreter::Run(Gfx* commands)
  ├─ SpReset()                                  // Clear loaded vertices
  ├─ mRapi->StartFrame()                        // DX11: per-frame constant buffer
  ├─ mRapi->StartDrawToFramebuffer(fbId, noiseScale)  // Bind RTV + DSV
  ├─ mRapi->ClearFramebuffer(...)
  │
  └─ while (!cmd_stack.empty())
       ├─ gfx_step()                            // Dispatch GBI opcode
       │   ├─ GfxSpVertex()                     // Transform N64 vertices
       │   │   ├─ MVP * position
       │   │   ├─ Lighting (if G_LIGHTING)
       │   │   ├─ Fog factor = z * (1/w) * fog_mul + fog_offset
       │   │   └─ Store fog in vertex alpha
       │   │
       │   ├─ GfxSpTri1()                       // *** CRITICAL: Draw call assembly ***
       │   │   ├─ Backface culling
       │   │   ├─ SetDepthTestAndMask()
       │   │   ├─ SetZmodeDecal()
       │   │   ├─ SetViewport() / SetScissor()
       │   │   ├─ Build shader options bitfield:
       │   │   │   ALPHA | FOG | TEXTURE_EDGE | NOISE | _2CYC |
       │   │   │   ALPHA_THRESHOLD | INVISIBLE | GRAYSCALE | mask/blend flags
       │   │   ├─ GenerateCC() → shader_id0 (CC mode) + shader_id1 (options)
       │   │   ├─ ImportTexture() if textures_changed
       │   │   ├─ LookupOrCreateShaderProgram(id0, id1)
       │   │   │   └─ mRapi->CreateAndLoadNewShader()  [D3D11: D3DCompile HLSL]
       │   │   ├─ Build VBO:
       │   │   │   [position: x,y,z,w] [UV: u,v] [fog: r,g,b,factor]
       │   │   │   [grayscale: r,g,b,a] [inputs: r,g,b,a per combiner input]
       │   │   └─ mBufVboLen += per_vertex_floats; mBufVboNumTris++
       │   │
       │   ├─ GfxDpSetCombineMode()             // Store CC state
       │   ├─ GfxDpSetFogColor()                // Store fog_color
       │   ├─ GfxDpSetEnvColor()                // Store env_color
       │   ├─ GfxSpGeometryMode()               // Set/clear geometry mode bits
       │   └─ ~70 other GBI opcodes
       │
       └─ Flush()                               // Submit batched triangles
```

---

### 3. Interpreter::Flush() → D3D11 DrawTriangles

```
Flush()
  └─ mRapi->DrawTriangles(mBufVbo, mBufVboLen, mBufVboNumTris)
       │                                        [gfx_direct3d11.cpp]
       ├─ Update depth/stencil state (if changed)
       ├─ Update rasterizer state / zmode_decal (if changed)
       ├─ Bind textures:
       │   PSSetShaderResources()
       │   PSSetSamplers()
       ├─ Update per-draw constant buffer (texture dimensions)
       ├─ Map + copy VBO → mVertexBuffer
       ├─ Bind VS/PS/InputLayout/BlendState (if shader changed)
       ├─ IASetPrimitiveTopology(TRIANGLELIST)
       └─ mContext->Draw(num_tris * 3, 0)       // *** GPU DRAW CALL ***
```

---

### 4. Shader Generation (Prism Template System)

```
GfxSpTri1()
  └─ LookupOrCreateColorCombiner(key)
       └─ GenerateCC(shader_id0, shader_id1)
            └─ Determines N64 CC mode + options bitmask

LookupOrCreateShaderProgram(id0, id1)
  └─ mRapi->CreateAndLoadNewShader(id0, id1)
       └─ gfx_direct3d_common_build_shader()    // Prism template expansion
            ├─ Reads: default.shader.hlsl
            ├─ Injects: @if(FOG), @if(ALPHA), @if(NOISE), etc.
            └─ Output: compiled HLSL → D3DCompile → VS + PS + InputLayout
```

**Shader template**: `libultraship/src/fast/shaders/directx/default.shader.hlsl` (332 lines)

Key shader features:
- Vertex position: MVP transform
- UV passthrough (up to 2 texture coordinates)
- Fog interpolation via alpha channel
- Color combiner inputs: SHADE, TEXEL0, TEXEL1, PRIMITIVE, ENVIRONMENT, PRIMITIVE_ALPHA, ENV_ALPHA, TEXEL0_A, TEXEL1_A, ONE, ZERO
- Alpha test with configurable threshold
- Grayscale pass-through
- Noise-based randomness

---

### 5. Texture Import Path

```
ImportTexture(tile, textureId)
  ├─ Check texture cache (mTexCache)
  ├─ If miss:
  │   ├─ Load from OTR archive (texture pool)
  │   ├─ UploadTexture(rgba32, width, height)
  │   │   └─ CreateTexture2D() + CreateShaderResourceView()
  │   └─ Store in cache
  └─ mTexSelected[tile] = textureId
```

---

### 6. Render Target Management

```
CreateFramebuffer()
  └─ Create RTV + DSV pair for off-screen rendering

StartDrawToFramebuffer(fbId, noiseScale)
  ├─ OMSetRenderTargets(rtv, dsv)
  └─ RSSetViewports(...)

CopyFramebuffer(srcFb, dstFb)
  └─ CopyResource() or CopySubresourceRegion()
```

---

### 7. Frame Start / Frame End

```
StartFrame()
  └─ Update per-frame constant buffer (time, noise)

EndFrame()
  ├─ mContext->Flush()
  ├─ SwapBuffersBegin()                         // DXGI present call
  ├─ FinishRender()
  └─ SwapBuffersEnd()
```

---

### 8. Post-Process Opportunities

**Current hooks available:**

| Hook Point | File | Function | Available? |
|---|---|---|---|
| After all triangles drawn, before present | `interpreter.cpp` | `EndFrame()` | YES — `FinishRender()` is callable |
| After `Flush()` of final batch | `interpreter.cpp` | `Flush()` | NO — mid-frame |
| DX11-specific post-process | `gfx_direct3d11.cpp` | `RunTonemappingPass()` | EXISTS but never called |
| Base class virtual | `gfx_rendering_api.h` | `RunTonemappingPass()` | Default empty impl |

**Recommended insertion point for V03 post-process chain:**
- `Interpreter::EndFrame()` → after `mRapi->EndFrame()`, before `SwapBuffersBegin()`
- Or: `Fast3dWindow::DrawAndRunGraphicsCommands()` → after `gui->EndDraw()`, before `Interpreter::EndFrame()`

---

### 9. File / Function Index

| File | Lines | Role |
|---|---|---|
| `interpreter.cpp` | 4688 | GBI command dispatch, vertex transform, triangle batching |
| `interpreter.h` | 530 | State structures (RSP, RDP), command table |
| `gfx_direct3d11.cpp` | 1543 | D3D11 device, shaders, textures, draw, tonemap |
| `gfx_direct3d_common.h` | 198 | GfxRenderingAPIDX11 class declaration |
| `gfx_dxgi.cpp` | 953 | DXGI swap chain, window, present |
| `gfx_rendering_api.h` | 86 | Abstract rendering API interface |
| `gfx_direct3d11.h` | 11 | Backend include guard |
| `gfx_direct3d_common.cpp` | 5 | Shader template helper |
| `Fast3dWindow.cpp` | 380 | Window lifecycle, frame orchestration |
| `Fast3dWindow.h` | 73 | Window class declaration |
| `default.shader.hlsl` | 332 | Prism HLSL shader template |
| `MaterialRegistry.cpp` | 152 | Material classification (NOT CALLED) |
| `MaterialRegistry.h` | 92 | Material types + DrawCallInfo struct |
| `SohMenuSettings.cpp` | 500 | VisualProfile UI (NOT FUNCTIONAL) |
