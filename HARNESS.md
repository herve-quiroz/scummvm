# MM4/Xeen reference-screenshot harness

This fork of ScummVM adds a one-shot rendering mode to the MM4/World of Xeen
engine, used as a ground-truth source by the `mm5e` Go re-port's pixel-diff
regression suite.

## Invocation

```bash
xvfb-run -a ~/src/scummvm/scummvm \
    --path="/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/" \
    --music-driver=null -m 0 -s 0 -r 0 \
    --mm-screenshot=/tmp/out.png \
    --mm-maze=28 --mm-cell=8,8 --mm-facing=N \
    worldofxeen
```

Drop `xvfb-run -a` if you have a real X display. The audio flags
(`--music-driver=null -m 0 -s 0 -r 0`) silence the engine; ScummVM does not
expose `--music-mute` style flags as CLI options.

## Flags

| Flag | Required | Format | Description |
|------|----------|--------|-------------|
| `--mm-screenshot` | yes | absolute filesystem path | Output PNG. Must be writable. Presence enables harness mode. |
| `--mm-maze` | yes | integer 1-128 | Map ID. |
| `--mm-cell` | yes | `X,Y` (each 0-15) | Party cell coordinates. |
| `--mm-facing` | yes | `N`, `E`, `S`, or `W` (case-insensitive) | Party facing direction. |
| `--mm-side` | no (default 0) | `0` or `1` | World of Xeen side: 0=Clouds, 1=Dark Side. |

The corresponding ConfMan keys are `mm_screenshot`, `mm_maze`, `mm_cell_x`,
`mm_cell_y`, `mm_facing`, `mm_side` (ScummVM rewrites option dashes to
underscores in its config storage).

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

## Determinism

Two invocations with identical inputs produce byte-identical PNGs. Verified
in smoke testing. Animation phases default to frame 0 via the engine's
`_isAnimReset` mechanism on the first `drawScene` call, and no animation
tick advances before the frame is captured. If you observe per-run pixel
noise on a new map you sample, it is most likely from a sprite whose initial
frame depends on something time-driven; the calling test suite should
tolerate it via a fuzziness threshold rather than chasing it here.

## Headless

Runs unattended under `xvfb-run -a`. No autosave is written. No prompts are
emitted. Failure paths exit fast (no waiting for a user to dismiss the modal
error dialog ScummVM normally shows).

## Performance

Single invocation including ScummVM startup is sub-3s on a modest workstation
(measured: ~2.1s success, ~1.5s validation failure). For large regression
suites, invoke N renders in parallel with `xargs -P`; each invocation is a
self-contained process with no shared state.

## Source pointers (for maintainers)

* CLI option parsing: `base/commandLine.cpp` (search for `mm-screenshot`).
* Harness module: `engines/mm/xeen/screenshot_harness.{h,cpp}`.
* Engine hook: `engines/mm/xeen/xeen.cpp`, in `XeenEngine::run()`.
* Plan and rationale: `docs/superpowers/plans/2026-04-30-screenshot-harness.md`.
