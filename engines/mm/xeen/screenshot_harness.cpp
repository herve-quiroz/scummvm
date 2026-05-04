// The harness is a one-shot CLI mode: we bypass ScummVM's normal
// engine-completion path (which would show a launcher or modal error
// dialog) by calling _exit() directly with our chosen status code.
#define FORBIDDEN_SYMBOL_EXCEPTION_exit

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

#include <stdlib.h>

#include "common/config-manager.h"
#include "common/file.h"
#include "common/str.h"
#include "common/textconsole.h"
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
	return ConfMan.hasKey("screenshot") && !ConfMan.get("screenshot").empty();
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
	if (!ConfMan.hasKey("screenshot") || ConfMan.get("screenshot").empty()) {
		err = "--screenshot=PATH is required";
		return false;
	}
	out.screenshotPath = Common::Path::fromCommandLine(ConfMan.get("screenshot"));

	if (!ConfMan.hasKey("level")) {
		err = "--level=N is required";
		return false;
	}
	int maze = ConfMan.getInt("level");
	if (maze < 1 || maze > 128) {
		err = Common::String::format("--level=%d out of range (expected 1-128)", maze);
		return false;
	}
	out.mazeId = (uint8)maze;

	if (!ConfMan.hasKey("cell_x") || !ConfMan.hasKey("cell_y")) {
		err = "--cell=X,Y is required";
		return false;
	}
	int x = atoi(ConfMan.get("cell_x").c_str());
	int y = atoi(ConfMan.get("cell_y").c_str());
	if (x < 0 || x > 15 || y < 0 || y > 15) {
		err = Common::String::format("--cell=%d,%d out of range (each 0-15)", x, y);
		return false;
	}
	out.cellX = (uint8)x;
	out.cellY = (uint8)y;

	if (!ConfMan.hasKey("facing")) {
		err = "--facing=N|E|S|W is required";
		return false;
	}
	if (!parseFacing(ConfMan.get("facing"), out.facing)) {
		err = Common::String::format(
			"--facing=%s invalid (expected N, E, S, or W)",
			ConfMan.get("facing").c_str());
		return false;
	}

	out.side = 0;
	if (ConfMan.hasKey("mm_side")) {
		int s = ConfMan.getInt("mm_side");
		if (s != 0 && s != 1) {
			err = Common::String::format("--mm-side=%d invalid (expected 0 or 1)", s);
			return false;
		}
		out.side = (uint8)s;
	}

	out.noMonsters = ConfMan.hasKey("no_actors") && ConfMan.getBool("no_actors");

	return true;
}

int ScreenshotHarness::run(XeenEngine *vm) {
	Settings s;
	Common::String err;
	if (!parseSettings(s, err)) {
		warning("Screenshot harness: %s", err.c_str());
		exit(1);
	}

	// Refuse to run on anything other than World of Xeen — the spec only
	// targets WoX, and Clouds/DarkSide alone might not have both archives.
	if (vm->getGameID() != GType_WorldOfXeen) {
		warning("Screenshot harness: target must be World of Xeen (got gameID=%u)",
			vm->getGameID());
		exit(1);
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

	// Load the requested map. Missing data files still abort via error()
	// inside Map::load. We can't recover from that, but the user sees the
	// failure on stderr and the process exits non-zero, which the spec
	// allows.
	vm->_map->load(s.mazeId);

	// Verify Map::load picked up the requested maze. This catches the
	// "file present but mismatched maze ID" case (e.g. requesting maze 99
	// on the wrong side); a fully-missing data file would have aborted
	// inside Map::load above.
	if (vm->_map->mazeData()._mazeId != s.mazeId) {
		warning("Screenshot harness: maze %u failed to load (loaded id=%d)",
			s.mazeId, vm->_map->mazeData()._mazeId);
		exit(1);
	}

	// Re-apply the requested position — Map::load can mutate party state
	// (e.g. wrap-around or fall-through cells). We want exactly the cell
	// the caller asked for.
	vm->_party->_mazePosition = Common::Point(s.cellX, s.cellY);
	vm->_party->_mazeDirection = s.facing;

	// Drop monsters if requested, so the captured frame contains only the
	// static scene (walls, objects, wall items). The indoor/outdoor draw
	// paths both iterate _mobData._monsters and are no-ops on an empty
	// array, so this is the least invasive suppression point.
	if (s.noMonsters)
		vm->_map->_mobData._monsters.clear();

	// Run the same first-frame setup that XeenEngine::play() runs.
	vm->_mode = MODE_INTERACTIVE;
	// outerGameLoop normally clears _gameMode to GMODE_NONE before play()
	// runs. We bypass outerGameLoop, so do it here, otherwise shouldExit()
	// returns true and Screen::fadeInner exits before applying the palette
	// to _mainPalette (leaving the captured PNG black).
	vm->_gameMode = GMODE_NONE;
	vm->_interface->startup();
	(*vm->_windows)[0].update();
	vm->_interface->mainIconsPrint();
	(*vm->_windows)[0].update();

	// Apply the loaded palette via the engine's normal fade-in routine.
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
		exit(1);
	}

	// Read the palette from the engine's own copy rather than from the
	// SDL backend. Under the dummy / offscreen video drivers ScummVM uses
	// for headless harness runs, getPaletteManager()->grabPalette() returns
	// all zeros, which would produce a paletted PNG of pure black even
	// though the surface buffer is correctly populated.
	byte palette[256 * 3];
	vm->_screen->getMainPalette(palette);

	if (!Image::writePNG(out, vm->_screen->rawSurface(), palette)) {
		warning("Screenshot harness: writePNG failed for '%s'",
			s.screenshotPath.toString(Common::Path::kNativeSeparator).c_str());
		out.close();
		// Caller must check exit status; the partial PNG may be left on
		// disk but the non-zero exit signals it should not be trusted.
		exit(1);
	}

	out.close();
	debug("Screenshot harness: wrote %s",
		s.screenshotPath.toString(Common::Path::kNativeSeparator).c_str());

	// One-shot mode: skip ScummVM's launcher / error dialog by exiting
	// immediately. The successful PNG is the only artifact this run is
	// supposed to leave behind.
	exit(0);
}

} // End of namespace Xeen
} // End of namespace MM
