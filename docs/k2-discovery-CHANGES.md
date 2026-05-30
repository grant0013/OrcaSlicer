# K2-Discovery — Proposed Changes

**Branch:** `k2-discovery`
**Base commit:** `8a2dbd1c10` (Fix merge-resolution braces in PrintHostDialogs.hpp + PhysicalPrinterDialog.cpp)
**Source upstream for profiles:** CrealityPrint `master` (https://github.com/CrealityOfficial/CrealityPrint)
**Author of these changes:** Shael Rosen

This document summarizes a batch of Creality **K2-series** profile additions, profile
normalization fixes, and a small engine/GUI feature port (configurable prime-tower
position). It is intended as a review aid for the original `k2-discovery` maintainer.

---

## 1. Summary

| Area | Change | Files |
|------|--------|-------|
| Filament profiles | Added 9 missing **Creality-brand** K2 filaments (Hyper PETG / PETG-CF) | 9 new |
| Process profiles | Added 8 missing K2 process profiles (HueForge / Strength / High Quality / SpecialFilament) | 8 new |
| Profile normalization | Re-ran the upstream fixer; caught files the original port missed | 10 filament + 3 process |
| Multicolor strip | Removed invalid `{if !multicolor_method}` g-code wrappers the strip missed | 4 existing + 5 new |
| Prime-tower position | Ported CrealityPrint's `prime_tower_position_type` (enum option + bed-corner placement) | 4 source files |
| Prime-tower position (live) | Made the dropdown actually re-position the tower (CrealityPrint's is non-responsive) | 1 source file |
| Prime-tower shape | Set K2 process tower wall type to `rectangle` (Orca defaults to `rib`) | 60 process |
| Tower position default | Set all 12 K2-series machines to `Middle Upper` | 12 machine |

All changes validated with `OrcaSlicer_profile_validator` (exit 0) and a clean build.

---

## 2. Profile changes

### 2.1 New Creality-brand K2 filaments (9)
These exist in CrealityPrint master but were missing from `k2-discovery`. They target
printers that already exist here (K2 / K2 Plus / K2 Pro). All are PETG-family
(`PETG` / `PETG-CF`), normalized to repo convention and re-pointed to inherit
`fdm_filament_pet` (matching the existing CR-PETG profiles; `fdm_filament_petg` is **not**
defined for this vendor — it resolves only via the OrcaFilamentLibrary base bundle, which
lacks it).

```
filament/Hyper PETG @Creality K2 0.4 nozzle.json
filament/Hyper PETG @Creality K2 Plus 0.2 nozzle.json
filament/Hyper PETG @Creality K2 Plus 0.4 nozzle.json
filament/Hyper PETG @Creality K2 Plus 0.6 nozzle.json
filament/Hyper PETG @Creality K2 Plus 0.8 nozzle.json
filament/Hyper PETG @Creality K2 Pro 0.4 nozzle.json
filament/Hyper PETG-CF @Creality K2 0.4 nozzle.json
filament/Hyper PETG-CF @Creality K2 Plus 0.4 nozzle.json
filament/Hyper PETG-CF @Creality K2 Pro 0.4 nozzle.json
```

> **Not added (out of scope):** 13 K2 **SE** Creality filaments — the K2 SE printer
> (machine + process) does not exist in this branch, so those filaments would be orphaned.
> Adding them requires porting the entire K2 SE printer first. Also excluded: ~21 `Generic`
> and 2 `eSUN` K2 filaments (not Creality-brand).

### 2.2 New K2 process profiles (8)
Genuinely-new capabilities (no existing equivalent under Orca naming). The ~31 CrealityPrint
`Standard` profiles were intentionally **not** copied — they duplicate layer-height slots
this branch already covers under Orca names (`Optimal`/`Draft`/etc.).

```
process/0.08mm HueForge @Creality K2 0.4 / Plus 0.4 / Pro 0.4 nozzle.json
process/0.20mm Strength @Creality K2 Plus 0.4 nozzle.json
process/0.30mm Strength @Creality K2 Plus 0.6 nozzle.json
process/0.40mm Strength @Creality K2 Plus 0.8 nozzle.json
process/0.20mm High Quality @Creality K2 Plus 0.4 nozzle.json
process/0.20mm SpecialFilament @Creality K2 Plus 0.4 nozzle.json
```

### 2.3 Normalization the original port missed
Re-ran the upstream fixer:
```
python3 scripts/orca_filament_lib.py -v Creality -p filament -f --force
python3 scripts/orca_filament_lib.py -v Creality -p process  -f --force
```
This brought **10 filament** + **3 process** existing files into convention (spaces→tabs,
field ordering, scalar→array for `filament_cost`/`filament_density`/`temperature_vitrification`/
`filament_max_volumetric_speed`). No semantic content changes.

### 2.4 `{if !multicolor_method}` strip (the "few things we missed")
The original multicolor strip missed the **4 CR-PETG K2 Plus** files (they are also the only
files that use `layer_z` instead of `position[2]` — the two anomalies coincide). These plus
the **5 new** Hyper PETG/PETG-CF K2 Plus files contained the invalid OrcaSlicer g-code
`{if !multicolor_method}`, which causes a hard slice failure:

> `filament_start_gcode Parsing error at line 2: Not a variable name {if !multicolor_method}`

All 9 were stripped to the canonical temperature-only `filament_start_gcode` used by the
working K2 siblings. **0** occurrences of `multicolor_method` remain in Creality profiles.

> **Note for maintainer:** the CR-PETG K2 family is the only set using `layer_z`; all 215
> other Creality filament profiles use `position[2]`. Worth normalizing one way or the other.

### 2.5 `Creality.json`
Registered the 17 new profiles in `filament_list` / `process_list`; bumped `version`
`02.03.02.75 → 02.03.02.77`.

---

## 3. Engine / GUI change: configurable prime-tower position

CrealityPrint has a per-printer **`prime_tower_position_type`** option (a 9-cell grid:
Left/Middle/Right × Upper/Center/Below) that anchors the prime tower to a bed corner/edge.
OrcaSlicer has no such option — it hardcodes a single position. This ports the feature.

**Files:**
- `src/libslic3r/PrintConfig.hpp` — add `enum GCodeFlavorText` (9 values) and the
  `prime_tower_position_type` member on `GCodeConfig` (printer-side config).
- `src/libslic3r/PrintConfig.cpp` — enum keys map + option definition (default `Middle_Upper`,
  matching CrealityPrint).
- `src/libslic3r/Preset.cpp` — add `prime_tower_position_type` to `s_Preset_printer_options`
  so the machine preset accepts the key. *(This also un-breaks a few existing non-K2 machine
  profiles, e.g. Ender-5 Max, that already carried the key and were being silently stripped.)*
- `src/slic3r/GUI/PartPlate.cpp` — `set_default_wipe_tower_pos_for_plate()` now computes the
  position from `prime_tower_position_type` using CrealityPrint's bed-corner math (both the
  ≤15° and >15° rotation branches), then clamps to the plate as before.
- `src/slic3r/GUI/Tab.cpp` — expose the **Tower Position** dropdown under
  *Printer settings → Multimaterial → Wipe tower*.
- `src/slic3r/GUI/Plater.cpp` — **make the selector live**: add the key to the plater's
  watched config and recompute the tower position for all plates in `on_config_change`.
  (CrealityPrint's own dropdown does not re-position on change — this build fixes that.)

**Design choice — backwards compatibility:** the default value `Middle_Upper` routes to the
**legacy** fixed placement, so any printer that does not explicitly set the option behaves
exactly as before. Only profiles that set a non-default value (here, the K2 series) use the
new bed-corner math. Trade-off: selecting `Middle_Upper` in the dropdown yields the legacy
center spot rather than CrealityPrint's hard-against-the-back-edge Middle-Upper coordinate.
Flipping this would change the default tower position for *all* printers/vendors, so it was
left opt-in.

**Profiles:** all 12 K2-series machines (K2 / K2 Plus / K2 Pro × 0.2/0.4/0.6/0.8) set to
`Middle Upper`.

---

## 4. Prime-tower shape

OrcaSlicer defaults `wipe_tower_wall_type` to `wtwRib` (a long thin rib-wall tower);
CrealityPrint always uses a rectangular tower (its "Square prime tower" toggle =
`prime_tower_rib_wall`, which drives CP's separate `WipeTowerCreality.cpp` engine).
To match CrealityPrint's shape, set `wipe_tower_wall_type = rectangle` on the K2 process
profiles. Orca's existing **Wall type** dropdown (Process → Others → Prime tower) is the
equivalent control and remains available for per-print override.

---

## 5. Build / validation notes

- **`PrintConfig.hpp` is included widely.** Adding a member to `GCodeConfig` changes the
  struct layout — an *incremental* build leaves a partial set of TUs on the old layout and
  causes an ABI-skew crash (observed: `SIGSEGV` in `GCodeConfig` copy-ctor via
  `Print::check_multi_filament_valid`). **A clean slicer rebuild is required** after the
  header change. Profile-only and `.cpp`-only changes build incrementally.
- Validated each step with:
  `OrcaSlicer_profile_validator -p resources/profiles -v Creality` → exit 0.

---

## 6. Suggested follow-ups for the maintainer

1. Normalize the CR-PETG K2 family off `layer_z` onto `position[2]` (or vice-versa) for
   consistency with the other 215 Creality filament profiles.
2. Decide whether `Middle_Upper` should be edge-anchored (CrealityPrint-exact) vs. the current
   legacy-center mapping.
3. Consider porting the **K2 SE** printer so its 13 Creality filaments can be added.
4. Optionally make `prime_tower_position_type` visible in Simple mode (currently `comAdvanced`).
