# Reference-screenshot harness

This fork of ScummVM adds two one-shot rendering harnesses:

* **MM/Xeen harness:** consumed by `mm5e`, a Go re-port of *Might and Magic IV: Clouds of Xeen*. Sources live under `engines/mm/xeen/`.
* **KYRA/EOB2 harness:** consumed by `griddelve`, a Go EOB2-inspired engine. Sources live under `engines/kyra/engine/`.

Both harnesses share the CLI parsing, the headless-force machinery, and the engine-agnostic core flags `--screenshot`, `--level`, `--cell`, `--facing`, `--no-actors`. Engine-specific extras live under the `--mm-*` namespace for MM/Xeen and under stock ScummVM flags (`--save-slot`) for KYRA/EOB2.

## Invocation

MM/Xeen (Might and Magic IV target):

```bash
~/src/scummvm/scummvm \
    --path="/path/to/Might and Magic 4-5/" \
    --extrapath=/home/hqz/src/scummvm/dists/engine-data \
    --music-driver=null -m 0 -s 0 -r 0 \
    --screenshot=/tmp/mm.png \
    --level=28 --cell=8,8 --facing=N \
    worldofxeen
```

KYRA/EOB2 (Eye of the Beholder II target):

```bash
~/src/scummvm/scummvm \
    --path="/path/to/EOB2/" \
    --music-driver=null -m 0 -s 0 -r 0 \
    --screenshot=/tmp/eob.png \
    --level=1 --cell=15,20 --facing=N \
    eob2
```

`--extrapath` points at `dists/engine-data/` which ships `mm.dat` (the MM/Xeen engine-data file). Drop it if `mm.dat` is installed system-wide. The audio flags silence the engine; ScummVM does not expose `--music-mute` style flags as CLI options.

The harness auto-forces `SDL_VIDEODRIVER=dummy` when `--screenshot`, `--mm-scale-test`, or `--mm-screenshot-prefix` is on the command line, so no game window pops up regardless of whether a real X / Wayland display is present. To pick a different driver (e.g. `offscreen`), set `SDL_VIDEODRIVER=offscreen` explicitly; the harness only sets the env var when none is provided.

## Shared flags

These flags are parsed in `base/commandLine.cpp` and consumed by whichever engine the loaded target boots into. Each harness validates them against its own coordinate space.

| Flag | Required | Format | Description |
|------|----------|--------|-------------|
| `--screenshot` | yes | absolute filesystem path | Output PNG. Must be writable. Presence enables harness mode. |
| `--level` | yes (see KYRA save-slot exception) | integer | Map/level ID. Range is engine-specific: 1-128 for MM/Xeen, 1-16 for EOB2. |
| `--cell` | yes (see KYRA save-slot exception) | `X,Y` | Party cell coordinates. Range is engine-specific. |
| `--facing` | yes (see KYRA save-slot exception) | `N`, `E`, `S`, or `W` (case-insensitive) | Party facing direction. |
| `--no-actors` | no (default off) | (no value) | Suppress actor rendering (monsters). Static scene only (walls, objects, wall items). |

The corresponding ConfMan keys are `screenshot`, `level`, `cell_x`, `cell_y`, `facing`, `no_actors` (ScummVM rewrites option dashes to underscores in its config storage).

## MM/Xeen-specific flags

| Flag | Required | Format | Description |
|------|----------|--------|-------------|
| `--mm-side` | no (default 0) | `0` or `1` | World of Xeen side: 0=Clouds, 1=Dark Side. |
| `--mm-no-border-anims` | no (default off) | (no value) | Suppress the five animated border UI overlays. |
| `--mm-log-slots` | no (default off) | (no value) | Log SLOT_FILL diagnostics for every indoor object slot assignment. |
| `--mm-pin-anim-frames` | no (default off) | (no value) | Pin each animated object to its cycle-start frame for deterministic captures. |
| `--mm-input-script` | no | string of `F`/`B`/`L`/`R`/`<`/`>`/`S`/`.` | Replay a sequence of player inputs after the initial render. See "Input replay" below. Whitespace and commas in the string are ignored. Requires `--mm-screenshot-prefix`. |
| `--mm-screenshot-prefix` | no | absolute filesystem path prefix | Per-step PNG output prefix. With `--mm-input-script`, writes `<prefix>.NNN.png` for steps 0..N plus a sidecar `<prefix>.trace.txt`. May replace `--screenshot` entirely; if both are set, `--screenshot` receives the final frame. |
| `--mm-scale-test` | no | absolute filesystem path | Dump SpriteResource scaler reference PNGs to the given directory and exit. |

