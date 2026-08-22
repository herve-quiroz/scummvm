// The harness is a one-shot CLI mode: we bypass ScummVM's normal
// engine-completion path (which would show a launcher or modal error
// dialog) by calling _exit() directly with our chosen status code.
// _exit skips atexit hooks so we don't deadlock on SDL2/audio thread
// destructors that the engine's own message loop normally drains before
// teardown.
#define FORBIDDEN_SYMBOL_EXCEPTION_exit
#define FORBIDDEN_SYMBOL_EXCEPTION_getenv

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
#include "common/fs.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"
#include "graphics/surface.h"
#include "image/png.h"
#include "kyra/detection.h"
#include "kyra/engine/eobcommon.h"
#include "kyra/graphics/screen.h"
#include "kyra/script/script_eob.h"

namespace Kyra {

static bool haveSetting(const char *key) {
	return ConfMan.hasKey(key) && !ConfMan.get(key).empty();
}

bool ScreenshotHarness::isEnabled() {
	return haveSetting("screenshot") || haveSetting("eob_dump_state")
		|| haveSetting("eob_batch") || haveSetting("eob_fire_triggers");
}

// Scripted dialogue answers, consumed in order by runDialogue. A headless
// harness has nobody to click the buttons, and several EOB2 triggers open
// a dialogue, so without these the engine spins forever on the first one.
static Common::Array<int> g_dialogueAnswers;
static uint g_dialogueAnswerPos = 0;
static int g_dialogueDefault = 1;
static uint g_dialoguesAnswered = 0;

static void parseDialogueAnswers() {
	g_dialogueAnswers.clear();
	g_dialogueAnswerPos = 0;
	g_dialoguesAnswered = 0;
	if (!haveSetting("eob_dialog_answers"))
		return;
	Common::StringTokenizer tok(ConfMan.get("eob_dialog_answers"), ",");
	while (!tok.empty()) {
		Common::String t = tok.nextToken();
		t.trim();
		if (!t.empty())
			g_dialogueAnswers.push_back(atoi(t.c_str()));
	}
}

bool ScreenshotHarness::nextDialogueAnswer(int &out) {
	if (!isEnabled())
		return false;

	// Past the end of the supplied list, keep answering with the default
	// rather than blocking: a trigger sweep must not stall on trigger 900
	// because the answer list was written for the first few.
	if (g_dialogueAnswerPos < g_dialogueAnswers.size())
		out = g_dialogueAnswers[g_dialogueAnswerPos++];
	else
		out = g_dialogueDefault;

	g_dialoguesAnswered++;
	return true;
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
	const bool haveShot = haveSetting("screenshot");
	const bool haveDump = haveSetting("eob_dump_state");
	const bool haveBatch = haveSetting("eob_batch");
	const bool haveTrig = haveSetting("eob_fire_triggers");

	if (!haveShot && !haveDump && !haveBatch && !haveTrig) {
		err = "one of --screenshot=PATH, --eob-dump-state=PATH, --eob-fire-triggers=PATH "
			"or --eob-batch=FILE is required";
		return false;
	}
	if (haveTrig)
		out.fireTriggersPath = Common::Path::fromCommandLine(ConfMan.get("eob_fire_triggers"));
	parseDialogueAnswers();
	if (haveShot)
		out.screenshotPath = Common::Path::fromCommandLine(ConfMan.get("screenshot"));
	if (haveDump)
		out.dumpStatePath = Common::Path::fromCommandLine(ConfMan.get("eob_dump_state"));
	if (haveBatch)
		out.batchPath = Common::Path::fromCommandLine(ConfMan.get("eob_batch"));

	// A batch script carries the level, cell and facing for every capture
	// on its own lines, so the command line does not have to. A state dump
	// needs a level but no viewpoint, because a snapshot describes the
	// whole level rather than what the party can see.
	const bool needLevel = !haveBatch;
	const bool needPosition = haveShot && !haveBatch;

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
	} else if (needLevel && !haveSave) {
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
	} else if (needPosition && !haveSave) {
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
	} else if (needPosition && !haveSave) {
		err = "--facing=N|E|S|W is required (or pass --save-slot=N)";
		return false;
	}

	out.noActors = ConfMan.hasKey("no_actors") && ConfMan.getBool("no_actors");

	return true;
}

static const char *const kFacingNames[4] = { "N", "E", "S", "W" };

