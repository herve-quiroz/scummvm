# MM4/Xeen Screenshot Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a one-shot rendering mode to ScummVM's MM4/World of Xeen engine that loads the game, teleports the party to a caller-specified position, renders a single first-person frame, writes it as a PNG, and exits with status 0.

**Architecture:** Implement approach #1 from the spec ("CLI-flag + early-exit mode"). Add five new long CLI options to `base/commandLine.cpp` that flow into ConfMan. Add a small new module `engines/mm/xeen/screenshot_harness.{h,cpp}` that owns parsing/validation and the bootstrap-render-save flow. Hook into `XeenEngine::run()` so that when the harness is enabled the engine bypasses intro/menu, performs a minimal in-game bootstrap, draws one frame, dumps a 320×200 paletted PNG via `Image::writePNG`, and quits.

**Tech Stack:** C++ (ScummVM coding conventions), GNU Make build system, ScummVM `Common::ConfMan`, ScummVM `Image::writePNG`, MM/Xeen engine APIs (`XeenEngine`, `Map`, `Party`, `Interface`, `Screen`, `SavesManager`, `FileManager`).

---

## Background research summary

Before writing code, the implementer should know what was already discovered when this plan was written:

1. **CLI parsing.** ScummVM long options are added in `base/commandLine.cpp` inside `parseCommandLine()`. Unknown options trigger `usage("Unrecognized option '%s'", argv[i])` (line ~1075). All option values are stored in a `Common::StringMap settings` that is later flushed to `ConfMan` via the transient domain. Values can be read inside the engine with `ConfMan.hasKey("foo") ? ConfMan.get("foo") : ""`.

2. **Engine bootstrap.** `XeenEngine::run()` (`engines/mm/xeen/xeen.cpp:191`) calls `initialize()` (creates all subsystems and calls `initGraphics(SCREEN_WIDTH, SCREEN_HEIGHT)`) then `outerGameLoop()`. `outerGameLoop()` dispatches on `_gameMode`, which starts as `GMODE_STARTUP`; that calls `showStartup()` (intro), then `showMainMenu()`, then eventually `playGame()`/`play()`. The harness needs to bypass `showStartup()` and `showMainMenu()` entirely.

3. **Party teleport pattern.** The engine already has the exact teleport idiom we need — see `Debugger::cmdMap` (`engines/mm/xeen/debugger.cpp:151`):
   ```cpp
   map.load(mapId);
   party._mazePosition.x = x;
   party._mazePosition.y = y;
   party._mazeDirection = DIR_NORTH;
   ```
   Also `engines/mm/xeen/scripts.cpp:1131` (`cmdCutsceneEndClouds`) and `engines/mm/xeen/interface.cpp:1913` set `_party->_mazeId`, `_party->_mazePosition`, `_party->_mazeDirection`. The `Direction` enum (`engines/mm/xeen/party.h:36`) is `DIR_NORTH=0, DIR_EAST=1, DIR_SOUTH=2, DIR_WEST=3`.

4. **Default party state.** `Party::Party()` (`engines/mm/xeen/party.cpp:207`) initializes `_mazeId=0`, `_mazeDirection=DIR_NORTH`, etc. `SavesManager::newGame()` (`engines/mm/xeen/saves.cpp:212`) creates the `SaveArchive` (`_xeenSave`/`_darkSave`), assigns `_files->_currentSave`, calls `_currentSave->loadParty()` (which deserializes the default `MAZE.PTY` and overwrites `_mazeId`/`_mazePosition`/`_mazeDirection`), and resets blacksmith wares. The harness must call `newGame()` *first* and then override the position/maze fields.

5. **Render flow.** `XeenEngine::play()` (`engines/mm/xeen/xeen.cpp:267`) demonstrates the in-game bootstrap:
   ```cpp
   _files->setGameCc(0);
   _interface->setup();
   _screen->loadBackground("back.raw");
   _screen->loadPalette("mm4.pal");
   _map->clearMaze();
   _map->load(_party->_mazeId);
   _interface->startup();          // calls draw3d(false) internally
   (*_windows)[0].update();
   _interface->mainIconsPrint();
   (*_windows)[0].update();
   _screen->fadeIn();
   gameLoop();
   ```
   `Interface::startup()` (`engines/mm/xeen/interface.cpp:294`) calls `draw3d(false)` which already calls `drawScene()` → `drawIndoors()`/`drawOutdoors()`. So after `startup()` + window updates, the frame is fully rendered into `_screen` (a `Graphics::Screen` extending `ManagedSurface`).

