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
#include "common/memstream.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "image/png.h"
#include "mm/shared/xeen/sprites.h"
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
	out.noBorderAnims = ConfMan.hasKey("mm_no_border_anims") && ConfMan.getBool("mm_no_border_anims");

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

	// Each map's data is bundled with one side's cc/save archive: Clouds
	// maps live in xeen.cc (side=0, surfaced via _xeenSave at runtime),
	// Dark Side maps in dark.cc (side=1, _darkSave). A handful of
	// Clouds-reachable maps, notably 109/110/111 (the neighbours of
	// Vertigo), are actually stored in the Dark Side bundle. In normal
	// play a cmdFlipWorld script flips _loadCcNum during the cross-boundary
	// transition. The harness teleports directly without running scripts,
	// so it has to pick the right side itself. Without this, loadEvents()
	// raises a fatal error("Could not open file - maze...evt!"), which
	// pops up the modal debugger console and hangs forever under headless
	// dummy/offscreen SDL.
	{
		Common::Path evtName(Common::String::format(
			"maze%c%03d.evt", (s.mazeId >= 100) ? 'x' : '0', s.mazeId));
		SaveArchive *xeenSave = vm->_files->_xeenSave;
		SaveArchive *darkSave = vm->_files->_darkSave;
		bool xeenHas = xeenSave && xeenSave->hasFile(evtName);
		bool darkHas = darkSave && darkSave->hasFile(evtName);
		bool onRequested = (s.side == 0) ? xeenHas : darkHas;
		if (!onRequested) {
			uint8 other = s.side ? 0 : 1;
			bool otherHas = (other == 0) ? xeenHas : darkHas;
			if (otherHas) {
				debug("Screenshot harness: maze %u not on side %u, switching to side %u",
					s.mazeId, s.side, other);
				s.side = other;
			}
			// Else: leave s.side as-is and let Map::load below report the
			// missing file. The harness still hangs in that error path
			// under headless SDL, but the user sees the 'Could not open
			// file' line on stderr before the timeout fires.
		}
	}

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

	// Suppress the five animated border UI overlays drawn by
	// Interface::assembleBorder() (levitate-bat, spot-doors, danger-sense,
	// two clairvoyance faces). The mm5e renderer composites its chrome
	// from back.raw and does not draw these animation cells, so leaving
	// them in the reference frame produces deterministic pixel diffs that
	// have nothing to do with the 3D viewport. Gating the draws inside
	// Interface (rather than re-blitting back.raw over the regions here)
	// is simpler: it avoids hard-coding sprite extents and stashing a copy
	// of back.raw around the draw3d call.
	if (s.noBorderAnims)
		vm->_interface->_suppressBorderAnims = true;

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

// ---------------------------------------------------------------------------
// Scaler reference oracle
//
// Dumps SpriteResource::draw() output at every scale 0..15 across a couple of
// synthetic 16x16 test patterns. mm5e commits these PNGs as fixtures and uses
// them to byte-compare its own scaler against ScummVM's. We deliberately go
// through the on-disk RLE path (load + draw) rather than a hand-rolled
// scaler so the fixtures exercise the exact same code the live game does.
// ---------------------------------------------------------------------------

