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

#ifndef KYRA_ENGINE_SCREENSHOT_HARNESS_H
#define KYRA_ENGINE_SCREENSHOT_HARNESS_H

#include "common/path.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Kyra {

class EoBCoreEngine;

/**
 * One-shot screenshot harness for the EOB2 (KYRA) engine.
 *
 * When enabled via CLI flags (--screenshot, --level, --cell, --facing,
 * optional --no-actors, optional --save-slot), the harness bypasses the
 * intro and main menu, loads the requested level (or restores a save
 * slot), teleports the party to the requested (block, facing), draws a
 * single frame, writes a 320x200 paletted PNG to disk, and exits with
 * status 0. On any error it prints a "WARNING: Screenshot harness: ..."
 * line to stderr and exits 1.
 *
 * If --save-slot=N is passed (ScummVM's standard `-x N` flag), the
 * harness restores that save instead of doing a fresh-game bootstrap.
 * --level/--cell/--facing become optional in that mode and act as
 * post-load overrides; omit them to capture the save's recorded
 * position. This is how state-dependent scenes (open doors, pulled
 * levers, scripted decoration changes) are captured for diffing.
 *
 * The harness only runs against EOB2 targets. EOB1 is rejected
 * because the renderer paths and resource layout differ enough that
 * sharing one harness would be brittle.
 *
 * The flag set is shared with MM/Xeen's screenshot_harness; engine
 * dispatch is by the loaded game target, not by the flag name.
 */
class ScreenshotHarness {
public:
	struct Settings {
		Common::Path screenshotPath;
		uint8 level;
		uint8 cellX;
		uint8 cellY;
		uint8 facing;   // 0 = N, 1 = E, 2 = S, 3 = W
		bool noActors;

		// Save slot to restore. -1 means "no save, fresh load".
		// When >= 0, level/cell/facing become optional overrides.
		int saveSlot;

		// Per-field "was this provided on the CLI?" flags. Only
		// meaningful when saveSlot >= 0; without a save, all four
		// fields are required and these are always true.
		bool haveLevel;
		bool haveCell;
		bool haveFacing;
	};

	/** @returns true if --screenshot was supplied on the command line. */
	static bool isEnabled();

	/**
	 * Parse and validate harness settings from ConfMan.
	 * @returns true on success. On failure, sets @p err to a
	 *          human-readable description.
	 */
	static bool parseSettings(Settings &out, Common::String &err);

	/**
	 * Execute the harness: bootstrap engine state, draw one frame,
	 * write PNG, exit. Never returns.
	 */
	static void run(EoBCoreEngine *vm);
};

} // End of namespace Kyra

#endif
