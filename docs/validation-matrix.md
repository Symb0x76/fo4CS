# Validation matrix (`community-shaders`)

Build and static checks are automated. In-game behaviour is validated manually by
the maintainer; the scripts under `tools/` only collect and check logs and never
claim gameplay proof.

## Automated (every commit)

| Check | Command | Passes when |
|-------|---------|-------------|
| Configure + build | `cmake --preset PreNG` then `cmake --build build/PreNG --config Release` | 0 warnings (`/W4 /WX`), `CommunityShaders.dll` produced |
| Other variants | same with `--preset PostNG` / `--preset PostAE` | builds |
| Split plugin layout | `cmake --preset PreNG -B build/PreNG-split -DCOMMUNITY_SHADERS=OFF -DAIO=ON -DOVERLAY=ON` then build | `NuclearGFX.dll` + `Overlay.dll` produced |
| File size rule | `pwsh -File tools/check-file-sizes.ps1` | no `src/**` file over 800 lines |

The file-size check does not pass yet. See "Remaining oversized files" in
`docs/cs-refactor-plan.md`.

## Manual (maintainer, per release candidate)

Matrix: variant × scenario. Record each cell as PASS / FAIL / NOT RUN with the
date and the collected log folder.

| Variant | Scenario | What to observe |
|---------|----------|-----------------|
| PreNG | Load save, light-heavy interior, 2 min | LightLimitFix active, light count above the vanilla cap, no flicker on point lights |
| PreNG | Downtown exterior at night, 2 min | frame time stable; this is the scenario that previously exposed the DFLight empty-light upload regression |
| PreNG | Cold shader cache (delete the cache dir) then warm run | first run compiles without a hitch spike that stalls the frame; second run is faster |
| PreNG | Toggle each feature off and on in the overlay | settings persist to the per-feature INI, no crash on re-entry |
| PostNG | Load save, exterior | DLSS / DLSS-G / Reflex where hardware allows; LightLimitFix falls back to descriptor observation |
| PostAE | Load save, exterior | as PostNG |
| any | Loading screen → gameplay transition | frame generation blocked in menus, resumes after |
| any | Alt-tab / resolution change | `RESOURCE_CREATE` after the reset, no `ERROR` |

### Procedure

1. Deploy the built DLL (`scripts/BuildRelease<Variant>.bat` or copy from
   `build/<Variant>/Release`).
2. `pwsh -File tools/collect-runtime-log.ps1 -Phase Before -Variant <Variant>`
3. Launch the game, run the scenario, exit.
4. `pwsh -File tools/collect-runtime-log.ps1 -Phase After -Variant <Variant>`
   prints the collected folder under `dist/logs/`.
5. `pwsh -File tools/validate-runtime-log.ps1 -Path <that folder>`
6. Record visual/gameplay observations in the matrix separately from the
   validator result. A validator PASS with a visual FAIL is a FAIL.

If the game logs live elsewhere (Mod Organizer profile), pass
`-LogDirectory <path>` to both scripts.

### Required events

`validate-runtime-log.ps1` requires `BOOT`, `DEVICE_READY`, `HOOK_INSTALL` and
`FEATURE_STATE` in `CommunityShaders.log`, exactly one `BOOT`, and no `ERROR`
unless `-AllowErrors` is given. Unlike the `main` branch there are no passive
plugins: everything ships in one DLL, so every event is expected in one log.

### Deeper diagnostics

- `Debug.ini` switch `FO4CS_HANG_TRACE=1` writes a per-stage marker file next to
  the log; the last marker survives a hard hang.
- The LightLimitFix and shader-gate Debug.ini switches are listed in
  `src/Core/Shaders/ShaderCacheInternal.h` and `src/Features/LightLimitFix.cpp`.
  They default off, and several log once on first read.