ConfMan keys: `mm_side`, `mm_no_border_anims`, `mm_log_slots`, `mm_pin_anim_frames`, `mm_input_script`, `mm_screenshot_prefix`, `mm_scale_test`.

## KYRA/EOB2-specific flags

The KYRA harness reuses ScummVM's stock `--save-slot` flag.

| Flag | Required | Format | Description |
|------|----------|--------|-------------|
| `--save-slot` (alias `-x`) | no (default -1) | integer save slot | Restore the given save instead of doing a fresh-game bootstrap. When `--save-slot=N` is supplied, `--level`, `--cell`, and `--facing` become optional post-load overrides; omit them to capture the save's recorded position. This is how state-dependent scenes (open doors, pulled levers, scripted decoration changes) are captured for diffing. |

The KYRA harness refuses to run on anything other than EOB2 (`gameID == GI_EOB2`). EOB1 is rejected because the renderer paths and resource layout differ enough that sharing one harness would be brittle.

## Headless behavior

The harness is fully headless. No window pops up under any video driver because `posix-main.cpp` forces `SDL_VIDEODRIVER=dummy` (when none is set) before SDL initialises. Palette and surface data are read from each engine's own state instead of from the SDL backend, so the captured PNG is correct even under `dummy` and `offscreen` drivers (which return an empty palette via `getPaletteManager()->grabPalette()`).

No autosave is written. No prompts are emitted. Failure paths exit fast (no waiting for a user to dismiss the modal error dialog ScummVM normally shows).

## Input replay (MM/Xeen only)

When `--mm-input-script=STR` is supplied alongside `--mm-screenshot-prefix=PATH`, the harness still bootstraps and renders an initial frame at the requested cell+facing (saved as `<prefix>.000.png`), then dispatches each character of `STR` through the same code path the in-game keyboard handlers use:

| Char | Action |
|------|--------|
| `F` | Step forward (in current facing) |
| `B` | Step backward |
| `L` | Turn left in place |
| `R` | Turn right in place |
| `<` | Strafe left (facing unchanged) |
| `>` | Strafe right (facing unchanged) |
| `S` | Press Space (cell-script interact) |
| `.` | No-op (skip; useful to align step counts with another engine) |

After each step the harness re-runs `Interface::draw3d(true, false)` and writes `<prefix>.NNN.png`. A sidecar `<prefix>.trace.txt` records one line per step in the form `NNN <char> maze=<id> pos=<x>,<y> facing=<N|E|S|W>`. The first line uses `-` as the input char to mark the initial state. If `checkMoveDirection()` blocks a movement (wall / impassable terrain), the position is unchanged but a screenshot and trace line are still emitted, so step counts stay aligned with whatever schedule the calling test is replaying.

Example: turn left twice, step forward, then press Space at Vertigo cell (13,15) facing N:

```bash
./scummvm "--path=/path/to/Might and Magic 4-5/" \
    --extrapath="$PWD/dists/engine-data" \
    --music-driver=null -m 0 -s 0 -r 0 \
    --mm-screenshot-prefix=/tmp/replay --level=28 --cell=13,15 --facing=N \
    --mm-input-script="LLFS" \
    --no-actors --mm-no-border-anims --mm-pin-anim-frames worldofxeen
```

Produces `/tmp/replay.000.png` ... `/tmp/replay.004.png` plus `/tmp/replay.trace.txt`.

