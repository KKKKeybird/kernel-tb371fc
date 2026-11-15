# 1.1 — 2026-11-15

- Fix the original MiSound NPE when Dolby, MiSound and Harman Kardon are all
  unavailable by normalizing the effect dropdown to `none`.
- Add null-safe reads across initialization, preference changes and teardown.
- Re-test the real `HEADSET_SETTINGS` entry and direct DolbyEqActivity.

# 1.0 — 2026-11-15

- Hide the unsupported Dirac graphic-equalizer entrance.
- Safely finish direct DolbyEqActivity intents when Dirac is unavailable.
- Preserve Dolby DAP and all Lenovo vendor audio calibration.
- Change only three methods in the MiSound DEX; audit 43,160 other methods
  byte-for-byte unchanged.
