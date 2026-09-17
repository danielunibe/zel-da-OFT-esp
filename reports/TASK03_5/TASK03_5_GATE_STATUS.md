# Task 03.5 — Gate Status

Overall: `PARTIAL`

| Criterion | Status | Evidence / blocker |
|---|---|---|
| Controller detected | PASS | Xbox Series X Controller, SDL count 1 |
| SDL identity captured | PASS | Name, GUID, mapping, axes, buttons, hats |
| A / B physical event | UNVERIFIED | No physical event observed |
| LB / RB / Menu physical event | UNVERIFIED | No physical event observed; prior config discrepancy corrected in build-test |
| LT physical axis event | UNVERIFIED | No physical event observed |
| Left-stick drift/range | UNVERIFIED | No physical movement observed |
| Right-stick quadrants/diagonals | UNVERIFIED | No physical movement observed |
| Free Camera remains disabled | PASS | Build-test legacy setting remains disabled |
| SDL rumble capability | PASS | SDL reports capability |
| Rumble motor output | PARTIAL | User feel unverified; no pulse issued |
| Windows display mode | PASS | Primary 2560×1440 at 240 Hz; ultrawide 3440×1440 at 85 Hz |
| Shipwright effective mode | PASS | Isolated fullscreen run used primary 2560×1440 at 240 Hz |
| Ultrawide discrepancy investigated | PASS | Current run is 16:9 on primary; no config change applied |
| Match refresh rate | PASS | `gSettings.MatchRefreshRate=1` preserved |
| Simulation rate unchanged | PASS | No simulation configuration or source changes |
| Saves intact | PASS | Build-test copies unchanged; live saves unchanged |
| Live project intact | PASS | No live launch or modification |

## Gate decision

`TASK03_5_GATE=PARTIAL`.

Remaining evidence requires supervised physical input: button presses, LT/left-stick/right-stick movement, in-game action confirmation, and optional user confirmation of a short body-rumble pulse.
