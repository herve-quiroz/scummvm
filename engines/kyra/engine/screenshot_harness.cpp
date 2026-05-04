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

	if (!ConfMan.hasKey("level")) {
		err = "--level=N is required";
		return false;
	}
	int level = ConfMan.getInt("level");
	if (level < 1 || level > 16) {
		err = Common::String::format("--level=%d out of range (expected 1-16)", level);
		return false;
	}
	out.level = (uint8)level;

	if (!ConfMan.hasKey("cell_x") || !ConfMan.hasKey("cell_y")) {
		err = "--cell=X,Y is required";
		return false;
	}
	int x = atoi(ConfMan.get("cell_x").c_str());
	int y = atoi(ConfMan.get("cell_y").c_str());
	if (x < 0 || x > 31 || y < 0 || y > 31) {
		err = Common::String::format("--cell=%d,%d out of range (each 0-31)", x, y);
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

	// --- bootstrap (mirrors EoBEngine::startupNew + minimal startup) ---
	// We deliberately skip:
	//   - intro / title screen
	//   - main menu / character creation
	//   - importOrigSaves (handled by the caller's EoBCoreEngine::go path
	//     before this harness is reached, so already settled)
	//   - sound resource selection (engine is silenced via --music-driver=null)
	//
	// We do need:
	//   - level state: _currentLevel, _currentSub, loadLevel(level, 0)
	//   - party position: _currentBlock = y*32+x, _currentDirection
	//   - hand item slot (so portrait draw doesn't crash)
	//   - default party state from EoBCoreEngine::startupNew

	// EoBCoreEngine::startupNew populates the party with a default set
	// of characters so portrait drawing has something to render. Without
	// this, _characters[] is zero-initialized and the portrait code may
	// crash or render garbage. Run startupNew first so its own loadLevel
	// is overridden by ours below.
	vm->startupNew();

	vm->_currentLevel = s.level;
	vm->_currentSub = 0;
	vm->loadLevel(s.level, 0);
	vm->_currentBlock = (uint16)s.cellY * 32u + (uint16)s.cellX;
	vm->_currentDirection = s.facing;
	vm->setHandItem(0);

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
