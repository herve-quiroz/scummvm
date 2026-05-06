// The harness is a one-shot CLI mode: we bypass ScummVM's normal
// engine-completion path (which would show a launcher or modal error
// dialog) by calling _exit() directly with our chosen status code.
// _exit skips atexit hooks so we don't deadlock on SDL2/audio thread
// destructors that the engine's own message loop normally drains before
// teardown.
#define FORBIDDEN_SYMBOL_EXCEPTION_exit

// Pull <unistd.h> in before "common/forbidden.h" so its declarations of
// chdir/getcwd/etc. don't collide with the forbidden-symbol macros.
#include <unistd.h>

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

#include "kyra/engine/screenshot_harness.h"

#include <stdlib.h>

#include "common/config-manager.h"
#include "common/file.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "graphics/surface.h"
#include "image/png.h"
#include "kyra/detection.h"
#include "kyra/engine/eobcommon.h"
#include "kyra/graphics/screen.h"

namespace Kyra {

bool ScreenshotHarness::isEnabled() {
	return ConfMan.hasKey("screenshot") && !ConfMan.get("screenshot").empty();
}

static bool parseFacing(const Common::String &raw, uint8 &out) {
	if (raw.size() != 1)
		return false;
	switch (raw[0]) {
	case 'N': case 'n': out = 0; return true;
	case 'E': case 'e': out = 1; return true;
	case 'S': case 's': out = 2; return true;
	case 'W': case 'w': out = 3; return true;
	default: return false;
	}
}

bool ScreenshotHarness::parseSettings(Settings &out, Common::String &err) {
	if (!ConfMan.hasKey("screenshot") || ConfMan.get("screenshot").empty()) {
		err = "--screenshot=PATH is required";
		return false;
	}
	out.screenshotPath = Common::Path::fromCommandLine(ConfMan.get("screenshot"));

	// Save slot is optional. ScummVM's standard --save-slot=N (alias -x N)
	// populates ConfMan["save_slot"] (default -1 meaning "no save"). When
	// set, we restore that slot instead of doing a fresh bootstrap, and
	// level/cell/facing become optional overrides.
	out.saveSlot = -1;
	if (ConfMan.hasKey("save_slot")) {
		int slot = ConfMan.getInt("save_slot");
		if (slot >= 0)
			out.saveSlot = slot;
	}
	const bool haveSave = out.saveSlot >= 0;

	out.haveLevel = ConfMan.hasKey("level");
	if (out.haveLevel) {
		int level = ConfMan.getInt("level");
		if (level < 1 || level > 16) {
			err = Common::String::format("--level=%d out of range (expected 1-16)", level);
			return false;
		}
		out.level = (uint8)level;
	} else if (!haveSave) {
		err = "--level=N is required (or pass --save-slot=N)";
		return false;
	}

	const bool haveCellX = ConfMan.hasKey("cell_x");
	const bool haveCellY = ConfMan.hasKey("cell_y");
	out.haveCell = haveCellX && haveCellY;
	if (out.haveCell) {
		int x = atoi(ConfMan.get("cell_x").c_str());
		int y = atoi(ConfMan.get("cell_y").c_str());
		if (x < 0 || x > 31 || y < 0 || y > 31) {
			err = Common::String::format("--cell=%d,%d out of range (each 0-31)", x, y);
			return false;
		}
		out.cellX = (uint8)x;
		out.cellY = (uint8)y;
	} else if (haveCellX != haveCellY) {
		err = "--cell=X,Y must specify both X and Y";
		return false;
	} else if (!haveSave) {
		err = "--cell=X,Y is required (or pass --save-slot=N)";
		return false;
	}

	out.haveFacing = ConfMan.hasKey("facing");
	if (out.haveFacing) {
		if (!parseFacing(ConfMan.get("facing"), out.facing)) {
			err = Common::String::format(
				"--facing=%s invalid (expected N, E, S, or W)",
				ConfMan.get("facing").c_str());
			return false;
		}
	} else if (!haveSave) {
		err = "--facing=N|E|S|W is required (or pass --save-slot=N)";
		return false;
	}

	out.noActors = ConfMan.hasKey("no_actors") && ConfMan.getBool("no_actors");

	return true;
}

void ScreenshotHarness::run(EoBCoreEngine *vm) {
	Settings s;
	Common::String err;
	if (!parseSettings(s, err)) {
		warning("Screenshot harness: %s", err.c_str());
		_exit(1);
	}

	// Refuse to run on anything other than EOB2. The renderer paths
	// and resource layout for EOB1 differ enough that sharing one
	// harness would be brittle.
	if (vm->_flags.gameID != GI_EOB2) {
		warning("Screenshot harness: target must be Eye of the Beholder II "
			"(got gameID=%d)", vm->_flags.gameID);
		_exit(1);
	}

	// --- bootstrap ---
	// Two paths depending on whether a save slot was requested:
	//
	//   * No save (the default): mirror EoBEngine::startupNew, then load
	//     the requested level fresh. Skip intro / main menu / character
	//     creation. Party state, monsters, decorations are at their
	//     level-load defaults.
	//
	//   * With --save-slot=N: restore the save's full state via
	//     loadGameState(). This brings back party, level, monster
	//     positions, decoration toggles (open doors, pulled levers,
	//     etc.) — anything the engine persists. After load, apply
	//     level/cell/facing overrides if those flags were given on the
	//     CLI; otherwise capture from the save's own position.

	if (s.saveSlot >= 0) {
		// startupLoad does the equivalent of resetting graphics/sound
		// state before the save read pulls things back in. Mirrors the
		// engine's own _gameToLoad path in EoBCoreEngine::go.
		vm->startupLoad();
		Common::Error loadErr = vm->loadGameState(s.saveSlot);
		if (loadErr.getCode() != Common::kNoError) {
			warning("Screenshot harness: failed to load save slot %d: %s",
				s.saveSlot, loadErr.getDesc().c_str());
			_exit(1);
		}

		// Apply post-load overrides. If the user specified a level
		// different from the save's, we have to call loadLevel to
		// switch the maze data. Cell and facing are simple state
		// updates that don't need extra calls.
		if (s.haveLevel && s.level != vm->_currentLevel) {
			vm->_currentLevel = s.level;
			vm->_currentSub = 0;
			vm->loadLevel(s.level, 0);
		}
		if (s.haveCell)
			vm->_currentBlock = (uint16)s.cellY * 32u + (uint16)s.cellX;
		if (s.haveFacing)
			vm->_currentDirection = s.facing;
	} else {
		// EoBCoreEngine::startupNew populates the party with a default
		// set of characters so portrait drawing has something to
		// render. Without this, _characters[] is zero-initialized and
		// the portrait code may crash or render garbage.
		vm->startupNew();

		vm->_currentLevel = s.level;
		vm->_currentSub = 0;
		vm->loadLevel(s.level, 0);
		vm->_currentBlock = (uint16)s.cellY * 32u + (uint16)s.cellX;
		vm->_currentDirection = s.facing;
		vm->setHandItem(0);
	}

	// Optional actor suppression. EOB2's monster table is _monsters[]
	// owned by EoBCoreEngine; setting block to 0 is the in-engine
	// "out of play" marker (see killMonster + placeMonster), so the
	// drawMonsters pass skips them without disturbing wall/decoration
	// draw paths.
	if (s.noActors) {
		for (int i = 0; i < 30; ++i) {
			vm->_monsters[i].block = 0;
		}
	}

	// --- draw the frame ---
	// drawScene() renders the 3D viewport into the back buffer.
	// gui_drawAllCharPortraitsWithStats() renders the right-side HUD.
	vm->drawScene(1);
	vm->gui_drawAllCharPortraitsWithStats();

	// Force the back buffer to the front so the page-0 surface contains
	// the composed frame. EOB's drawScene already calls copyRegion on
	// success, but the explicit update mirrors what a player would see
	// at the end of a normal frame.
	vm->_screen->updateScreen();

	// --- save the frame ---
	Common::DumpFile out;
	if (!out.open(s.screenshotPath)) {
		warning("Screenshot harness: cannot open '%s' for writing",
			s.screenshotPath.toString(Common::Path::kNativeSeparator).c_str());
		_exit(1);
	}

	// Read the palette from the engine's own copy rather than the SDL
	// backend. Under the dummy/offscreen drivers the backend returns
	// zeros, producing an all-black PNG. This is the same trap the
	// MM/Xeen harness hit; we avoid it the same way.
	//
	// Kyra stores 6-bit VGA palette internally; getRealPalette converts
	// to 8-bit RGB suitable for PNG output.
	byte palette[256 * 3];
	vm->_screen->getRealPalette(0, palette);

	// Wrap page 0 of the Screen as a Graphics::Surface for writePNG.
	// Page 0 is the visible front buffer; updateScreen() above made sure
	// it carries the freshly composed frame. getPagePtr() is protected,
	// so use the public copyRegionToBuffer() to grab the full page.
	byte pageBuf[Screen::SCREEN_W * Screen::SCREEN_H];
	vm->_screen->copyRegionToBuffer(0, 0, 0, Screen::SCREEN_W, Screen::SCREEN_H, pageBuf);

	Graphics::Surface surf;
	surf.init(Screen::SCREEN_W, Screen::SCREEN_H, Screen::SCREEN_W,
		pageBuf, Graphics::PixelFormat::createFormatCLUT8());

	if (!Image::writePNG(out, surf, palette)) {
		warning("Screenshot harness: writePNG failed for '%s'",
			s.screenshotPath.toString(Common::Path::kNativeSeparator).c_str());
		out.close();
		_exit(1);
	}

	out.close();
	debug("Screenshot harness: wrote %s",
		s.screenshotPath.toString(Common::Path::kNativeSeparator).c_str());

	_exit(0);
}

} // End of namespace Kyra