6. **PNG output.** `Image::writePNG(WriteStream &, const Graphics::Surface &, const byte *palette[768])` is available in `image/png.h`. `config.mk` confirms `USE_PNG = 1`. `_screen->rawSurface()` (`graphics/managed_surface.h:195`) returns the underlying `Graphics::Surface`. The active palette is fetched via `g_system->getPaletteManager()->grabPalette(buf, 0, 256)`.

7. **Map ID semantics.** Maps live on either side: Clouds (`_loadCcNum=0`) or Dark Side (`_loadCcNum=1`). `Map::load(mapId)` uses `_loadCcNum` to choose the right archive. Vertigo (the doc's example) is map 28 on the Clouds side. Default `_loadCcNum=0`.

8. **Build system.** Sources are listed in `engines/mm/module.mk` under `ifdef ENABLE_XEEN`. New `.o` files must be added there. The existing pre-built binary at `~/src/scummvm/scummvm` confirms `ENABLE_XEEN` is on.

9. **Headless / muting.** The user runs under `xvfb-run -a`. Audio is muted via existing flags `--music-mute --sfx-mute --speech-mute --music-driver=null`. No new code needed for headless support.

10. **Game data path.** GOG install at `/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/`. The path contains spaces — quote in shell. Pass to ScummVM via existing `--path=...`.

---

## File structure

| File | Action | Responsibility |
|------|--------|----------------|
| `base/commandLine.cpp` | Modify | Parse 5 new long CLI options into ConfMan (`mm-screenshot`, `mm-maze`, `mm-cell`, `mm-facing`, `mm-side`). Add usage text. |
| `engines/mm/xeen/screenshot_harness.h` | Create | Declare `MM::Xeen::ScreenshotHarness` (settings struct, `isEnabled()`, `run(XeenEngine*)`). |
| `engines/mm/xeen/screenshot_harness.cpp` | Create | Implement parse-from-ConfMan, validate ranges, bootstrap engine, override party, render, save PNG, set `_gameMode = GMODE_QUIT`. |
| `engines/mm/xeen/xeen.cpp` | Modify | In `XeenEngine::run()`, after `initialize()`, branch to `ScreenshotHarness::run(this)` when enabled; otherwise fall through to `outerGameLoop()`. |
| `engines/mm/module.mk` | Modify | Add `xeen/screenshot_harness.o` to `ENABLE_XEEN` block. |
| `HARNESS.md` (repo root) | Create | Document invocation contract: required CLI flags, exit codes, sample command, headless tips. |

No tests are added. ScummVM has no engine-level unit-test harness and the spec's acceptance test is a manual smoke test (run binary, open the PNG, eyeball it, run again with a different position, confirm a different PNG). Steps for that smoke test live in Task 7.

---

## Acceptance criteria recap (from spec)

- Caller passes maze ID, X, Y, facing, output PNG path; ScummVM loads World of Xeen, teleports the party, renders the first-person scene, writes the PNG, exits 0.
- Headless under `xvfb-run -a`. No interactive prompts. No autosave / file side effects.
- Same input → same output bytes (no time-driven animation in the rendered frame).
- Sub-10s per invocation.
- Non-zero exit + clear stderr on bad input.

---

## Task 1: Add CLI options to commandLine.cpp

**Files:**
- Modify: `base/commandLine.cpp` (option parser block ending around line 1066, and usage text around line 161)

- [ ] **Step 1: Add usage documentation lines for the new flags**

Find the `kUsageStringTemplate` (search for `"  --debugflags=FLAGS"` near line 161). Add the following block immediately after the line that documents `--debugflags`. Match the existing two-space indent and column alignment:

```c
"  --mm-screenshot=PATH     [MM/Xeen] Render one in-game frame to PATH and exit.\n"
"                           Requires --mm-maze, --mm-cell, --mm-facing.\n"
"  --mm-maze=N              [MM/Xeen] Maze ID for the harness frame (1-128).\n"
"  --mm-cell=X,Y            [MM/Xeen] Party cell coordinates (each 0-15).\n"
"  --mm-facing=N|E|S|W      [MM/Xeen] Party facing direction.\n"
"  --mm-side=0|1            [MM/Xeen] World of Xeen side (0=Clouds default, 1=Dark).\n"
```

- [ ] **Step 2: Add option parsing in parseCommandLine()**

In `parseCommandLine()` (the function whose definition starts around line 637 and whose option block ends right before `unknownOption:` at line 1073), insert the following options *just before* `unknownOption:` (i.e. after the `DO_LONG_OPTION_BOOL("console")` block on Windows, and after `DO_LONG_OPTION("start-movie")`). They are placed at the bottom intentionally so they do not appear in the middle of unrelated logic:

```c
			DO_LONG_OPTION_PATH("mm-screenshot")
			END_OPTION

			DO_LONG_OPTION_INT("mm-maze")
			END_OPTION

			DO_LONG_OPTION("mm-cell")
				// Parse "X,Y" into mm_cell_x and mm_cell_y; we keep the raw value
				// in settings as well so the engine can re-validate.
				Common::StringTokenizer tokenizer(option, ",");
				if (tokenizer.empty())
					usage("Invalid --mm-cell value: %s (expected X,Y)", option);
				Common::String xs = tokenizer.nextToken();
				if (tokenizer.empty())
					usage("Invalid --mm-cell value: %s (expected X,Y)", option);
				Common::String ys = tokenizer.nextToken();
				settings["mm-cell-x"] = xs;
				settings["mm-cell-y"] = ys;
				settings.erase("mm-cell");
			END_OPTION

			DO_LONG_OPTION("mm-facing")
			END_OPTION

			DO_LONG_OPTION_INT("mm-side")
			END_OPTION
```

Notes:
- `DO_LONG_OPTION_PATH` is the right macro for the screenshot path (it already exists; see `screenshotpath` at line 766) so the value is normalised the same way as other path settings.
- `DO_LONG_OPTION_INT` validates the value as an integer.
- `DO_LONG_OPTION` accepts a free-form string we will validate inside the engine.
- The keys land in ConfMan as `mm-screenshot`, `mm-maze`, `mm-cell-x`, `mm-cell-y`, `mm-facing`, `mm-side` (note the dashes — ScummVM keeps them as-is in the StringMap; the engine looks them up with the same dashed names).

- [ ] **Step 3: Verify the file still compiles syntactically by re-running make on just commandLine**

Run:
```bash
cd ~/src/scummvm && make base/commandLine.o
```
Expected: compilation succeeds. If it fails with macro errors, re-check the `END_OPTION` placement and that the `option` local variable is in scope (the macros expose it).

- [ ] **Step 4: Commit**

```bash
cd ~/src/scummvm && git add base/commandLine.cpp && git commit -m "BASE: Add MM/Xeen screenshot harness CLI options

Adds --mm-screenshot, --mm-maze, --mm-cell, --mm-facing, --mm-side
long options so the MM/Xeen engine can be invoked in a one-shot
reference-screenshot mode driven by an external test harness."
```

---

## Task 2: Create the screenshot_harness header

**Files:**
- Create: `engines/mm/xeen/screenshot_harness.h`

- [ ] **Step 1: Write the header**

Create `engines/mm/xeen/screenshot_harness.h` with the following exact content (it includes the standard ScummVM license header and matches the project's `#ifndef`/`namespace` conventions):

```cpp
/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef MM_XEEN_SCREENSHOT_HARNESS_H
#define MM_XEEN_SCREENSHOT_HARNESS_H

#include "common/path.h"
#include "common/scummsys.h"
#include "mm/xeen/party.h"

namespace MM {
namespace Xeen {

class XeenEngine;

/**
 * One-shot reference-screenshot harness for MM4/Xeen.
 *
 * When enabled via CLI flags (--mm-screenshot, --mm-maze, --mm-cell,
 * --mm-facing, optional --mm-side), the harness bypasses the intro and
 * main menu, teleports the party to the requested cell, renders a single
 * first-person frame, writes it as a PNG, and exits.
 *
 * Designed to be invoked by an external test harness that diff-compares
 * the produced PNGs against an independently rendered set.
 */
class ScreenshotHarness {
public:
	struct Settings {
		Common::Path screenshotPath;
		uint8 mazeId;
		uint8 cellX;
		uint8 cellY;
		Direction facing;
		uint8 side; // 0 = Clouds, 1 = Dark Side

		Settings() : mazeId(0), cellX(0), cellY(0), facing(DIR_NORTH), side(0) {}
	};

	/**
	 * @returns true if --mm-screenshot was supplied on the command line.
	 */
	static bool isEnabled();

	/**
	 * Parse and validate harness settings from the active ConfMan domain.
	 * @param out  Filled in on success.
	 * @param err  On failure, populated with a human-readable error message.
	 * @returns true on success.
	 */
	static bool parseSettings(Settings &out, Common::String &err);

	/**
	 * Run the harness end-to-end. Drives engine bootstrap, teleports the
	 * party, renders one frame, writes the PNG, and signals the engine to
	 * quit.
	 *
	 * @param vm  The active XeenEngine instance.
	 * @returns 0 on success, non-zero on failure (suitable for use as the
	 *          process exit code via Common::Error / direct mapping).
	 */
	static int run(XeenEngine *vm);
};

} // End of namespace Xeen
} // End of namespace MM

#endif
```

- [ ] **Step 2: Commit**

```bash
cd ~/src/scummvm && git add engines/mm/xeen/screenshot_harness.h && git commit -m "MM: Declare Xeen screenshot harness interface"
```

---

## Task 3: Implement settings parsing

**Files:**
- Create: `engines/mm/xeen/screenshot_harness.cpp` (parsing/validation portion only — full file is finished in Task 4)

- [ ] **Step 1: Write the file with parsing only**

Create `engines/mm/xeen/screenshot_harness.cpp` with this initial content. Task 4 will append the `run()` body; for now we only need the file to compile and the parsing logic to exist:

```cpp
/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "mm/xeen/screenshot_harness.h"

#include "common/config-manager.h"
#include "common/file.h"
#include "common/str.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "graphics/paletteman.h"
#include "image/png.h"
#include "mm/xeen/files.h"
#include "mm/xeen/interface.h"
#include "mm/xeen/map.h"
#include "mm/xeen/party.h"
#include "mm/xeen/saves.h"
#include "mm/xeen/screen.h"
#include "mm/xeen/window.h"
#include "mm/xeen/xeen.h"

namespace MM {
namespace Xeen {

bool ScreenshotHarness::isEnabled() {
	return ConfMan.hasKey("mm-screenshot") && !ConfMan.get("mm-screenshot").empty();
}

static bool parseFacing(const Common::String &raw, Direction &out) {
	if (raw.size() != 1)
		return false;
	switch (raw[0]) {
	case 'N': case 'n': out = DIR_NORTH; return true;
	case 'E': case 'e': out = DIR_EAST;  return true;
	case 'S': case 's': out = DIR_SOUTH; return true;
	case 'W': case 'w': out = DIR_WEST;  return true;
	default: return false;
	}
}

bool ScreenshotHarness::parseSettings(Settings &out, Common::String &err) {
	if (!ConfMan.hasKey("mm-screenshot") || ConfMan.get("mm-screenshot").empty()) {
		err = "--mm-screenshot=PATH is required";
		return false;
	}
	out.screenshotPath = Common::Path::fromConfig(ConfMan.get("mm-screenshot"));

	if (!ConfMan.hasKey("mm-maze")) {
		err = "--mm-maze=N is required";
		return false;
	}
	int maze = ConfMan.getInt("mm-maze");
	if (maze < 1 || maze > 128) {
		err = Common::String::format("--mm-maze=%d out of range (expected 1-128)", maze);
		return false;
	}
	out.mazeId = (uint8)maze;

	if (!ConfMan.hasKey("mm-cell-x") || !ConfMan.hasKey("mm-cell-y")) {
		err = "--mm-cell=X,Y is required";
		return false;
	}
	int x = atoi(ConfMan.get("mm-cell-x").c_str());
	int y = atoi(ConfMan.get("mm-cell-y").c_str());
	if (x < 0 || x > 15 || y < 0 || y > 15) {
		err = Common::String::format("--mm-cell=%d,%d out of range (each 0-15)", x, y);
		return false;
	}
	out.cellX = (uint8)x;
	out.cellY = (uint8)y;

	if (!ConfMan.hasKey("mm-facing")) {
		err = "--mm-facing=N|E|S|W is required";
		return false;
	}
	if (!parseFacing(ConfMan.get("mm-facing"), out.facing)) {
		err = Common::String::format(
			"--mm-facing=%s invalid (expected N, E, S, or W)",
			ConfMan.get("mm-facing").c_str());
		return false;
	}

	out.side = 0;
	if (ConfMan.hasKey("mm-side")) {
		int s = ConfMan.getInt("mm-side");
		if (s != 0 && s != 1) {
			err = Common::String::format("--mm-side=%d invalid (expected 0 or 1)", s);
			return false;
		}
		out.side = (uint8)s;
	}

	return true;
}

int ScreenshotHarness::run(XeenEngine *vm) {
	// Implemented in Task 4.
	(void)vm;
	return 0;
}

} // End of namespace Xeen
} // End of namespace MM
```

- [ ] **Step 2: Add to module.mk so the new file builds (Task 6 also updates this)**

Edit `engines/mm/module.mk`. Inside the `ifdef ENABLE_XEEN` block, add `xeen/screenshot_harness.o \` immediately before `xeen/xeen.o` so it stays alphabetically grouped near other top-level Xeen sources. The new line:
```make
	xeen/screenshot_harness.o \
```

- [ ] **Step 3: Build the new object file to confirm compilation**

Run:
```bash
cd ~/src/scummvm && make engines/mm/xeen/screenshot_harness.o
```
Expected: compiles cleanly. Fix any header path issues (e.g. `graphics/paletteman.h` vs `graphics/palette.h` — if the include fails, replace with `graphics/palette.h` and use `g_system->getPaletteManager()` directly without including paletteman explicitly, since `xeen.h` transitively pulls in `common/system.h`).

- [ ] **Step 4: Commit**

```bash
cd ~/src/scummvm && git add engines/mm/xeen/screenshot_harness.cpp engines/mm/module.mk && git commit -m "MM: Implement Xeen screenshot harness settings parsing"
```

---

## Task 4: Implement the harness execution path

**Files:**
- Modify: `engines/mm/xeen/screenshot_harness.cpp` (replace the stub `run()` body)

- [ ] **Step 1: Replace the stub `run()` with the full implementation**

In `engines/mm/xeen/screenshot_harness.cpp`, replace the placeholder `run()` body from Task 3 with the implementation below. This mirrors `XeenEngine::play()` but stops after one rendered frame.

Replace this stub:
```cpp
int ScreenshotHarness::run(XeenEngine *vm) {
	// Implemented in Task 4.
	(void)vm;
	return 0;
}
```

with:

```cpp
int ScreenshotHarness::run(XeenEngine *vm) {
	Settings s;
	Common::String err;
	if (!parseSettings(s, err)) {
		warning("Screenshot harness: %s", err.c_str());
		return 1;
	}

	// Refuse to run on anything other than World of Xeen — the spec only
	// targets WoX, and Clouds/DarkSide alone might not have both archives.
	if (vm->getGameID() != GType_WorldOfXeen) {
		warning("Screenshot harness: target must be World of Xeen (got gameID=%u)",
			vm->getGameID());
		return 1;
	}

	// --- bootstrap (mirrors XeenEngine::playGame() + play() up to gameLoop) ---
	vm->_files->setGameCc(0);
	vm->_sound->stopAllAudio();
	SpriteResource::setClippedBottom(140);

	vm->_interface->setup();
	vm->_screen->loadBackground("back.raw");
	vm->_screen->loadPalette("mm4.pal");

	// Initialise save archives + default party so _files->_currentSave is
	// non-null and the active party is populated. This loads the on-disk
	// MAZE.PTY which sets _party->_mazeId/_mazePosition/_mazeDirection — we
	// override these immediately below.
	vm->_saves->newGame();

	// Honour the requested side and maze.
	vm->_map->clearMaze();
	vm->_map->_loadCcNum = s.side;
	vm->_party->_mazeId = s.mazeId;
	vm->_party->_mazePosition = Common::Point(s.cellX, s.cellY);
	vm->_party->_mazeDirection = s.facing;
	vm->_party->_priorMazeId = s.mazeId;

	// Load the requested map. If the underlying data files are missing this
	// will warning() and abort via error() inside Map::load — we cannot
	// recover from that gracefully, but the user will see the failure on
	// stderr and the process will exit non-zero, which satisfies the spec.
	vm->_map->load(s.mazeId);

	// Verify Map::load actually loaded the requested maze. If the requested
	// mazeId is invalid, _mazeData[0]._mazeId will not equal s.mazeId.
	if (vm->_map->mazeData()._mazeId != s.mazeId) {
		warning("Screenshot harness: maze %u failed to load (loaded id=%d)",
			s.mazeId, vm->_map->mazeData()._mazeId);
		return 1;
	}

	// Re-apply the requested position — Map::load can mutate party state
	// (e.g. wrap-around or fall-through cells). We want exactly the cell
	// the caller asked for.
	vm->_party->_mazePosition = Common::Point(s.cellX, s.cellY);
	vm->_party->_mazeDirection = s.facing;

	// Run the same first-frame setup that XeenEngine::play() runs.
	vm->_mode = MODE_INTERACTIVE;
	vm->_interface->startup();
	(*vm->_windows)[0].update();
	vm->_interface->mainIconsPrint();
	(*vm->_windows)[0].update();

	// Apply the palette directly without animating a fade — gives a fully
	// lit frame immediately and keeps the harness deterministic.
	vm->_screen->fadeIn();

	// Re-draw scene + HUD now that the palette is live, so the captured
	// surface matches what a player would see at this position.
	vm->_interface->draw3d(true, false);
	vm->_interface->mainIconsPrint();
	(*vm->_windows)[0].update();

	// --- save the frame ---
	Common::DumpFile out;
	if (!out.open(s.screenshotPath)) {
		warning("Screenshot harness: cannot open '%s' for writing",
			s.screenshotPath.toString(Common::Path::kNativeSeparator).c_str());
		return 1;
	}

	byte palette[256 * 3];
	g_system->getPaletteManager()->grabPalette(palette, 0, 256);

	if (!Image::writePNG(out, vm->_screen->rawSurface(), palette)) {
		warning("Screenshot harness: writePNG failed for '%s'",
			s.screenshotPath.toString(Common::Path::kNativeSeparator).c_str());
		return 1;
	}

	out.close();
	debug("Screenshot harness: wrote %s",
		s.screenshotPath.toString(Common::Path::kNativeSeparator).c_str());

	// Cause outerGameLoop to exit cleanly.
	vm->_gameMode = GMODE_QUIT;
	return 0;
}
```

- [ ] **Step 2: Build the engine module**

```bash
cd ~/src/scummvm && make engines/mm/xeen/screenshot_harness.o
```
Expected: compiles. If `(*vm->_windows)[0]` does not compile (because `Windows` is forward-declared), add `#include "mm/xeen/window.h"` at the top of the file (it is already in the include list above, but double-check). If `vm->_screen->rawSurface()` is rejected, replace with `*vm->_screen` — `Image::writePNG` accepts `const Graphics::Surface &` and `Graphics::Screen` is-a `ManagedSurface` is-a `Surface` via `rawSurface()`.

- [ ] **Step 3: Commit**

```bash
cd ~/src/scummvm && git add engines/mm/xeen/screenshot_harness.cpp && git commit -m "MM: Implement Xeen screenshot harness render-and-save path

Bootstraps the engine just enough to render a single first-person
frame at a caller-specified party position and writes it as a 320x200
paletted PNG, then signals the engine to quit."
```

---

## Task 5: Hook the harness into XeenEngine::run()

**Files:**
- Modify: `engines/mm/xeen/xeen.cpp` (the `run()` function around line 191)

- [ ] **Step 1: Add the include**

At the top of `engines/mm/xeen/xeen.cpp`, in the include block (after `#include "mm/xeen/resources.h"`), add:
```cpp
#include "mm/xeen/screenshot_harness.h"
```

- [ ] **Step 2: Branch on harness mode in `run()`**

Replace the existing `XeenEngine::run()`:
```cpp
Common::Error XeenEngine::run() {
	if (initialize())
		outerGameLoop();

	return Common::kNoError;
}
```

with:
```cpp
Common::Error XeenEngine::run() {
	if (!initialize())
		return Common::kNoError;

	if (ScreenshotHarness::isEnabled()) {
		int rc = ScreenshotHarness::run(this);
		// Map non-zero rc to a ScummVM error so the process exits non-zero.
		return rc == 0 ? Common::kNoError : Common::kUnknownError;
	}

	outerGameLoop();
	return Common::kNoError;
}
```

- [ ] **Step 3: Build the engine**

```bash
cd ~/src/scummvm && make -j$(nproc) 2>&1 | tail -40
```
Expected: full link succeeds, producing an updated `~/src/scummvm/scummvm` binary. Any compile errors in `xeen.cpp` are likely include-order issues — the `screenshot_harness.h` include must come after `mm/xeen/xeen.h` is *not* needed (the harness header forward-declares `XeenEngine`), so include order is flexible.

- [ ] **Step 4: Commit**

```bash
cd ~/src/scummvm && git add engines/mm/xeen/xeen.cpp && git commit -m "XEEN: Wire screenshot harness into XeenEngine::run

When --mm-screenshot is provided on the command line, bypass the
intro and main menu, drive the harness, and exit with a non-zero
status if rendering or saving fails."
```

---

## Task 6: Update module.mk (if not already complete)

**Files:**
- Modify: `engines/mm/module.mk`

- [ ] **Step 1: Confirm the harness object is in the build list**

Open `engines/mm/module.mk` and confirm that inside the `ifdef ENABLE_XEEN ... endif` block the line
```make
	xeen/screenshot_harness.o \
```
appears (alphabetically near `xeen/screen.o`/`xeen/scripts.o` is fine — the build does not care about order, only about being inside the block and ending with a backslash-continued line). If Task 3 already added it, no change needed.

- [ ] **Step 2: Skip commit if no change**

If the file is unchanged from Task 3, skip this step. Otherwise:
```bash
cd ~/src/scummvm && git add engines/mm/module.mk && git commit -m "MM: Build screenshot_harness.o under ENABLE_XEEN"
```

---

## Task 7: Smoke-test the harness end-to-end

**Files:**
- None modified; this task verifies the binary works.

- [ ] **Step 1: Make sure the build is up-to-date**

```bash
cd ~/src/scummvm && make -j$(nproc) 2>&1 | tail -10
```
Expected: build succeeds.

- [ ] **Step 2: Confirm xvfb-run is available**

```bash
which xvfb-run
```
Expected: a path. If absent, install with `sudo apt install xvfb`.

- [ ] **Step 3: First reference render (Vertigo, cell 8,8, facing North)**

```bash
rm -f /tmp/vertigo-8-8-n.png
xvfb-run -a ~/src/scummvm/scummvm \
    --path="/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/" \
    --music-driver=null \
    --music-mute --sfx-mute --speech-mute \
    --mm-screenshot=/tmp/vertigo-8-8-n.png \
    --mm-maze=28 --mm-cell=8,8 --mm-facing=N \
    worldofxeen
echo "exit=$?"
ls -l /tmp/vertigo-8-8-n.png
file /tmp/vertigo-8-8-n.png
```
Expected: `exit=0`, the PNG file exists, `file` reports `PNG image data, 320 x 200, 8-bit colormap`.

If exit is non-zero, look at the warning() output on stderr — most likely causes are: invalid map id (data files for that side missing for WoX path), bad path (case-sensitivity on the GOG install), or PNG write failure (path not writable). Fix and re-run.

- [ ] **Step 4: Second reference render (Vertigo, cell 15,8, facing West)**

```bash
rm -f /tmp/vertigo-15-8-w.png
xvfb-run -a ~/src/scummvm/scummvm \
    --path="/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/" \
    --music-driver=null \
    --music-mute --sfx-mute --speech-mute \
    --mm-screenshot=/tmp/vertigo-15-8-w.png \
    --mm-maze=28 --mm-cell=15,8 --mm-facing=W \
    worldofxeen
echo "exit=$?"
ls -l /tmp/vertigo-15-8-w.png
file /tmp/vertigo-15-8-w.png
```
Expected: `exit=0`, PNG file exists, 320x200.

- [ ] **Step 5: Confirm the two PNGs differ**

```bash
md5sum /tmp/vertigo-8-8-n.png /tmp/vertigo-15-8-w.png
```
Expected: two different md5 sums. If they match, the render path likely is not picking up the position override — re-check that `_party->_mazePosition` is set after `_map->load()` (Task 4 step 1 already does this).

- [ ] **Step 6: Determinism check — run the same invocation twice**

```bash
rm -f /tmp/det1.png /tmp/det2.png
xvfb-run -a ~/src/scummvm/scummvm \
    --path="/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/" \
    --music-driver=null --music-mute --sfx-mute --speech-mute \
    --mm-screenshot=/tmp/det1.png --mm-maze=28 --mm-cell=8,8 --mm-facing=N \
    worldofxeen
xvfb-run -a ~/src/scummvm/scummvm \
    --path="/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/" \
    --music-driver=null --music-mute --sfx-mute --speech-mute \
    --mm-screenshot=/tmp/det2.png --mm-maze=28 --mm-cell=8,8 --mm-facing=N \
    worldofxeen
md5sum /tmp/det1.png /tmp/det2.png
```
Expected: identical md5 sums. If they differ, document the noise source (likely an animation phase) in `HARNESS.md` Task 8 instead of trying to chase it — the spec explicitly allows the calling harness to tolerate animation noise as a soft requirement.

- [ ] **Step 7: Failure-path check — bad facing should exit non-zero**

```bash
xvfb-run -a ~/src/scummvm/scummvm \
    --path="/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/" \
    --music-driver=null --music-mute --sfx-mute --speech-mute \
    --mm-screenshot=/tmp/should-not-exist.png \
    --mm-maze=28 --mm-cell=8,8 --mm-facing=Q \
    worldofxeen
echo "exit=$?"
```
Expected: non-zero exit with a clear stderr message about invalid facing.

- [ ] **Step 8: Failure-path check — out-of-range maze ID**

```bash
xvfb-run -a ~/src/scummvm/scummvm \
    --path="/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/" \
    --music-driver=null --music-mute --sfx-mute --speech-mute \
    --mm-screenshot=/tmp/should-not-exist2.png \
    --mm-maze=999 --mm-cell=8,8 --mm-facing=N \
    worldofxeen
echo "exit=$?"
```
Expected: non-zero exit with a clear stderr message about the range.

- [ ] **Step 9: No commit — this is a verification step. If any of steps 3-8 fail, fix the underlying issue (most likely in `screenshot_harness.cpp` Task 4) and amend the relevant commit, then re-run all smoke tests. Do not move on to Task 8 until all steps in Task 7 pass.**

---

## Task 8: Document the invocation contract

**Files:**
- Create: `HARNESS.md` (at `~/src/scummvm/HARNESS.md`)

- [ ] **Step 1: Write the doc**

Create `~/src/scummvm/HARNESS.md` with the following content:

```markdown
# MM4/Xeen Reference-Screenshot Harness

This fork of ScummVM adds a one-shot rendering mode to the MM4/World of
Xeen engine for use as a ground-truth source by the mm5e Go re-port's
pixel-diff regression suite.

## Invocation

```bash
xvfb-run -a ~/src/scummvm/scummvm \
    --path="/mnt/c/Program Files (x86)/GOG Galaxy/Games/Might and Magic 4-5/" \
    --music-driver=null --music-mute --sfx-mute --speech-mute \
    --mm-screenshot=/tmp/out.png \
    --mm-maze=28 --mm-cell=8,8 --mm-facing=N \
    worldofxeen
```

## Flags

| Flag | Required | Format | Description |
|------|----------|--------|-------------|
| `--mm-screenshot` | yes | absolute filesystem path | Output PNG. Must be writable. Presence enables harness mode. |
| `--mm-maze` | yes | integer 1-128 | Map ID. |
| `--mm-cell` | yes | `X,Y` (each 0-15) | Party cell coordinates. |
| `--mm-facing` | yes | `N`, `E`, `S`, or `W` | Party facing direction. |
| `--mm-side` | no (default 0) | `0` or `1` | World of Xeen side: 0=Clouds, 1=Dark Side. |

## Behaviour

* Bypasses intro, copy protection, and the main menu.
* Boots into World of Xeen with the default `MAZE.PTY` party (positions overridden).
* Loads the requested map, teleports the party, and renders one first-person frame.
* Writes a 320x200 8-bit paletted PNG (the entire game window — viewport, HUD, and minimap).
  The calling harness should crop to the 3D viewport region (top-left 224x140) if it wants
  viewport-only comparison.
* Exits with status 0 on success and non-zero on any failure (bad arguments, map load
  failure, PNG write failure). Stderr carries a `WARNING:` line describing the cause.

## Determinism

Animation frames are reset to phase 0 by the engine's `_isAnimReset` mechanism the first
time `drawScene()` runs. The harness renders before any animation tick advances, so two
invocations with identical inputs produce identical PNGs (verified in the smoke test).
If you observe per-run pixel noise, it is most likely from a sprite whose initial frame
depends on the system clock; the calling test suite should tolerate it via a fuzziness
threshold.

## Headless

The harness runs unattended under `xvfb-run -a`. Mute audio with the standard ScummVM
flags shown above. No autosave is written. No prompts are emitted.

## Performance

A single invocation including ScummVM startup is sub-10s on a modest workstation. For
large regression suites, consider invoking N renders in parallel with `xargs -P` —
each invocation is a self-contained process with no shared state.

## Source pointers (for maintainers)

* CLI option parsing: `base/commandLine.cpp` (search for `mm-screenshot`).
* Harness module: `engines/mm/xeen/screenshot_harness.{h,cpp}`.
* Engine hook: `engines/mm/xeen/xeen.cpp`, in `XeenEngine::run()`.
```

- [ ] **Step 2: Commit**

```bash
cd ~/src/scummvm && git add HARNESS.md && git commit -m "Document MM/Xeen screenshot harness usage"
```

---

## Done

After Task 8, the user can run the invocation in `HARNESS.md` and consume the produced PNGs from `mm5e`. No code remains to write.

If the smoke tests in Task 7 surfaced a determinism issue that could not be eliminated, add a follow-up task here documenting which animation source is responsible (most likely candidate: water shimmer or torch flicker controlled by `_flipUIFrame`/`_flipWater` in `Interface::draw3d`).
