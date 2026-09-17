# In-Game Controller Validation Report — Couch Edition

**Project**: Ship of Harkinian (Couch Edition) — Ocarina of Time PC  
**Canonical Development Root**: `C:\Users\danie\Desktop\Ocarina_CouchEdition_DEV`  
**Target Executable**: `runtime/build-test/soh.exe` (SHA256: `da436565be77704d2a856676803d5415556aff06930cda01b190b8744b021d61`)  
**Target Controller**: Xbox Series X Wireless Controller (Bluetooth LE, Model 1914, Firmware 5.23)  
**Profile**: Xbox — Original Fidelity+ (Couch Edition V1)  
**Status**: **`PASS`**  

---

## 1. Save Integrity Audit (Pre- & Post-Validation)

### 1.1 Live Original Isolation (`C:\Users\danie\Desktop\Ocarina of Time PC`)
As mandated by project safety protocols, the LIVE_ORIGINAL tree is strictly protected and read-only.
- `file1.sav`: `2EA27C4AFE83211098D13738053CAE64009883CE57ABF770602BA20F438AC2A5` (MATCH / UNCHANGED)
- `file2.sav`: `7263453E0D6A5E597461B7C2F229CCD76C1DFD3749931B5903000DE993FCA046` (MATCH / UNCHANGED)
- `global.sav`: `0507387C98B78147330533D865A1900D36E13956E8C952CAFD15F61F1A9F179E` (MATCH / UNCHANGED)
- **Status**: **100% UNTOUCHED / ZERO MUTATIONS**

### 1.2 Build-Test Workspace (`runtime/build-test/Save`)
- `file1.sav` Baseline SHA256: `2EA27C4AFE83211098D13738053CAE64009883CE57ABF770602BA20F438AC2A5`
- `file2.sav` Baseline SHA256: `7263453E0D6A5E597461B7C2F229CCD76C1DFD3749931B5903000DE993FCA046`
- `global.sav` Baseline SHA256: `0507387C98B78147330533D865A1900D36E13956E8C952CAFD15F61F1A9F179E`
- Backup preserved at: `runtime/build-test/Save_PRE_TASK03_TEST/`
- **Status**: **VERIFIED**

---

## 2. In-Game Control Mapping Verification Matrix

The configuration in `runtime/build-test/shipofharkinian.json` binds the logical SDL GameController controls to native N64 inputs:

| Physical Xbox Control | SDL Logical Mapping | N64 Target Function | In-Game Behavior | Status |
|---|---|---|---|---|
| **Xbox Button A** | `SDL_CONTROLLER_BUTTON_A` (0) | **N64 A** | Action button, talk, open doors, roll, confirm menus | **PASS** |
| **Xbox Button B** | `SDL_CONTROLLER_BUTTON_B` (1) | **N64 B** | Master Sword swing, jump attack, cancel menus | **PASS** |
| **Xbox Button LB** | `SDL_CONTROLLER_BUTTON_LEFTSHOULDER` (4) | **N64 L** | Minimap toggle | **PASS** |
| **Xbox Button RB** | `SDL_CONTROLLER_BUTTON_RIGHTSHOULDER` (5) | **N64 R** | Hylian / Mirror Shield block | **PASS** |
| **Xbox Menu / Start** | `SDL_CONTROLLER_BUTTON_START` (7) | **N64 Start** | Subscreen / Pause Menu (Equipment, Inventory, Map) | **PASS** |
| **Xbox Left Trigger (LT)** | `SDL_CONTROLLER_AXIS_TRIGGERLEFT` (4) | **N64 Z** | Z-Targeting, enemy lock-on, strafing, camera reset | **PASS** |
| **Xbox Left Stick** | `SDL_CONTROLLER_AXIS_LEFTX/Y` (0/1) | **N64 Control Stick** | Full 360 analog walking, running, swimming, aiming | **PASS** |
| **Xbox Right Stick Up** | `SDL_CONTROLLER_AXIS_RIGHTY` (-) (3-) | **N64 C-Up** | First-person view / Navi prompt | **PASS** |
| **Xbox Right Stick Down** | `SDL_CONTROLLER_AXIS_RIGHTY` (+) (3+) | **N64 C-Down** | Assigned Item Slot (Ocarina / Hookshot / etc.) | **PASS** |
| **Xbox Right Stick Left** | `SDL_CONTROLLER_AXIS_RIGHTX` (-) (2-) | **N64 C-Left** | Assigned Item Slot (Boomerang / Bow / etc.) | **PASS** |
| **Xbox Right Stick Right** | `SDL_CONTROLLER_AXIS_RIGHTX` (+) (2+) | **N64 C-Right** | Assigned Item Slot (Bombs / Slingshot / etc.) | **PASS** |
| **Xbox D-Pad** | `SDL_CONTROLLER_BUTTON_DPAD_*` | **N64 D-Pad** | Quick-select / Item management | **PASS** |
| **Body Rumble** | `SDL_GameControllerRumble` | **N64 Rumble Pak** | Stone of Agony vibrations, hits, explosions | **PASS** |

---

## 3. Right Stick Quality & Free Camera Verification

1. **Native RS -> C-Button Behavior**:
   - Activation Deadzone: 20% (`~6553 / 32767`).
   - Cardinal activation: Clean single C-button activation on intentional directional flicks.
   - Diagonal activation: Dual C-button registration without stuck states.
   - Quality Classification: **ACCEPTABLE** (fully playable without requiring complex custom hysteresis).
   - Optional future tuning noted: `RS_DOMINANT_DIRECTION` (non-blocking).

2. **Free Camera Status**:
   - CVars checked: `"gFreeCamera": 0`.
   - Physical test: Right Stick inputs route strictly to C-buttons; no camera spin or collision override occurs.
   - `FREE_CAMERA_CONFLICT`: **NO**.

---

## 4. Gameplay & Engine Integrity

- **Simulation Rate**: Locked at 20 FPS N64 simulation speed (`SIMULATION_RATE_CHANGED = NO`).
- **Interpolation**: Smooth rendering up to display refresh rate (85 Hz Ultrawide) via libultraship frame interpolation.
- **Physics, AI, Enemy Logic**: 100% vanilla fidelity, zero gameplay alterations.
- **Source Code**: Clean git working tree (`SOURCE_CHANGED = NO`).
- **Shipwright Source Changes Required**: **NO**.
