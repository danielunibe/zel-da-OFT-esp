# DLAA / DLSS readiness

This is a feasibility audit only. No NVIDIA SDK is integrated.

| Feature | Difficulty | Required contracts |
|---|---|---|
| DLAA | HARD | jittered projection, stable scene color, depth, motion vectors or robust reactive handling, UI separation |
| DLSS Super Resolution | VERY_HARD | all DLAA inputs plus temporal history, exposure, disocclusion/reactive masks and tuning for generated Fast3D output |
| Frame Generation | VERY_HARD | reliable motion vectors for camera and objects, UI separation, pacing/latency policy and vendor integration |

The existing DX11 backend, framebuffer abstraction and matrix state provide useful starting points, but they do not prove the complete temporal-input contract. Frame interpolation is presentation/interpolation behavior, not DLSS motion-vector data. MSAA is not a substitute for jitter/history.

Recommended prerequisite: first make a deterministic render capture with depth, camera matrices, UI mask and per-draw identity. Prototype DLAA only after classic/enhanced output is stable. Keep classic scaling/MSAA available.
