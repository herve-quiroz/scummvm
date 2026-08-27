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

#include "common/array.h"
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

		// Canonical state snapshot output. Empty means "not requested".
		// When set, --screenshot becomes optional: a state dump needs no
		// rendered frame.
		Common::Path dumpStatePath;

		// Trigger-firing output. Empty means "not requested". Fires
		// every trigger on the level from a clean state and records
		// what each one changed.
		Common::Path fireTriggersPath;

		// Batch script. Empty means "not requested". Each line is one
		// capture, letting a whole level set be produced by a single
		// process. That matters because ScummVM's shutdown path stalls
		// for roughly 85 seconds after the harness finishes, so
		// per-capture invocation is prohibitively slow in bulk.
		Common::Path batchPath;

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

	/**
	 * @returns true if any harness mode was requested on the command
	 *          line (--screenshot, --eob-dump-state or --eob-batch).
	 */
	static bool isEnabled();

	/**
	 * Supply the next scripted dialogue answer, if the harness was given
	 * any. EoBCoreEngine::runDialogue calls this before it would block
	 * waiting for a button press: a headless harness has no one to click.
	 *
	 * @returns true if @p out was filled with a forced answer.
	 */
	static bool nextDialogueAnswer(int &out);

	/**
	 * Report whether the script currently running has exceeded the
	 * harness's opcode budget, and should be abandoned.
	 *
	 * EoBInfProcessor::run has no step limit: it runs until a script
	 * ends or aborts. That is fine in play, where every reachable script
	 * terminates, but a trigger sweep fires scripts in states the game
	 * never produces (any block, any invocation kind, an emptied monster
	 * table) and some of those spin forever. A sweep that hangs on one
	 * script yields nothing at all, so the budget trades a truncated
	 * record of one trigger for a complete record of the rest.
	 *
	 * @returns false when no harness mode is active, so normal play is
	 *          never subject to a limit.
	 */
	static bool scriptBudgetExceeded();

	/** Reset the opcode budget before running one script. */
	static void resetScriptBudget();


	/** @returns true if the last script was cut short by the budget. */
	static bool scriptWasTruncated();

	/**
	 * Write a canonical engine state snapshot for the currently loaded
	 * level to @p path.
	 *
	 * The snapshot is the shared primitive behind movement, passability
	 * and trigger conformance: GridDelve emits the same format, and each
	 * conformance test is a diff of two snapshots. It is deliberately
	 * line-oriented, ordered and free of timestamps or absolute paths so
	 * that a diff points at engine state rather than at formatting.
	 *
	 * @returns false and sets @p err if the file cannot be written.
	 */
	static bool writeStateSnapshot(EoBCoreEngine *vm, const Common::Path &path,
		Common::String &err);

	/**
	 * Parse and validate harness settings from ConfMan.
	 * @returns true on success. On failure, sets @p err to a
	 *          human-readable description.
	 */
	static bool parseSettings(Settings &out, Common::String &err);

	/**
	 * Execute the harness: bootstrap engine state, produce whatever the
	 * requested mode asks for, exit. Never returns.
	 */
	static void run(EoBCoreEngine *vm);

private:
	/** Bootstrap party/level state for a fresh (non-save) capture. */
	static void bootstrap(EoBCoreEngine *vm, const Settings &s);

	/** Load @p level and point the party at (@p x, @p y) facing @p facing. */
	static void gotoPosition(EoBCoreEngine *vm, uint8 level, uint8 x, uint8 y, uint8 facing);

	/** Render the current scene and write it to @p path as a PNG. */
	static bool writeFrame(EoBCoreEngine *vm, const Common::Path &path, Common::String &err);

	/** Execute a batch script. @returns false and sets @p err on failure. */
	static bool runBatch(EoBCoreEngine *vm, const Common::Path &path, Common::String &err);

	/**
	 * Fire every trigger on @p level from a clean state, recording what
	 * each one changed. @returns false and sets @p err on failure.
	 */
	static bool fireTriggers(EoBCoreEngine *vm, int level, const Common::Path &path,
		Common::String &err);

	/**
	 * Everything a trigger can change, small enough to hold two copies
	 * while one script runs. Declared here rather than at file scope so
	 * the capture helper can be a member and inherit the harness's
	 * friendship with the engine classes.
	 */
	struct TriggerState {
		/**
		 * One record of the item table. Mirrors the EoBItem fields a
		 * script can change plus `prev`, the link the per-block item
		 * lists are walked along; `next` and the name indices are left
		 * out because no delta line reads them.
		 */
		struct ItemSnapshot {
			uint8 level;
			int16 block;
			int8 pos;
			int8 type;
			int8 value;
			uint8 flags;
			int8 icon;
			int16 prev;
		};

		uint8 walls[1024][4];
		uint32 flags[18];
		uint8 level;
		uint16 block;
		uint16 direction;
		int8 doorState[3];
		uint16 doorBlock[3];
		Common::Array<ItemSnapshot> items;
		uint16 drawObjects[1024];
		int hand;
	};

	/** Copy the engine's current trigger-visible state into @p out. */
	static void captureTriggerState(EoBCoreEngine *vm, TriggerState &out);

	/** Item-table helpers for the `item` and `items` delta lines. */
	static bool itemIsFree(const TriggerState &st, uint idx);
	static bool sameItem(const TriggerState &a, const TriggerState &b, uint idx);
	static Common::String itemSide(const TriggerState &st, uint idx);
	static void blockItemList(const TriggerState &st, int block, Common::Array<uint16> &out);

	/** Reload @p level so each trigger fires against identical state. */
	static void resetLevel(EoBCoreEngine *vm, int level);

	/**
	 * Install a fixed four-character party.
	 *
	 * startupNew() only sets up the playfield; it creates no characters,
	 * so a harness run has an empty party. That is harmless while only
	 * drawing frames (the portraits are empty in reference and candidate
	 * alike) but fatal once scripts run: oeob_printMessage_v2 picks a
	 * speaker with `while (!testCharacter(c, 3)) c = (c + 1) % 6;`, which
	 * spins forever when no character qualifies.
	 *
	 * The party is deliberately minimal and fixed rather than imported
	 * from a save, so that a trigger sweep is reproducible on any
	 * installation.
	 */
	static void installHarnessParty(EoBCoreEngine *vm);
};

} // End of namespace Kyra

#endif
