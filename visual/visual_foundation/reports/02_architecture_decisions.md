# V02 Visual Foundation — Architecture Decisions

## Design Philosophy
**Remaster, not remake.** All enhancements must preserve the OoT aesthetic. Enhanced mode adds cinematic quality while maintaining the game's visual identity.

## Key Decisions

### 1. Single-Profile Toggle (Not Per-Feature)
- **Decision**: One CVar (`VisualProfile`) controls all visual enhancements
- **Rationale**: Simpler UX, prevents conflicting feature combinations, easier QA
- **Trade-off**: Less granular control, but appropriate for v02

### 2. Post-Process in Interpreter::EndFrame()
- **Decision**: Tonemapping runs in `EndFrame()` between `FlushFrame()` and `mRapi->EndFrame()`
- **Rationale**: Scene is fully rendered to backbuffer, ImGui hasn't been drawn yet
- **Alternative considered**: Inject into `gfx_direct3d11.cpp::EndFrame()` — rejected because it would tonemap ImGui too

### 3. ACES Filmic Tonemapping
- **Decision**: Use ACES filmic curve, not Reinhard or Uncharted2
- **Rationale**: ACES is the industry standard for cinematic content, good highlight rolloff, preserves OoT's warm palette
- **Implementation**: `pow`-based approximation for GPU efficiency

### 4. Inline HLSL Shaders (Not .hlsl Files)
- **Decision**: Compile tonemapping shaders from inline strings in `Init()`
- **Rationale**: Follows existing pattern (compute shader at line 225), no build system changes needed
- **Trade-off**: Less maintainable than external files, but consistent with codebase

### 5. Fog Power Curve (Not Fog Color Modification)
- **Decision**: Apply power curve to fog factor, not modify fog color
- **Rationale**: Fog color is set by game code per-scene; modifying it would break scene-specific atmospherics
- **Effect**: Smoother depth transition, more cinematic atmospheric perspective

### 6. MaterialRegistry as Singleton
- **Decision**: Global singleton with frame-based flush
- **Rationale**: Draw calls happen across many translation units; singleton provides unified access
- **Thread safety**: Not needed — rendering is single-threaded in this engine

### 7. CVar Naming Convention
- **Decision**: Use `gEnhancements.Graphics.VisualProfile` prefix
- **Rationale**: Follows existing `gEnhancements.*` pattern, consistent with other enhancement CVars

## Risk Assessment
- **Low risk**: Tonemapping is purely additive, CLASSIC mode unchanged
- **Low risk**: MaterialRegistry is read-only during rendering, no side effects
- **Medium risk**: Fog curve change could affect game balance (fog-based stealth sections)
- **Mitigation**: Power curve is subtle (0.85 exponent), CLASSIC mode untouched
