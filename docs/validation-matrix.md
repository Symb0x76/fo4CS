# Validation matrix

Build and static checks are automated. In-game behaviour is validated manually by
the maintainer; the scripts under `tools/` only collect and check logs and never
claim gameplay proof.

## Automated (every commit)

| Check | Command | Passes when |
|-------|---------|-------------|
| Configure + build, AIO layout | `cmake --preset PreNG` then `cmake --build build/PreNG --config Release` | 0 warnings (`/WX`), `NuclearGFX.dll` + `Overlay.dll` produced |
| Configure + build, standalone layout | `cmake --preset PreNG -B build/PreNG-standalone -DAIO=OFF -DFRAMEGEN=ON -DREFLEX=ON -DUPSCALER=ON` then build | `FrameGen.dll`, `Reflex.dll`, `Upscaler.dll`, `Overlay.dll` produced |
| Other variants | same with `--preset PostNG` / `--preset PostAE` | builds |
| File size rule | `pwsh -File tools/check-file-sizes.ps1` | no `src/**` file over 800 lines |

## Manual (maintainer, per release candidate)

Matrix: variant × plugin layout × scenario. Record each cell as PASS / FAIL /
NOT RUN with the date and the collected log folder.

| Variant | Layout | Scenario | What to observe |
|---------|--------|----------|-----------------|
| PreNG | AIO | Load save, exterior, 2 min | FSR upscaling active, FSR frame generation active, no ERROR events |
| PreNG | Standalone (Upscaler + FrameGen + Overlay) | same | panels appear in overlay (hotkey `End`), settings persist after save |
| PostNG | AIO | same | DLSS + DLSS-G + Reflex where hardware allows |
| PostNG | Standalone (Reflex only) | same | Reflex panel visible, proxy hooks installed by Reflex |
| PostAE | AIO | same | as PostNG |
| any | any | Loading screen → gameplay transition | frame generation blocked in menus, resumes after |
| any | any | Alt-tab / resolution change | `RESOURCE_RESET` then `RESOURCE_CREATE`, no ERROR |

### Procedure

1. Deploy the built DLLs (`scripts/BuildRelease<Variant>.bat` or copy from
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
`FEATURE_STATE` in the hook-owning plugin's log (NuclearGFX or Upscaler; FrameGen
when it owns the hooks), exactly one `BOOT` per log, and no `ERROR` unless
`-AllowErrors` is given. Overlay, FrameGen and Reflex logs are treated as passive
(only `BOOT` required) because they normally ride on another plugin's hooks.