KYRA/EOB2 has no equivalent input-replay mode. The current griddelve workflow captures one frame per invocation; if replay becomes needed, the KYRA harness would have to grow its own.

## Source pointers

### Shared

* CLI option parsing: `base/commandLine.cpp` (search for `--screenshot`).
* Headless force: `backends/platform/sdl/posix/posix-main.cpp` (early `setenv` of `SDL_VIDEODRIVER=dummy`).

### MM/Xeen

* Harness module: `engines/mm/xeen/screenshot_harness.{h,cpp}`.
* Engine palette accessor: `engines/mm/xeen/screen.h` (`Screen::getMainPalette`).
* Engine hook: `engines/mm/xeen/xeen.cpp`, in `XeenEngine::run()`.

### KYRA/EOB2

* Harness module: `engines/kyra/engine/screenshot_harness.{h,cpp}`.
* Engine hook: `engines/kyra/engine/eobcommon.cpp`, in `EoBCoreEngine::go()` (just after `loadItemDefs()`).
* Friend declarations for engine state access: `engines/kyra/engine/eobcommon.h`, `engines/kyra/engine/kyra_rpg.h`.

## Modifying the harness

The `harness` branch serves both `mm5e` and `griddelve`. Treat shared infrastructure changes with care: anything you touch in `base/commandLine.cpp`, in the headless-force code in `posix-main.cpp`, or in the semantics of the core flags (`--screenshot`, `--level`, `--cell`, `--facing`, `--no-actors`) can break the other consumer even if it looks fine for the one you are testing against.

Before changing shared code:

* Build the fork (`./configure ...; make -j$(nproc)`).
* Run a smoke test against the consumer you are working on. For mm5e that means `scripts/capture-scummvm-refs.sh` against a few reference cells, or `task test:integration` if it covers the area you touched. For griddelve, the equivalent reference-capture script.
* If you can run both consumers, do so. If you cannot, dispatch a sub-agent that can, or ask the user before pushing.

Engine-specific changes (anything under `engines/mm/xeen/` or `engines/kyra/`) are lower-risk but still benefit from a smoke test on the affected project.

Other guidelines:

* Prefer additive flags over changes to existing flag semantics. A new `--mm-foo` or KYRA-side `--kyra-foo` flag costs little; rewriting the meaning of `--facing` costs both downstream consumers.
* Never rename a flag without updating both consumers in lockstep. The last flag rename (when the core flags moved from `--mm-screenshot`/`--mm-level`/`--mm-cell`/`--mm-facing` to engine-agnostic `--screenshot`/`--level`/`--cell`/`--facing`) was coordinated with both mm5e and griddelve in the same change set. Future renames need the same care.
* Engine-specific flags belong in their own namespace. MM-only flags use `--mm-*`. KYRA-only flags, if added, should use `--kyra-*` or `--eob-*` rather than living in the shared set.
* Document new flags here, in both the table and the source-pointers section.

## Determinism

Two invocations with identical inputs produce byte-identical PNGs in MM/Xeen. Verified in smoke testing. Animation phases default to frame 0 via the engine's `_isAnimReset` mechanism on the first `drawScene` call, and no animation tick advances before the frame is captured. If you observe per-run pixel noise on a new map you sample, it is most likely from a sprite whose initial frame depends on something time-driven; the calling test suite should tolerate it via a fuzziness threshold rather than chasing it here.

KYRA/EOB2 determinism follows the same principle (single `drawScene(1)` then exit, no tick), but has not been exercised at the same volume as MM/Xeen. If griddelve starts seeing per-run noise it is worth auditing the EOB2 draw paths for any time-of-day or random-frame dependencies.

## Performance

Single invocation including ScummVM startup is sub-3s on a modest workstation (measured: ~2.1s success, ~1.2s validation failure) for the MM/Xeen harness. For large regression suites, invoke N renders in parallel with `xargs -P`; each invocation is a self-contained process with no shared state. KYRA/EOB2 startup is comparable.
