# MM4/Xeen reference-screenshot harness

This fork of ScummVM adds a one-shot rendering mode to the MM4/World of Xeen
engine, used as a ground-truth source by the `mm5e` Go re-port's pixel-diff
regression suite.

## Invocation

```bash
~/src/scummvm/scummvm \
    --path="/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/" \
    --extrapath=/home/hqz/src/scummvm/dists/engine-data \
    --music-driver=null -m 0 -s 0 -r 0 \
    --screenshot=/tmp/out.png \
    --level=28 --cell=8,8 --facing=N \
    worldofxeen
```

The harness auto-forces `SDL_VIDEODRIVER=dummy` when `--screenshot`,
`--mm-scale-test`, or `--mm-screenshot-prefix` is on the command line, so
no game window pops up regardless of whether a real X / Wayland display is
present. To pick a different driver (e.g. `offscreen`), set
`SDL_VIDEODRIVER=offscreen` explicitly; the harness only sets the env var
when none is provided.

`--extrapath` points at the in-tree `dists/engine-data/` directory which
ships `mm.dat`, the engine-data file the MM/Xeen engine needs at boot. Drop
it if `mm.dat` is installed system-wide (e.g. via the Debian `scummvm-data`
package). The audio flags (`--music-driver=null -m 0 -s 0 -r 0`) silence the
engine; ScummVM does not expose `--music-mute` style flags as CLI options.

## Flags

| Flag | Required | Format | Description |
|------|----------|--------|-------------|
| `--screenshot` | yes | absolute filesystem path | Output PNG. Must be writable. Presence enables harness mode. |
| `--level` | yes | integer 1-128 (MM/Xeen maze ID) | Map ID. |
| `--cell` | yes | `X,Y` (each 0-15) | Party cell coordinates. |
| `--facing` | yes | `N`, `E`, `S`, or `W` (case-insensitive) | Party facing direction. |
| `--mm-side` | no (default 0) | `0` or `1` | World of Xeen side: 0=Clouds, 1=Dark Side. MM/Xeen-specific. |
| `--no-actors` | no (default off) | (no value) | Suppress actor rendering (monsters in MM/Xeen). Static scene (walls, objects, wall items) only. |
| `--mm-input-script` | no | string of `F`/`B`/`L`/`R`/`<`/`>`/`S`/`.` | Replay a sequence of player inputs after the initial render. See "Input replay" below. Whitespace and commas in the string are ignored. Requires `--mm-screenshot-prefix`. |
| `--mm-screenshot-prefix` | no | absolute filesystem path prefix | Per-step PNG output prefix. With `--mm-input-script`, writes `<prefix>.NNN.png` for steps 0..N plus a sidecar `<prefix>.trace.txt`. May replace `--screenshot` entirely; if both are set, `--screenshot` receives the final frame. |

The corresponding ConfMan keys are `screenshot`, `level`, `cell_x`,
`cell_y`, `facing`, `mm_side`, `no_actors`, `mm_input_script`,
`mm_screenshot_prefix` (ScummVM rewrites option dashes to underscores
in its config storage).

## Input replay

When `--mm-input-script=STR` is supplied alongside
`--mm-screenshot-prefix=PATH`, the harness still bootstraps and renders
an initial frame at the requested cell+facing (saved as
`<prefix>.000.png`), then dispatches each character of `STR` through the
same code path the in-game keyboard handlers use:

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

After each step the harness re-runs `Interface::draw3d(true, false)` and
writes `<prefix>.NNN.png`. A sidecar `<prefix>.trace.txt` records one
line per step in the form
`NNN <char> maze=<id> pos=<x>,<y> facing=<N|E|S|W>`. The first line uses
`-` as the input char to mark the initial state. If
`checkMoveDirection()` blocks a movement (wall / impassable terrain),
the position is unchanged but a screenshot and trace line are still
emitted, so step counts stay aligned with whatever schedule the calling
test is replaying.

Example: turn left twice, step forward, then press Space at Vertigo
cell (13,15) facing N:

```bash
./scummvm "--path=/path/to/Might and Magic 4-5/" \
    --extrapath="$PWD/dists/engine-data" \
    --music-driver=null -m 0 -s 0 -r 0 \
    --mm-screenshot-prefix=/tmp/replay --level=28 --cell=13,15 --facing=N \
    --mm-input-script="LLFS" \
    --no-actors --mm-no-border-anims --mm-pin-anim-frames worldofxeen
```

Produces `/tmp/replay.000.png` ... `/tmp/replay.004.png` plus
`/tmp/replay.trace.txt`.

## Behaviour

* Bypasses intro, copy protection, and the main menu.
* Boots into World of Xeen with the default `MAZE.PTY` party (positions
  overridden after load).
* Loads the requested map, teleports the party, and renders one first-person
  frame.
* Writes a 320x200 8-bit paletted PNG (the entire game window: viewport, HUD,
  and minimap). The calling harness should crop to the 3D viewport region
  (top-left 224x140) if it wants viewport-only comparison.
* Exits via `_exit()` immediately after the render, bypassing ScummVM's
  launcher GUI and any modal error dialogs. Exit code is 0 on success, 1 on
  any failure (bad arguments, map load mismatch, PNG write failure). Stderr
  carries a `WARNING: Screenshot harness: ...` line describing the cause.

## Headless

The harness is fully headless. No window pops up under any video driver
because `posix-main.cpp` forces `SDL_VIDEODRIVER=dummy` (when none is set)
before SDL initialises. Palette and surface data are read from the engine's
own state instead of from the SDL backend, so the captured PNG is correct
even under `dummy` and `offscreen` drivers (which return an empty palette via
`getPaletteManager()->grabPalette()`).

No autosave is written. No prompts are emitted. Failure paths exit fast (no
waiting for a user to dismiss the modal error dialog ScummVM normally shows).

## Determinism

Two invocations with identical inputs produce byte-identical PNGs. Verified
in smoke testing. Animation phases default to frame 0 via the engine's
`_isAnimReset` mechanism on the first `drawScene` call, and no animation
tick advances before the frame is captured. If you observe per-run pixel
noise on a new map you sample, it is most likely from a sprite whose initial
frame depends on something time-driven; the calling test suite should
tolerate it via a fuzziness threshold rather than chasing it here.

## Performance

Single invocation including ScummVM startup is sub-3s on a modest workstation
(measured: ~2.1s success, ~1.2s validation failure). For large regression
suites, invoke N renders in parallel with `xargs -P`; each invocation is a
self-contained process with no shared state.

## Source pointers (for maintainers)

* CLI option parsing: `base/commandLine.cpp` (search for `--screenshot`).
* Headless force: `backends/platform/sdl/posix/posix-main.cpp` (early `setenv` of `SDL_VIDEODRIVER=dummy`).
* Engine palette accessor: `engines/mm/xeen/screen.h` (`Screen::getMainPalette`).
* Harness module: `engines/mm/xeen/screenshot_harness.{h,cpp}`.
* Engine hook: `engines/mm/xeen/xeen.cpp`, in `XeenEngine::run()`.
* Plan and rationale: `docs/superpowers/plans/2026-04-30-screenshot-harness.md`.
