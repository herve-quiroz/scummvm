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
#include "kyra/kyra_v1.h"
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

// Opcode budget for one script run. Sized far above any real EOB2
// script: the longest observed trigger executes a few dozen opcodes.
static const uint kScriptOpcodeBudget = 20000;
static uint g_scriptOpcodes = 0;
static bool g_scriptTruncated = false;

void ScreenshotHarness::resetScriptBudget() {
	g_scriptOpcodes = 0;
	g_scriptTruncated = false;
}

bool ScreenshotHarness::scriptBudgetExceeded() {
	if (!isEnabled())
		return false;
	if (++g_scriptOpcodes <= kScriptOpcodeBudget)
		return false;
	g_scriptTruncated = true;
	return true;
}

bool ScreenshotHarness::scriptWasTruncated() {
	return g_scriptTruncated;
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
	out.items.resize(vm->_items.size());
	for (uint i = 0; i < vm->_items.size(); ++i) {
		const EoBItem &src = vm->_items[i];
		TriggerState::ItemSnapshot &dst = out.items[i];
		dst.level = src.level;
		dst.block = src.block;
		dst.pos = src.pos;
		dst.type = src.type;
		dst.value = src.value;
		dst.flags = src.flags;
		dst.icon = src.icon;
		dst.prev = src.prev;
	}
	for (int i = 0; i < 1024; ++i)
		out.drawObjects[i] = vm->_levelBlockProperties[i].drawObjects;
	out.hand = vm->_itemInHand;
}

// A record is free when its block is -1. Picking an item up into the
// hand or a pack also sets -1, so those read as free too, while an item
// created straight into the hand keeps its template's block (duplicateItem
// copies the record and only setItemPosition writes a block); the `hand`
// line is what names the hand item either way.
bool ScreenshotHarness::itemIsFree(const TriggerState &st, uint idx) {
	return idx >= st.items.size() || st.items[idx].block == -1;
}

bool ScreenshotHarness::sameItem(const TriggerState &a, const TriggerState &b, uint idx) {
	const bool fa = itemIsFree(a, idx);
	const bool fb = itemIsFree(b, idx);
	if (fa || fb)
		return fa == fb;
	const TriggerState::ItemSnapshot &x = a.items[idx];
	const TriggerState::ItemSnapshot &y = b.items[idx];
	return x.level == y.level && x.block == y.block && x.pos == y.pos
		&& x.type == y.type && x.value == y.value && x.flags == y.flags
		&& x.icon == y.icon;
}

// One side of an `item` line: the bare word `free`, or
// `<level>:<block>:<pos> <type>/<value>/<flags>/<icon>`.
Common::String ScreenshotHarness::itemSide(const TriggerState &st, uint idx) {
	if (itemIsFree(st, idx))
		return "free";
	const TriggerState::ItemSnapshot &it = st.items[idx];
	return Common::String::format("%u:%d:%d %d/%d/%02x/%d",
		(uint)it.level, (int)it.block, (int)it.pos,
		(int)it.type, (int)it.value, (uint)it.flags, (int)it.icon);
}

// The item list of @p block in the order the engine walks it: from the
// drawObjects head along `prev` until it returns to the head, the same
// walk countQueuedItems does. Bounded so a corrupt ring cannot spin.
void ScreenshotHarness::blockItemList(const TriggerState &st, int block,
		Common::Array<uint16> &out) {
	out.clear();
	const uint16 head = st.drawObjects[block];
	if (!head)
		return;
	uint16 cur = head;
	for (int steps = 0; steps < 1024; ++steps) {
		out.push_back(cur);
		if (cur >= st.items.size())
			break;
		cur = (uint16)st.items[cur].prev;
		if (cur == head || cur == 0)
			break;
	}
}

static bool sameList(const Common::Array<uint16> &a, const Common::Array<uint16> &b) {
	if (a.size() != b.size())
		return false;
	for (uint i = 0; i < a.size(); ++i)
		if (a[i] != b[i])
			return false;
	return true;
}

