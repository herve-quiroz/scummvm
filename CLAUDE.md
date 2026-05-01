# Claude instructions

This is a fork of [ScummVM](https://github.com/scummvm/scummvm). It exists to serve as a ground-truth oracle for the [mm5e](https://github.com/herve-quiroz/might_and_magic_srd5e) project — a Go reimplementation of *Might and Magic IV: World of Xeen*. The fork adds a one-shot screenshot harness to the MM/Xeen engine; mm5e calls into the harness as a subprocess to capture reference frames at known party positions and pixel-diffs them against its own renderer's output.

## What this fork is, what it isn't

* **Is:** vanilla upstream ScummVM + a self-contained screenshot harness for the MM/Xeen engine. Patches are confined to a small set of files: `base/commandLine.cpp` (CLI option parsing), `backends/platform/sdl/posix/posix-main.cpp` (headless force), `engines/mm/xeen/screen.h` (palette accessor), `engines/mm/xeen/screenshot_harness.{h,cpp}` (the harness module), `engines/mm/xeen/xeen.cpp` (call site).
* **Isn't:** an upstream contribution (yet). Not a maintained ScummVM distribution. Don't take general ScummVM bug reports here.

## Branches and tags

| Branch / Tag | What it is |
|--------------|------------|
| `master` | Mirrors `upstream/master`. Don't commit harness changes here. |
| `screenshot-harness` | Where the harness lives. New harness changes go here, on top of master. |
| `mm5e-harness-v1` | Tag pinning a known-working harness state. mm5e references this tag in its docs. |

When you ship harness changes, bump the tag (`mm5e-harness-v2`, `v3`, …) and push it. Don't move existing tags; mm5e treats them as immutable references.

## The harness, at a glance

CLI invocation (full contract is in `HARNESS.md`):

```bash
./scummvm \
    --path="/path/to/Might and Magic 4-5/" \
    --extrapath=./dists/engine-data \
    --music-driver=null -m 0 -s 0 -r 0 \
    --mm-screenshot=/tmp/out.png \
    --mm-maze=28 --mm-cell=8,8 --mm-facing=N \
    worldofxeen
```

Outputs a 320×200 PNG of the rendered first-person frame and exits. Headless via `SDL_VIDEODRIVER=dummy` (auto-set when `--mm-screenshot` is on the CLI).

Source pointers:

* CLI parsing: `base/commandLine.cpp`, search for `mm-screenshot`.
* Headless force: `backends/platform/sdl/posix/posix-main.cpp`, early `setenv` of `SDL_VIDEODRIVER=dummy`.
* Engine palette accessor: `engines/mm/xeen/screen.h`, `Screen::getMainPalette`.
* Harness module: `engines/mm/xeen/screenshot_harness.{h,cpp}`.
* Engine hook: `engines/mm/xeen/xeen.cpp`, in `XeenEngine::run()`.
* Plan and rationale: `docs/superpowers/plans/2026-04-30-screenshot-harness.md`.

## What you might be asked to do

* **Fix a harness bug.** mm5e's `docs/scummvm-harness-bug.md` style document describes the symptom. Reproduce it in this tree, fix, retest, push the harness branch, bump the tag.
* **Extend the harness.** Add a new flag, expose more engine state (e.g. dump `_wo[]` flags as a sidecar JSON), add an animation-frame override. Keep changes additive and documented in `HARNESS.md`.
* **Sync with upstream.** When ScummVM upstream advances and we want their changes, see "Syncing" below.
* **Verify the harness still builds and runs after upstream changes.** A simple smoke test command is in the verification section.

## What you should NOT do

* Modify mm5e itself (`~/src/might_and_magic_srd5e/` or wherever it's checked out). Different project, different repo.
* Fix general ScummVM bugs unrelated to the MM/Xeen harness. Those belong upstream as PRs to `scummvm/scummvm`.
* Move or delete existing `mm5e-harness-vN` tags. They are immutable references mm5e relies on.
* Make non-harness changes on the `screenshot-harness` branch. Keep that branch focused; if you need to change something else, it probably belongs upstream or in a separate branch.

## Build

```bash
./configure --disable-all-engines --enable-engine=mm,xeen \
            --disable-mt32emu --disable-nuked-opl --disable-lua \
            --disable-16bit --disable-highres --disable-scalers \
            --disable-hq-scalers
make -j$(nproc)
```

Produces `./scummvm` in the tree root. Standard ScummVM build deps apply (`build-essential`, `libsdl2-dev`, `libpng-dev`, `zlib1g-dev`, `libfreetype-dev`, `libjpeg-dev`).

## Verification

After any harness change, run this end-to-end smoke test (path may need adjusting):

```bash
./scummvm \
    --path="/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/" \
    --extrapath="$PWD/dists/engine-data" \
    --music-driver=null -m 0 -s 0 -r 0 \
    --mm-screenshot=/tmp/test.png \
    --mm-maze=28 --mm-cell=8,8 --mm-facing=N \
    worldofxeen
```

Expectations:

* Exit code 0.
* `Screenshot harness: wrote /tmp/test.png` on stderr.
* No window pops up regardless of `DISPLAY` being set.
* `/tmp/test.png` shows Vertigo's indoor scene (wood ceiling, stone walls, side ornaments). NOT all black; NOT an unrelated frame.
* Run with `--mm-cell=15,8 --mm-facing=W` and confirm a different scene (entrance corridor with sky and trees visible).

If those pass, the harness is working. mm5e's regression test suite (`cmd/mm5e/scummvm_diff_integration_test.go` in mm5e) is the deeper end-to-end check.

## Syncing with upstream

```bash
git fetch upstream
git checkout master && git merge upstream/master   # update master mirror
git push origin master
git checkout screenshot-harness && git rebase master  # rebase harness on new upstream
git push origin screenshot-harness --force-with-lease
# After verifying the harness still builds + smokes:
git tag mm5e-harness-vN  # bump N
git push origin mm5e-harness-vN
```

Force-pushing the `screenshot-harness` branch is fine because mm5e references the tag, not the branch. Don't force-push `master` — that's the upstream mirror.

## When in doubt

* The harness's intended invocation contract is in `HARNESS.md`. If your change breaks that contract, you're going too far.
* The mm5e project's `docs/scummvm-reference-harness.md` describes how mm5e consumes this harness. If your change breaks mm5e's regression tests, that's a real regression — coordinate with the mm5e codebase before shipping.
* Default to small, focused commits on `screenshot-harness`. The fork's value is being a thin layer over upstream; the more we add, the harder upstream syncing gets.
