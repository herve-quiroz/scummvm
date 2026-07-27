# Claude instructions

**Before making any change to the harness, read `HARNESS.md`, especially the "Modifying the harness" section.** It documents the cross-project compatibility constraints that govern this branch.

This is a fork of [ScummVM](https://github.com/scummvm/scummvm). It exists to serve as a ground-truth oracle for two Go projects:

* [mm5e](https://github.com/herve-quiroz/might_and_magic_srd5e), a Go re-port of *Might and Magic IV: Clouds of Xeen*. Consumes the MM/Xeen harness.
* [griddelve](https://github.com/herve-quiroz/griddelve), a Go EOB2-inspired engine. Consumes the KYRA/EOB2 harness.

The fork adds one-shot screenshot harnesses to the MM/Xeen and KYRA engines; both projects call into the harness as a subprocess to capture reference frames at known party positions and pixel-diff them against their own renderer's output.

## What this fork is, what it isn't

* **Is:** vanilla upstream ScummVM + two self-contained screenshot harnesses. Patches are confined to a small set of files: `base/commandLine.cpp` (CLI option parsing), `backends/platform/sdl/posix/posix-main.cpp` (headless force), `engines/mm/xeen/screen.h` and `engines/mm/xeen/screenshot_harness.{h,cpp}` and `engines/mm/xeen/xeen.cpp` (MM/Xeen harness), `engines/kyra/engine/screenshot_harness.{h,cpp}` and `engines/kyra/engine/eobcommon.{h,cpp}` and `engines/kyra/engine/kyra_rpg.h` (KYRA/EOB2 harness).
* **Isn't:** an upstream contribution (yet). Not a maintained ScummVM distribution. Don't take general ScummVM bug reports here.

## Scope of upstream policies

`AI-GUIDELINES.md` at the repo root is upstream ScummVM's contributor policy for PRs to `scummvm/scummvm` (no AI-authored code, mandatory `Assisted-by:` trailers, etc.). It does **not** govern commits on the `harness` branch of this fork. Harness commits are tooling for mm5e and griddelve, not contributions to upstream, and may be Claude-authored without `Assisted-by:` trailers. If/when a harness change is ever proposed upstream, that submission would need to comply with `AI-GUIDELINES.md` separately.

## Branches and tags

| Branch / Tag | What it is |
|--------------|------------|
| `master` | Mirrors `upstream/master`. Don't commit harness changes here. |
| `harness` | The single source of truth for both harnesses. Shared between mm5e and griddelve. All harness work goes here. |
| `mm5e-harness-v1` .. `mm5e-harness-v12` | Legacy version-pin tags from when mm5e pinned a specific harness state. Kept for archeology only. Do not move, do not delete, do not create new ones. |

The old `screenshot-harness` and `eob2-harness` branches have been deleted from origin. Both have merged into `harness`.

mm5e and griddelve now track the `harness` branch directly rather than a pinned tag. Coordination cost moves from "bump a tag every change" to "don't break either consumer", documented in `HARNESS.md`.

## The harness, at a glance

Full CLI contract is in `HARNESS.md`. Short version:

MM/Xeen:

```bash
./scummvm \
    --path="/path/to/Might and Magic 4-5/" \
    --extrapath=./dists/engine-data \
    --music-driver=null -m 0 -s 0 -r 0 \
    --screenshot=/tmp/mm.png \
    --level=28 --cell=8,8 --facing=N \
    worldofxeen
```

KYRA/EOB2:

```bash
./scummvm \
    --path="/path/to/EOB2/" \
    --music-driver=null -m 0 -s 0 -r 0 \
    --screenshot=/tmp/eob.png \
    --level=1 --cell=15,20 --facing=N \
    eob2
```

Each invocation outputs a 320x200 PNG of the rendered first-person frame and exits. Headless via `SDL_VIDEODRIVER=dummy` (auto-set when `--screenshot` is on the CLI).

Source pointers (see `HARNESS.md` for the full split between shared / MM / KYRA):

* CLI parsing: `base/commandLine.cpp`, search for `--screenshot`.
* Headless force: `backends/platform/sdl/posix/posix-main.cpp`, early `setenv` of `SDL_VIDEODRIVER=dummy`.
* MM/Xeen harness: `engines/mm/xeen/screenshot_harness.{h,cpp}`, hooked from `engines/mm/xeen/xeen.cpp` (`XeenEngine::run()`).
* KYRA/EOB2 harness: `engines/kyra/engine/screenshot_harness.{h,cpp}`, hooked from `engines/kyra/engine/eobcommon.cpp` (`EoBCoreEngine::go()` after `loadItemDefs()`).

## What you might be asked to do

* **Fix a harness bug.** The reporting consumer (mm5e or griddelve) typically has a docs page describing the symptom. Reproduce in this tree, fix, retest both consumers if the change touches shared code, push the harness branch.
* **Extend the harness.** Add a new flag, expose more engine state, add an animation-frame override. Keep changes additive and documented in `HARNESS.md`. Prefer namespaced flags (`--mm-*`, `--kyra-*` or `--eob-*`) over additions to the shared core.
* **Sync with upstream.** When ScummVM upstream advances and we want their changes, see "Syncing" below.
* **Verify the harness still builds and runs after upstream changes.** A simple smoke test command is in the verification section.

## What you should NOT do

* Modify mm5e or griddelve themselves from inside this tree. Different projects, different repos.
* Fix general ScummVM bugs unrelated to the harnesses. Those belong upstream as PRs to `scummvm/scummvm`.
* Create new `mm5e-harness-vN` tags. The tag-bump cycle has been retired; consumers track the `harness` branch directly.
* Move or delete the existing `mm5e-harness-v1` .. `mm5e-harness-v12` legacy tags. They are archeology.
* Make non-harness changes on the `harness` branch. Keep that branch focused; if you need to change something else, it probably belongs upstream or in a separate branch.

## Build

```bash
./configure --disable-all-engines --enable-engine=mm,xeen,kyra,eob \
            --disable-mt32emu --disable-nuked-opl --disable-lua \
            --disable-16bit --disable-highres --disable-scalers \
            --disable-hq-scalers
make -j$(nproc)
```

Produces `./scummvm` in the tree root. Standard ScummVM build deps apply (`build-essential`, `libsdl2-dev`, `libpng-dev`, `zlib1g-dev`, `libfreetype-dev`, `libjpeg-dev`).

Every sub-engine must be named explicitly. After `--disable-all-engines`, enabling a parent does not pull its sub-engines in: `configure` only expands them for the `<engine>_all` form. So `xeen` is listed alongside its parent `mm`, and `eob` alongside its parent `kyra`. Omitting `eob` still builds and still links a working binary, it just has no Eye of the Beholder support, which silently breaks every griddelve reference capture.

The two consumers need different halves: mm5e needs `mm,xeen`, griddelve needs `kyra,eob`. If you only need one harness for the change at hand, you can drop the unused pair from `--enable-engine`. CI / verification runs should include both.

## Verification

After any harness change, run end-to-end smoke tests for whichever consumer you touched. If you changed shared code (CLI parsing, headless force, core flag semantics), run smoke tests for both consumers.

MM/Xeen smoke test:

```bash
./scummvm \
    --path="/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/" \
    --extrapath="$PWD/dists/engine-data" \
    --music-driver=null -m 0 -s 0 -r 0 \
    --screenshot=/tmp/test.png \
    --level=28 --cell=8,8 --facing=N \
    worldofxeen
```

Expectations:

* Exit code 0.
* `Screenshot harness: wrote /tmp/test.png` on stderr.
* No window pops up regardless of `DISPLAY` being set.
* `/tmp/test.png` shows Vertigo's indoor scene (wood ceiling, stone walls, side ornaments). NOT all black; NOT an unrelated frame.
* Run with `--cell=15,8 --facing=W` and confirm a different scene (entrance corridor with sky and trees visible).

KYRA/EOB2 smoke test follows the same pattern with the EOB2 target. The deeper end-to-end checks live in each consumer: mm5e's `cmd/mm5e/scummvm_diff_integration_test.go`, and griddelve's equivalent.

## Syncing with upstream

```bash
git fetch upstream
git checkout master && git merge upstream/master   # update master mirror
git push origin master
git checkout harness && git rebase master           # rebase harness on new upstream
# Build, run smoke tests for both consumers.
git push origin harness --force-with-lease
```

Force-pushing the `harness` branch is fine because consumers track the branch head and a pre-push rebuild covers the API surface. Don't force-push `master`; that's the upstream mirror.

No tag bump after the rebase. The legacy `mm5e-harness-vN` tags stay frozen at their original commits.

## When in doubt

* The harness's intended invocation contract is in `HARNESS.md`. If your change breaks that contract, you're going too far.
* Each consumer's `docs/` directory describes how it calls the harness (mm5e: `docs/scummvm-reference-harness.md`; griddelve: its own equivalent). If your change breaks either consumer's regression tests, that's a real regression; coordinate before shipping.
* Default to small, focused commits on `harness`. The fork's value is being a thin layer over upstream; the more we add, the harder upstream syncing gets.