// Comma-separated indices, or `-` for an empty list.
static Common::String listText(const Common::Array<uint16> &l) {
	if (l.empty())
		return "-";
	Common::String s;
	for (uint i = 0; i < l.size(); ++i) {
		if (i)
			s += ",";
		s += Common::String::format("%u", (uint)l[i]);
	}
	return s;
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
	// Deliberately not applying --no-actors here. That flag suppresses
	// actor *rendering*; emptying the monster table makes some scripts
	// spin forever, and the state snapshot carries no monster data, so a
	// sweep gains nothing from it.
	// Seed the RNG so a sweep is reproducible. Several opcodes roll dice
	// (oeob_printMessage_v2 picks a speaker, oeob_moveInventoryItemToBlock
	// picks a slot), so without this the same sweep takes different script
	// branches on different runs: fixtures stop being stable, and a
	// script that spins does so only intermittently, which is far harder
	// to diagnose than one that always does.
	vm->_rnd.setSeed(0x5EED);

	vm->_hasTempDataFlags = 0;
	vm->_inf->reset();

	// Nothing clears the door animation slots on a level load: they are
	// emptied by completeDoorOperations, which runs when the party moves
	// or a door finishes animating, and neither happens under the
	// harness. Without this, a trigger that opened a door leaves the
	// block registered for the next trigger, which then reads it as a
	// door already in motion and does the opposite, or nothing. That
	// makes a firing's recorded deltas depend on which firings came
	// before it, which is exactly what a per-firing reset is for.
	for (int i = 0; i < 3; ++i) {
		vm->_openDoorState[i].block = 0;
		vm->_openDoorState[i].state = 0;
		vm->_openDoorState[i].wall = 0;
	}

	// The flying-object slots are the same kind of leftover: an item a
	// script launched is flown by timerProcessFlyingObjects, a timer that
	// never ticks under the harness, and a flight the drain in
	// fireTriggers could not finish would otherwise stay enabled into the
	// next firing's drain and land there instead. Nothing on the level
	// load path clears them either.
	for (int i = 0; i < vm->_numFlyingObjects; ++i)
		memset(&vm->_flyingObjects[i], 0, sizeof(EoBFlyingObject));

	// The item table is game-wide state that a level load does not
	// touch: createItem appends to it, deleteItem and the item moves
	// rewrite it, and a created item left in the hand stays there.
	// Without this, a firing that creates an item changes what every
	// later firing finds on its blocks and in the party's hand. Reload
	// the table the game ships and empty the hand, which is item 0, the
	// table's dummy record, as the screenshot path leaves it.
	vm->loadItemDefs();
	vm->_itemInHand = 0;
	vm->_lastUsedItem = 0;

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

	// Level marker for the opcode trace, so a single batched run with
	// --debuglevel=3 --debugflags=Script can be split per level as well
	// as per trigger.
	debugC(3, kDebugLevelScript, "HARNESS-LEVEL %d", level);

	out.writeString("# eob2-triggers v2\n");
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

			// Mark the opcode trace so a caller running with
			// --debuglevel=3 --debugflags=Script can split ScummVM's own
			// per-opcode log into per-trigger runs. The log line format
			// is "[0xNNNN] EoBInfProcessor::oeob_name()", emitted by
			// EoBInfProcessor::run for every opcode it executes, which
			// makes it an exact oracle for opcode lengths and control
			// flow rather than just for end state.
			debugC(3, kDebugLevelScript, "HARNESS-TRIGGER block=%u invoke=%02x", block, invoke);

			resetScriptBudget();
			vm->runLevelScript(block, invoke);

			// A script that launches an item (oeob_launchObject) parks it
			// in _flyingObjects on its start block at pos | 4. The game
			// flies it from timerProcessFlyingObjects, a timer that never
			// ticks under the harness, so without this the after state
			// would show an item hovering over its launch block. Drain
			// the flights here so the reference records where the item
			// lands and the crossing (0x10) and landing (4) scripts it
			// fires on the way. Bounded, because a landing script can
			// launch again and a magic object with distance 255 only
			// stops at a wall.
			bool flightBudgetExceeded = false;
			for (int iter = 0;; ++iter) {
				bool inFlight = false;
				for (int i = 0; i < vm->_numFlyingObjects && !inFlight; ++i)
					inFlight = vm->_flyingObjects[i].enable != 0;
				if (!inFlight)
					break;
				if (iter == 64) {
					flightBudgetExceeded = true;
					break;
				}
				vm->timerProcessFlyingObjects(0);
			}

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
			// The item table is game-wide, so its records are compared
			// even across a level change; the per-block lists are not,
			// for the same reason the walls are not. A record the firing
			// appended has no before side and reads as free there.
			const uint itemCount = MAX(before.items.size(), after.items.size());
			for (uint i = 1; i < itemCount; ++i) {
				if (!sameItem(before, after, i))
					out.writeString(Common::String::format(
						"  item %u %s -> %s\n", i,
						itemSide(before, i).c_str(), itemSide(after, i).c_str()));
			}
			if (!levelChanged) {
				Common::Array<uint16> lb, la;
				for (int i = 0; i < 1024; ++i) {
					blockItemList(before, i, lb);
					blockItemList(after, i, la);
					if (!sameList(lb, la))
						out.writeString(Common::String::format(
							"  items %d %s -> %s\n", i,
							listText(lb).c_str(), listText(la).c_str()));
				}
			}
			if (before.hand != after.hand)
				out.writeString(Common::String::format(
					"  hand %d -> %d\n", before.hand, after.hand));
			if (scriptWasTruncated())
				out.writeString("  truncated opcode-budget-exceeded\n");
			if (flightBudgetExceeded)
				out.writeString("  truncated flight-budget-exceeded\n");
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
