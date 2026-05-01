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
