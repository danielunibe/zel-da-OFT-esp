# DXR readiness

## Current boundary

The confirmed Windows path is DirectX 11. DXR is not a drop-in DX11 feature for this renderer. A practical implementation would require a D3D12 backend (or a separate rendering backend), graphics-abstraction changes, shader/pipeline management and per-frame acceleration structures.

## Required work

- Extract/retain triangle geometry and transforms at draw time for BLAS creation.
- Build/update TLAS for actors and dynamic scene geometry.
- Define alpha-tested, blended, billboard and special-combiner behavior; transparent objects cannot be treated like opaque triangles.
- Add ray-generation/miss/hit/shadow shaders, resource barriers, synchronization and denoising.
- Preserve classic fallback and avoid changing collision/gameplay visibility.

## Feature assessment

RT shadows: **VERY_HARD**; RT AO: **VERY_HARD**; RT reflections: **VERY_HARD**; path tracing: **VERY_HARD / experimental**. The largest risks are backend port scope, N64 display-list state fidelity, dynamic geometry, alpha semantics and performance at 3440x1440. No DXR implementation should begin before a stable enhanced raster path and capture/replay test exist.
