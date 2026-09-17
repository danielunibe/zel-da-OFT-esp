# Decision log — Task 02

## D-001 — Separate runtime and source

**Decision:** Keep the copied runtime under `runtime/working` and official source under `source/shipwright`.

**Reason:** The runtime contains private ROM/save/config state and is the baseline reference. It must not become the source tree or be tracked by source Git.

## D-002 — Pin the official source

**Decision:** Use `https://github.com/HarbourMasters/Shipwright.git`, tag `9.1.1`, commit `4aaad850bd5540cd77c2d83f3ad348d3b38605b2`, and create local branch `couch-edition`.

**Reason:** The executable metadata identifies Ship of Harkinian 9.1.1 / Copper Bravo and the official tag resolves to this exact commit.

## D-003 — Initialize declared submodules

**Decision:** Initialize the three pinned submodules recursively.

**Reason:** They are declared by the official checkout and are needed for a complete source workspace. No ROM, save or private runtime file was copied into them.

## D-004 — Defensive source ignore rules

**Decision:** Extend only the source repo `.gitignore` with couch-private runtime names, saves, dumps, logs, screenshots, environment files and local tool data.

**Reason:** Upstream already ignores common generated/ROM/save artifacts; the extension prevents accidental leakage if private material is placed near the source during later work.

## D-005 — No build in Task 02

**Decision:** Do not configure or build yet.

**Reason:** The task is workspace/reconnaissance only, and the current shell does not expose MSVC `cl.exe`. A build would not be an informative gate until the documented VS 2022 C++ environment is available.

## D-006 — First implementation scope

**Decision:** Start future implementation with an isolated controller profile using existing SDL mapping/configuration. Defer dynamic glyphs, semantic haptics, Spanish localization and voice-over until their source seams have dedicated designs and tests.

**Reason:** The source supports right-stick axis-to-button mappings and rumble already, while the other features cross message, audio or callback boundaries.

