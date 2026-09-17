# Task 03 — Reproducible Build Gate + Development Runtime

## Result

Overall: `PARTIAL`.

- Phase A reproducible build gate: `PASS`.
- Phase B config-only modernization gate: `FAIL` pending physical Xbox-controller validation.

## Build

The official Shipwright `9.1.1` tag was built from commit `4aaad850bd5540cd77c2d83f3ad348d3b38605b2` on branch `couch-edition`. The final clean build used Visual Studio 17 2022, v143, x64, Release, MSVC `19.44.35228`, Windows SDK `10.0.26100.0`, CMake `3.31.6`, and pinned vcpkg commit `9c147d5304087fe85104b776b019c45745fa9c11` with `x64-windows-static`.

The output is `source/shipwright/x64/Release/soh.exe`, x64 PE, SHA-256 `da436565be77704d2a856676803d5415556aff06930cda01b190b8744b021d61`. The full Release build completed with exit code 0 in `00:16:09.6632297`. It emitted 592 categorized warning diagnostics and no errors; the largest categories were C4244, C4838, C4267, C4101, C4309, C4305, and C4716. These are recorded as inherited build diagnostics, not silently treated as a clean-warning build.

The first dependency attempt exposed an incompatible bundled vcpkg state. A pinned vcpkg checkout and a fresh clean build root resolved configuration and dependency generation without updating the Shipwright source or its submodules. The reproducible command is `tools/build_shipwright_9_1_1.ps1`.

## Isolated runtime

The new executable was staged only into `runtime/build-test`, a copy of the development runtime. The process started independently, created a responsive native window titled `Ship of Harkinian (DirectX 11)`, and read the staged resources and saves. Save hashes stayed equal to the pre-smoke backup hashes. Native UI screenshot/menu inspection was unavailable in this environment, so smoke is reported as `PARTIAL`, not as visual acceptance.

The live runtime was not launched or modified. Its required executable, resources, configuration, launcher files, and saves remained byte-for-byte unchanged; the complete comparison is in `reports/TASK03_HASHES_END.csv`.

## Config-only modernization

Only the isolated `runtime/build-test/shipofharkinian.json` was edited. The byte-for-byte pre-change copy is `shipofharkinian.TASK03_BEFORE.json`; the edited JSON parses successfully and the application starts afterward.

Applied changes:

- Disabled the legacy free-camera setting in the development profile only.
- Preserved native analog stick mappings and deadzone/sensitivity values.
- Preserved right-stick C-button mappings.
- Corrected the Xbox right-trigger collision by mapping SDL button 10 to the native R action and removing the prior duplicate C-Up button mapping.
- Enabled nested `gSettings.MatchRefreshRate` while preserving `gSettings.InterpolationFPS=170`; no simulation-rate change was made.
- Preserved the baseline display configuration: fullscreen 2560x1440 and window 1920x1080. No 3440x1440 assumption was introduced.

The profile and source architecture document body rumble readiness, while trigger rumble, dynamic glyphs, Spanish localization, and Spanish voice remain explicitly unimplemented. Physical Xbox control and rumble behavior could not be verified because no controller was available; therefore Phase B does not pass.

## Scope boundary

No functional Shipwright source file was changed. The only source checkout change is the administrative `.gitignore` exclusion required to keep couch-private runtime material out of source status. No source commit was created. Task 04 must not start.
