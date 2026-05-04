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

#include "kyra/engine/screenshot_harness.h"

#include <stdlib.h>

#include "common/config-manager.h"
#include "common/file.h"
#include "common/str.h"
#include "common/textconsole.h"
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
		exit(1);
	}

	// Filled in by Task A3.
	(void)vm;
	exit(1);
}

} // End of namespace Kyra