void ScreenshotHarness::gotoPosition(EoBCoreEngine *vm, uint8 level, uint8 x, uint8 y, uint8 facing) {
	if (level != vm->_currentLevel) {
		vm->_currentLevel = level;
		vm->_currentSub = 0;
		vm->loadLevel(level, 0);
	}
	vm->_currentBlock = (uint16)y * 32u + (uint16)x;
	vm->_currentDirection = facing;
}

void ScreenshotHarness::bootstrap(EoBCoreEngine *vm, const Settings &s) {
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
	//     etc.) - anything the engine persists. After load, apply
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

		vm->_currentLevel = s.haveLevel ? s.level : 1;
		vm->_currentSub = 0;
		vm->loadLevel(vm->_currentLevel, 0);
		if (s.haveCell)
			vm->_currentBlock = (uint16)s.cellY * 32u + (uint16)s.cellX;
		if (s.haveFacing)
			vm->_currentDirection = s.facing;
		vm->setHandItem(0);
	}

	// Optional actor suppression. EOB2's monster table is _monsters[]
	// owned by EoBCoreEngine; setting block to 0 is the in-engine
	// "out of play" marker (see killMonster + placeMonster), so the
	// drawMonsters pass skips them without disturbing wall/decoration
	// draw paths.
	if (s.noActors) {
		for (int i = 0; i < 30; ++i)
			vm->_monsters[i].block = 0;
	}
}