namespace {

// Subclass exists solely to expose the protected stream-based load() so we
// can hand the encoded buffer in without going through the file system.
class StreamSpriteResource : public Shared::Xeen::SpriteResource {
public:
	void loadFromBuffer(const byte *data, uint32 size) {
		Common::MemoryReadStream stream(data, size, DisposeAfterUse::NO);
		Shared::Xeen::SpriteResource::load(stream);
	}
};

// Fixed 5-color RGBA palette. Index 0 is fully transparent; 1..4 are opaque
// red / green / blue / white. The synthetic test inputs only use indices 1
// and 2; 3 and 4 are reserved so future patterns can mix more colors without
// invalidating earlier fixtures.
struct RGBA { uint8 r, g, b, a; };
static const RGBA kPalette[5] = {
	{   0,   0,   0,   0 }, // 0: transparent
	{ 255,   0,   0, 255 }, // 1: red
	{   0, 255,   0, 255 }, // 2: green
	{   0,   0, 255, 255 }, // 3: blue
	{ 255, 255, 255, 255 }, // 4: white
};

// Encode one 16x16 cell as a single Xeen sprite in the on-disk RLE format
// expected by drawSprite().
//
// Layout:
//   uint16 LE frameCount = 1
//   uint16 LE offset1   = 6                  (cell starts right after index)
//   uint16 LE offset2   = 0                  (no foreground overlay)
//   cell header (8 B): xOffset=0, width=16, yOffset=0, height=16
//   16 rows, each: lineLength(1) xSkip(1) opcode(1) [16 literal palette idx]
//
// Row math (matches the decoder at sprites.cpp:285-340):
//   * lineLength counts xSkip + opcode + payload = 1 + 1 + 16 = 18
//   * opcode = 15 selects cmd 0 / len 15, which reads opcode + 1 = 16 literals
//   * the row body on disk is 19 bytes (lineLength itself is one byte before)
//
// Total file size: 6 (index) + 8 (cell hdr) + 16 * 19 (rows) = 318 bytes.
static const uint32 kSpriteSize = 6 + 8 + 16 * 19;

static void writeUint16LE(byte *p, uint16 v) {
	p[0] = (byte)(v & 0xFF);
	p[1] = (byte)(v >> 8);
}

static void encodeSprite(byte out[kSpriteSize], const byte src[16 * 16]) {
	byte *p = out;

	// Index: frame count, then cell-1 offset and unused cell-2 offset.
	writeUint16LE(p, 1);  p += 2;
	writeUint16LE(p, 6);  p += 2;
	writeUint16LE(p, 0);  p += 2;

	// Cell header.
	writeUint16LE(p, 0);   p += 2; // xOffset
	writeUint16LE(p, 16);  p += 2; // width
	writeUint16LE(p, 0);   p += 2; // yOffset
	writeUint16LE(p, 16);  p += 2; // height

	for (int y = 0; y < 16; ++y) {
		*p++ = 18;  // lineLength: xSkip + opcode + 16 literals
		*p++ = 0;   // xSkip / line xOffset
		*p++ = 15;  // opcode: cmd=0, len=15 -> next 16 bytes are literals
		for (int x = 0; x < 16; ++x)
			*p++ = src[y * 16 + x];
	}

	assert((uint32)(p - out) == kSpriteSize);
}

// Convert a paletted CLUT8 surface to RGBA32 using the fixed palette and
// write it to `path` as a PNG. We do the conversion ourselves rather than
// letting Image::writePNG synthesise it from a CLUT8 + palette, because
// only an RGBA conversion path honours per-index alpha (the CLUT8 PNG mode
// emits a 1-byte/pixel paletted PNG with no transparency).
static bool writeRGBAPng(const Common::Path &path, const Graphics::Surface &paletted) {
	Graphics::Surface rgba;
	rgba.create(paletted.w, paletted.h, Graphics::PixelFormat::createFormatRGBA32());

	for (int y = 0; y < paletted.h; ++y) {
		const byte *srcRow = (const byte *)paletted.getBasePtr(0, y);
		uint32 *dstRow = (uint32 *)rgba.getBasePtr(0, y);
		for (int x = 0; x < paletted.w; ++x) {
			byte idx = srcRow[x];
			const RGBA &c = (idx < 5) ? kPalette[idx] : kPalette[0];
			dstRow[x] = rgba.format.ARGBToColor(c.a, c.r, c.g, c.b);
		}
	}

	Common::DumpFile out;
	if (!out.open(path)) {
		warning("Scaler test: cannot open '%s' for writing",
			path.toString(Common::Path::kNativeSeparator).c_str());
		rgba.free();
		return false;
	}

	bool ok = Image::writePNG(out, rgba);
	out.close();
	rgba.free();

	if (!ok) {
		warning("Scaler test: writePNG failed for '%s'",
			path.toString(Common::Path::kNativeSeparator).c_str());
	}
	return ok;
}

} // anonymous namespace

int ScreenshotHarness::runScalerTest(const Common::Path &outDir) {
	if (outDir.empty()) {
		warning("Scaler test: --mm-scale-test=DIR is required");
		exit(1);
	}

	// Build the two synthetic 16x16 test patterns.
	byte vstripes[16 * 16];
	byte checker[16 * 16];
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 16; ++x) {
			vstripes[y * 16 + x] = (byte)((x % 2) + 1);            // cols alternate 1, 2
			checker[y * 16 + x]  = (byte)(((x + y) % 2) + 1);      // checkerboard 1/2
		}
	}

	struct Pattern {
		const char *name;
		const byte *src;
	};
	const Pattern patterns[2] = {
		{ "vstripes16", vstripes },
		{ "checker16",  checker  },
	};

	for (int pi = 0; pi < 2; ++pi) {
		const Pattern &pat = patterns[pi];

		// 1) Encode the pattern as a Xeen RLE sprite and load it.
		byte encoded[kSpriteSize];
		encodeSprite(encoded, pat.src);

		StreamSpriteResource sprite;
		sprite.loadFromBuffer(encoded, kSpriteSize);
		if (sprite.empty()) {
			warning("Scaler test: failed to load encoded sprite for '%s'", pat.name);
			exit(1);
		}

		// 2) Dump the unscaled input as a 16x16 reference PNG (palette applied
		//    directly, no draw call). mm5e uses this as its input fixture.
		{
			Graphics::Surface input;
			input.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
			for (int y = 0; y < 16; ++y) {
				byte *row = (byte *)input.getBasePtr(0, y);
				memcpy(row, pat.src + y * 16, 16);
			}
			Common::Path p = outDir.appendComponent(Common::String::format("%s_input.png", pat.name));
			if (!writeRGBAPng(p, input)) {
				input.free();
				exit(1);
			}
			input.free();
		}

		// 3) Render at every scale 0..15 to a 64x64 transparent canvas.
		for (int scale = 0; scale < 16; ++scale) {
			Shared::Xeen::XSurface canvas(64, 64);
			canvas.clear(0); // palette idx 0 == transparent in our fixed palette

			// Reach into SpriteResource via a base-class pointer so we hit the
			// public 5-arg overload (the Xeen subclass' override forces
			// scale=0; we want the unmodified shared draw()).
			const Shared::Xeen::SpriteResource *base = &sprite;
			base->draw(canvas, /*frame*/ 0, Common::Point(8, 8),
				/*flags*/ 0, /*scale*/ scale);

			Common::Path p = outDir.appendComponent(
				Common::String::format("%s_scale%02d.png", pat.name, scale));
			if (!writeRGBAPng(p, canvas.rawSurface()))
				exit(1);
		}
	}

	debug("Scaler test: wrote 34 PNGs to %s",
		outDir.toString(Common::Path::kNativeSeparator).c_str());
	exit(0);
}

} // End of namespace Xeen
} // End of namespace MM
