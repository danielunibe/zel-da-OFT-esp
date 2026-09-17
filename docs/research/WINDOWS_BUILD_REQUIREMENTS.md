# Windows build requirements

Source: official `docs/BUILDING.md` in the pinned Shipwright 9.1.1 checkout.

## Required by upstream

- Visual Studio 2022 Community (or equivalent) with Desktop development with C++.
- MSVC v143 C++ build tools.
- A Windows SDK supplied by Visual Studio.
- Python 3.
- Git.
- CMake.
- At least 8 GB RAM is recommended by upstream.

The documented generator is Visual Studio 17 2022, toolset v143, architecture x64. The documented build sequence generates `build/x64`, creates the SoH OTR target, then builds the project. This Task 02 workspace intentionally does not run that sequence.

## Environment observation — 2026-09-15

| Component | Observation | Interpretation |
|---|---|---|
| CMake | Visible from Visual Studio CMake bundle | Present |
| Python | WindowsApps launcher visible | Present, interpreter version not asserted here |
| Git | Git for Windows visible | Present |
| Ninja | Visible | Present, but not the upstream Windows generator |
| Clang | LLVM binary visible | Present, not a substitute for the documented MSVC gate |
| `cl.exe` | Not visible from current shell | MSVC gate not confirmed |
| `vcpkg` | Not visible | Not required by the upstream Windows page as a standalone prerequisite, but dependency discovery still needs the VS environment |
| Visual Studio environment | Not activated in current shell | Must be verified from a Developer PowerShell before build |

## Gate status

`WINDOWS_BUILD_ENVIRONMENT`: BLOCKED_EXTERNAL / NOT_YET_VERIFIED. This is not a source failure. It means the exact MSVC/Windows SDK developer environment has not been established in the current shell. No installation or system change was attempted.