bool ScreenshotHarness::writeStateSnapshot(EoBCoreEngine *vm, const Common::Path &path,
		Common::String &err) {
	Common::DumpFile out;
	if (!out.open(path)) {
		err = Common::String::format("cannot open '%s' for writing",
			path.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	// Header carries a format version so a GridDelve-side reader can
	// refuse a snapshot it does not understand rather than silently
	// misparsing one.
	out.writeString("# eob2-state v1\n");
	out.writeString(Common::String::format("level %d\n", vm->_currentLevel));
	out.writeString(Common::String::format("party %d %d %s\n",
		vm->_currentBlock & 0x1F, (vm->_currentBlock >> 5) & 0x1F,
		kFacingNames[vm->_currentDirection & 3]));

	// Script flags: 18 words, index 17 being the global set that
	// setFlags/clearFlags operate on. The mask API cannot enumerate
	// them, hence the friend declaration on EoBInfProcessor.
	Common::String flags("flags");
	for (int i = 0; i < 18; ++i)
		flags += Common::String::format(" %08x", vm->_inf->_flagTable[i]);
	flags += "\n";
	out.writeString(flags);

	// Wall-type properties, indexed by wall type rather than by block.
	// Bit 0 of each entry is what decides passability, so this table
	// plus the per-block wall types is the whole movement input.
	Common::String wf("wallflags");
	for (int i = 0; i < 256; ++i)
		wf += Common::String::format(" %02x", vm->_wllWallFlags[i]);
	wf += "\n";
	out.writeString(wf);

	for (int i = 0; i < 3; ++i)
		out.writeString(Common::String::format("door %d %u %d %d\n", i,
			vm->_openDoorState[i].block, vm->_openDoorState[i].wall,
			vm->_openDoorState[i].state));

	// One line per block, in block order. assignedObjects is the offset
	// of the block's trigger script, so this doubles as the trigger
	// enumeration both engines must agree on.
	for (int i = 0; i < 1024; ++i) {
		const LevelBlockProperty &b = vm->_levelBlockProperties[i];
		out.writeString(Common::String::format("block %d %d %d %d %d %04x %04x %04x %d\n",
			i, b.walls[0], b.walls[1], b.walls[2], b.walls[3],
			b.flags, b.assignedObjects, b.drawObjects, b.direction));
	}

	out.finalize();
	out.close();
	return true;
}

bool ScreenshotHarness::writeFrame(EoBCoreEngine *vm, const Common::Path &path,
		Common::String &err) {
	// drawScene() renders the 3D viewport into the back buffer.
	// gui_drawAllCharPortraitsWithStats() renders the right-side HUD.
	vm->drawScene(1);
	vm->gui_drawAllCharPortraitsWithStats();

	// Force the back buffer to the front so the page-0 surface contains
	// the composed frame. EOB's drawScene already calls copyRegion on
	// success, but the explicit update mirrors what a player would see
	// at the end of a normal frame.
	vm->_screen->updateScreen();

	Common::DumpFile out;
	if (!out.open(path)) {
		err = Common::String::format("cannot open '%s' for writing",
			path.toString(Common::Path::kNativeSeparator).c_str());
		return false;
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
		err = Common::String::format("writePNG failed for '%s'",
			path.toString(Common::Path::kNativeSeparator).c_str());
		out.close();
		return false;
	}

	out.close();
	return true;
}

void ScreenshotHarness::captureTriggerState(EoBCoreEngine *vm, TriggerState &out) {
	for (int i = 0; i < 1024; ++i)
		for (int w = 0; w < 4; ++w)
			out.walls[i][w] = vm->_levelBlockProperties[i].walls[w];
	for (int i = 0; i < 18; ++i)
		out.flags[i] = vm->_inf->_flagTable[i];
	out.level = vm->_currentLevel;
	out.block = vm->_currentBlock;
	out.direction = vm->_currentDirection;
	for (int i = 0; i < 3; ++i) {
		out.doorState[i] = vm->_openDoorState[i].state;
		out.doorBlock[i] = vm->_openDoorState[i].block;
	}
}

// Reload the level from scratch so each trigger fires against identical
// state. _hasTempDataFlags has to be cleared first: with it set,
// loadBlockProperties restores the modified block table instead of
// re-reading the maze, and the previous trigger's wall edits leak into
// the next one's baseline.
void ScreenshotHarness::installHarnessParty(EoBCoreEngine *vm) {
	static const char *const kNames[4] = { "HARNESS1", "HARNESS2", "HARNESS3", "HARNESS4" };

	for (int i = 0; i < 6; ++i) {
		EoBCharacter &c = vm->_characters[i];
		if (i >= 4) {
			c.flags = 0;
			continue;
		}
		c.id = (uint8)i;
		c.flags = 1;
		Common::strlcpy(c.name, kNames[i], sizeof(c.name));
		c.strengthCur = c.strengthMax = 16;
		c.strengthExtCur = c.strengthExtMax = 0;
		c.intelligenceCur = c.intelligenceMax = 12;
		c.wisdomCur = c.wisdomMax = 12;
		c.dexterityCur = c.dexterityMax = 12;
		c.constitutionCur = c.constitutionMax = 12;
		c.charismaCur = c.charismaMax = 12;
		c.hitPointsCur = c.hitPointsMax = 20;
		c.armorClass = 10;
		c.disabledSlots = 0;
		c.raceSex = 0;
		c.cClass = 0;
		c.alignment = 0;
		c.portrait = (int8)i;
		c.food = 100;
		for (int l = 0; l < 3; ++l) {
			c.level[l] = 1;
			c.experience[l] = 0;
		}
		c.hitPointsDividend = 0;
	}
	vm->_updateCharNum = 0;
}

void ScreenshotHarness::resetLevel(EoBCoreEngine *vm, int level) {
	vm->_hasTempDataFlags = 0;
	vm->_inf->reset();
	installHarnessParty(vm);
	vm->_currentLevel = level;
	vm->_currentSub = 0;
	vm->loadLevel(level, 0);
	vm->_currentBlock = 0;
	vm->_currentDirection = 0;
}

bool ScreenshotHarness::fireTriggers(EoBCoreEngine *vm, int level,
		const Common::Path &path, Common::String &err) {
	Common::DumpFile out;
	if (!out.open(path)) {
		err = Common::String::format("cannot open '%s' for writing",
			path.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	out.writeString("# eob2-triggers v1\n");
	out.writeString(Common::String::format("level %d\n", level));
	out.flush();

	resetLevel(vm, level);

	// Collect the trigger list before firing anything: a trigger can
	// rewrite block properties, and the enumeration must describe the
	// level as loaded, not as some earlier trigger left it.
	Common::Array<uint16> blocks;
	for (int i = 0; i < 1024; ++i) {
		if (vm->_levelBlockProperties[i].assignedObjects)
			blocks.push_back((uint16)i);
	}

	// The three invocation kinds a player can cause. run() gates on
	// subFlags = ((blockFlags & 0xFFF8) >> 3) | 0xE0, so 0x40 always
	// passes while 0x01 and 0x02 depend on the block's own flags.
	static const int kInvokeFlags[] = { 0x01, 0x02, 0x40 };

	TriggerState before, after;

	for (uint bi = 0; bi < blocks.size(); ++bi) {
		const uint16 block = blocks[bi];

		for (int fi = 0; fi < ARRAYSIZE(kInvokeFlags); ++fi) {
			const int invoke = kInvokeFlags[fi];

			resetLevel(vm, level);

			const uint16 blockFlags = vm->_levelBlockProperties[block].flags;
			const uint16 script = vm->_levelBlockProperties[block].assignedObjects;
			const uint16 subFlags = ((blockFlags & 0xFFF8) >> 3) | 0xE0;
			if (!(invoke & subFlags))
				continue;

			// Put the party on the trigger's own block facing north.
			// Step-on scripts routinely read the party position, and a
			// party parked elsewhere would take a different branch.
			vm->_currentBlock = block;
			vm->_currentDirection = 0;

			captureTriggerState(vm, before);
			const uint answeredBefore = g_dialoguesAnswered;

			// Record and flush the header before running the script, so
			// that a script which hangs or crashes leaves a file whose
			// last line names the trigger responsible.
			out.writeString(Common::String::format(
				"trigger %u %u %u flags=%04x script=%04x invoke=%02x\n",
				block, block & 0x1F, (block >> 5) & 0x1F, blockFlags, script, invoke));
			out.flush();

			if (const char *pf = getenv("EOB_TRIG_PROGRESS")) {
				Common::DumpFile prog;
				if (prog.open(Common::Path(pf))) {
					// Rewritten (not appended) each time, and closed
					// immediately: the main output is buffered, so if a
					// script hangs this file is the only record of which
					// trigger was running. Enable with EOB_TRIG_PROGRESS.
					prog.writeString(Common::String::format(
						"block=%u invoke=%02x script=%04x\n", block, invoke, script));
					prog.close();
				}
			}

			vm->runLevelScript(block, invoke);

			captureTriggerState(vm, after);

			// A trigger that switches level leaves the block table
			// describing a different map, so a wall-by-wall diff would
			// be a thousand lines of noise rather than a record of what
			// the trigger did. Report the transition instead.
			const bool levelChanged = before.level != after.level;
			if (levelChanged) {
				out.writeString("  walls-not-compared level-changed\n");
			} else {
				for (int i = 0; i < 1024; ++i) {
					for (int w = 0; w < 4; ++w) {
						if (before.walls[i][w] != after.walls[i][w])
							out.writeString(Common::String::format(
								"  wall %d %d %d -> %d\n", i, w,
								before.walls[i][w], after.walls[i][w]));
					}
				}
			}
			for (int i = 0; i < 18; ++i) {
				if (before.flags[i] != after.flags[i])
					out.writeString(Common::String::format(
						"  flag %d %08x -> %08x\n", i, before.flags[i], after.flags[i]));
			}
			if (before.level != after.level)
				out.writeString(Common::String::format(
					"  level %d -> %d\n", before.level, after.level));
			if (before.block != after.block || before.direction != after.direction)
				out.writeString(Common::String::format(
					"  party %u %u %u -> %u %u %u\n",
					before.block & 0x1F, (before.block >> 5) & 0x1F, before.direction,
					after.block & 0x1F, (after.block >> 5) & 0x1F, after.direction));
			for (int i = 0; i < 3 && !levelChanged; ++i) {
				if (before.doorState[i] != after.doorState[i]
						|| before.doorBlock[i] != after.doorBlock[i])
					out.writeString(Common::String::format(
						"  door %d %u/%d -> %u/%d\n", i,
						before.doorBlock[i], before.doorState[i],
						after.doorBlock[i], after.doorState[i]));
			}
			if (g_dialoguesAnswered != answeredBefore)
				out.writeString(Common::String::format(
					"  dialogues %u\n", g_dialoguesAnswered - answeredBefore));
			out.writeString("  end\n");
			// Flush per trigger: the KYRA harness's output normally only
			// lands when the process dies, so without this a sweep that
			// stalls on one script leaves nothing to say which.
			out.flush();
		}
	}

	out.writeString(Common::String::format("# %u trigger block(s)\n", blocks.size()));
	out.finalize();
	out.close();
	return true;
}

bool ScreenshotHarness::runBatch(EoBCoreEngine *vm, const Common::Path &path,
		Common::String &err) {
	Common::FSNode node(path);
	Common::SeekableReadStream *in = node.createReadStream();
	if (!in) {
		err = Common::String::format("cannot read batch file '%s'",
			path.toString(Common::Path::kNativeSeparator).c_str());
		return false;
	}

	int lineNo = 0;
	int captured = 0;
	bool ok = true;

	while (!in->eos()) {
		Common::String line = in->readLine();
		++lineNo;
		line.trim();
		if (line.empty() || line[0] == '#')
			continue;

		Common::StringTokenizer tok(line, " \t");
		Common::String cmd = tok.nextToken();

		if (cmd == "state") {
			Common::String outPath = tok.nextToken();
			Common::String levelStr = tok.nextToken();
			if (outPath.empty() || levelStr.empty()) {
				err = Common::String::format("line %d: 'state' needs <outpath> <level>", lineNo);
				ok = false;
				break;
			}
			int level = atoi(levelStr.c_str());
			if (level < 1 || level > 16) {
				err = Common::String::format("line %d: level %d out of range (1-16)", lineNo, level);
				ok = false;
				break;
			}
			// A state dump describes the whole level, so the viewpoint
			// is irrelevant; park the party at a fixed block so the
			// snapshot's party line stays deterministic.
			gotoPosition(vm, (uint8)level, 0, 0, 0);
			if (!writeStateSnapshot(vm, Common::Path::fromCommandLine(outPath), err)) {
				err = Common::String::format("line %d: %s", lineNo, err.c_str());
				ok = false;
				break;
			}
			++captured;
		} else if (cmd == "triggers") {
			Common::String outPath = tok.nextToken();
			Common::String levelStr = tok.nextToken();
			if (outPath.empty() || levelStr.empty()) {
				err = Common::String::format("line %d: 'triggers' needs <outpath> <level>", lineNo);
				ok = false;
				break;
			}
			int level = atoi(levelStr.c_str());
			if (level < 1 || level > 16) {
				err = Common::String::format("line %d: level %d out of range (1-16)", lineNo, level);
				ok = false;
				break;
			}
			if (!fireTriggers(vm, level, Common::Path::fromCommandLine(outPath), err)) {
				err = Common::String::format("line %d: %s", lineNo, err.c_str());
				ok = false;
				break;
			}
			++captured;
		} else if (cmd == "shot") {
			Common::String outPath = tok.nextToken();
			Common::String levelStr = tok.nextToken();
			Common::String xStr = tok.nextToken();
			Common::String yStr = tok.nextToken();
			Common::String facingStr = tok.nextToken();
			uint8 facing = 0;
			if (outPath.empty() || levelStr.empty() || xStr.empty() || yStr.empty()
					|| !parseFacing(facingStr, facing)) {
				err = Common::String::format(
					"line %d: 'shot' needs <outpath> <level> <x> <y> <N|E|S|W>", lineNo);
				ok = false;
				break;
			}
			int level = atoi(levelStr.c_str());
			int x = atoi(xStr.c_str());
			int y = atoi(yStr.c_str());
			if (level < 1 || level > 16 || x < 0 || x > 31 || y < 0 || y > 31) {
				err = Common::String::format("line %d: level/cell out of range", lineNo);
				ok = false;
				break;
			}
			gotoPosition(vm, (uint8)level, (uint8)x, (uint8)y, facing);
			// loadLevel repopulates the monster table, so actor
			// suppression has to be reapplied per capture rather than
			// once during bootstrap.
			if (ConfMan.hasKey("no_actors") && ConfMan.getBool("no_actors")) {
				for (int i = 0; i < 30; ++i)
					vm->_monsters[i].block = 0;
			}
			if (!writeFrame(vm, Common::Path::fromCommandLine(outPath), err)) {
				err = Common::String::format("line %d: %s", lineNo, err.c_str());
				ok = false;
				break;
			}
			++captured;
		} else {
			err = Common::String::format("line %d: unknown command '%s'", lineNo, cmd.c_str());
			ok = false;
			break;
		}
	}

	delete in;
	if (ok)
		debug("Screenshot harness: batch produced %d capture(s)", captured);
	return ok;
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

	bootstrap(vm, s);

	if (!s.batchPath.empty()) {
		if (!runBatch(vm, s.batchPath, err)) {
			warning("Screenshot harness: %s", err.c_str());
			_exit(1);
		}
		_exit(0);
	}

	if (!s.fireTriggersPath.empty()) {
		if (!fireTriggers(vm, s.haveLevel ? s.level : vm->_currentLevel,
				s.fireTriggersPath, err)) {
			warning("Screenshot harness: %s", err.c_str());
			_exit(1);
		}
		debug("Screenshot harness: wrote %s",
			s.fireTriggersPath.toString(Common::Path::kNativeSeparator).c_str());
	}

	if (!s.dumpStatePath.empty()) {
		if (!writeStateSnapshot(vm, s.dumpStatePath, err)) {
			warning("Screenshot harness: %s", err.c_str());
			_exit(1);
		}
		debug("Screenshot harness: wrote %s",
			s.dumpStatePath.toString(Common::Path::kNativeSeparator).c_str());
	}

	if (!s.screenshotPath.empty()) {
		if (!writeFrame(vm, s.screenshotPath, err)) {
			warning("Screenshot harness: %s", err.c_str());
			_exit(1);
		}
		debug("Screenshot harness: wrote %s",
			s.screenshotPath.toString(Common::Path::kNativeSeparator).c_str());
	}

	_exit(0);
}

} // End of namespace Kyra
