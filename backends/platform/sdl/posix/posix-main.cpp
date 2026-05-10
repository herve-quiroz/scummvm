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

// Needed so the screenshot-harness mode can force SDL_VIDEODRIVER=dummy
// before g_system->init() opens a window.
#define FORBIDDEN_SYMBOL_EXCEPTION_setenv

#include "common/scummsys.h"

#if defined(POSIX) && !defined(MACOSX) && !defined(SAMSUNGTV) && !defined(MAEMO) && !defined(OPENDINGUX) && !defined(OPENPANDORA) && !defined(PLAYSTATION3) && !defined(PSP2) && !defined(NINTENDO_SWITCH)  && !defined(__EMSCRIPTEN__) && !defined(MIYOO) && !defined(MIYOOMINI) && !defined(SAILFISH)

#include <stdlib.h>
#include <string.h>

#include "backends/platform/sdl/posix/posix.h"
#include "backends/plugins/sdl/sdl-provider.h"
#include "base/main.h"

int main(int argc, char *argv[]) {

	// Screenshot harness mode: when --screenshot=PATH, --mm-scale-test=DIR,
	// or --mm-screenshot-prefix=PATH is on the command line, force the
	// dummy SDL video driver so no game window pops up while the harness
	// runs and exits. Honour any SDL_VIDEODRIVER the user already set (so
	// they can pick "offscreen" or another driver if they prefer). The
	// --mm-scale-test path also short-circuits inside scummvm_main before
	// initBackend; the dummy driver here is belt-and-suspenders against
	// the SdlWindow created by g_system->init() above.
	for (int i = 1; i < argc; ++i) {
		if (argv[i] && (strncmp(argv[i], "--screenshot=", 13) == 0
				|| strncmp(argv[i], "--mm-scale-test=", 16) == 0
				|| strncmp(argv[i], "--mm-screenshot-prefix=", 23) == 0)) {
			setenv("SDL_VIDEODRIVER", "dummy", 0);
			break;
		}
	}

	// Create our OSystem instance
	g_system = new OSystem_POSIX();
	assert(g_system);

	// Pre initialize the backend
	g_system->init();

#ifdef DYNAMIC_MODULES
	PluginManager::instance().addPluginProvider(new SDLPluginProvider());
#endif

	// Invoke the actual ScummVM main entry point:
	int res = scummvm_main(argc, argv);

	// Free OSystem
	g_system->destroy();

	return res;
}

#endif
