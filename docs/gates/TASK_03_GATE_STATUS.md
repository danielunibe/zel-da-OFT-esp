# Task 03 — Gate Status

Date: 2026-09-15

| Gate | Status | Evidence |
|---|---|---|
| Baseline pre-state captured | PASS | `reports/TASK03_PRESTATE/TASK03_PRESTATE.txt` |
| Exact Shipwright 9.1.1 source | PASS | Tag `9.1.1`, commit `4aaad850bd5540cd77c2d83f3ad348d3b38605b2` |
| Reproducible x64 toolchain | PASS | `reports/TASK03_BUILD_ENVIRONMENT.txt` |
| Pinned dependency resolution | PASS | vcpkg `9c147d5304087fe85104b776b019c45745fa9c11`, static x64 triplet |
| Stock Release build | PASS | `reports/TASK03_BUILD_LOG.txt`, exit code 0 |
| PE architecture | PASS | `soh.exe` is x64; SHA-256 recorded in build environment |
| Isolated runtime staging | PASS | `runtime/build-test` |
| Independent runtime smoke | PARTIAL | Responsive native process/window and save reads; native UI screenshot unavailable |
| Live project unchanged | PASS | `reports/TASK03_HASHES_END.csv`; all tracked source changes administrative only |
| Config-only profile validity | PASS | JSON parses and build-test launches after config change |
| Physical Xbox controls and rumble | BLOCKED_EXTERNAL | No Xbox controller available for input/event verification |
| Phase A build gate | PASS | Build and isolated runtime evidence complete |
| Phase B config gate | FAIL | Hardware acceptance criteria remain unverified |

Overall status: PARTIAL.
