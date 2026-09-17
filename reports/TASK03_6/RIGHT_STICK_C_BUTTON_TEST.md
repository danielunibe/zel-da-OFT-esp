# Task 03.6 — Right Stick / C-Button Test

The build-test configuration preserves native right-stick mappings:

- Right Stick Up → C-Up
- Right Stick Down → C-Down
- Right Stick Left → C-Left
- Right Stick Right → C-Right

The interactive SDL session was prepared with threshold `16000` and a sequential prompt for all four directions, diagonals, near-threshold movement, release, and held-state behavior. It received no physical input and stopped at the first prompt (`A`) before any stick step.

Results:

- Center behavior: `UNVERIFIED`
- Four cardinal directions: `UNVERIFIED`
- Diagonals: `UNVERIFIED`
- Threshold/release/accidental activation: `UNVERIFIED`
- Native mapping classification: `UNVERIFIED`
- Custom hysteresis or dominant-direction filtering: not implemented
