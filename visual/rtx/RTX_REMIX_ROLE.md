# RTX Remix role assessment

RTX Remix is not recommended as the runtime path for this project. Shipwright's relevant path is a modern interpreted Fast3D renderer with a DirectX 11 backend, while the typical Remix capture/injection workflow targets older fixed-function or D3D8/D3D9-era applications. It does not provide a safe automatic translation of N64 combiner semantics, display-list ordering, sprites, HUD or water behavior.

Offline tools may still be useful in isolation for candidate normal/roughness generation, selective upscaling and review. Any generated map must remain an additive candidate referenced by a future MaterialRegistry; do not use Remix Runtime, wrappers or automatic asset replacement as part of V01.

**Runtime recommendation:** NO. **Offline AI/material tooling:** MAYBE, subject to deterministic validation and human approval.
