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

#ifndef XEEN_SCREENSHOT_HARNESS_H
#define XEEN_SCREENSHOT_HARNESS_H

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
