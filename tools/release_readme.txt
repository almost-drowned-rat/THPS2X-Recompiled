THPS 2X Recompiled v{VERSION} (Windows 10/11, 64-bit)

A static recompilation of Tony Hawk's Pro Skater 2X (Xbox, USA) into native PC code with a Vulkan renderer.
This download contains no game data, no game code and no keys. You need your own copy of the game (USA disc).

AI disclosure:  AI assistance yes (Claude Code by Anthropic).

Start:
  1. Unpack this zip (keep the tools\ folder next to the exe) and start 'thps2x_launcher.exe'.
  2. The setup window asks for two things: your disc image (.iso) and the folder to install into.
     Press Go.
  3. No internet needed: the compiler (llvm-mingw, in tools\bundle\, checked by its SHA-256) is copied
     along and unpacked, then the game's code is translated and compiled on your PC - about 2 GB of disk
     space and a few minutes. Then the game starts. Later, start 'thps2x_launcher.exe' in the install
     folder - it goes straight into the game (the game itself is data\thps2x.exe). Nothing is ever
     downloaded: if tools\bundle\ is missing or damaged, download this release zip again.
  Your disc image is only read; it is never changed or deleted. Quitting during the installation asks
  first and stops cleanly.
  Updating from v1.2.x: run the new setup into your old install folder. The old 'THPS 2X Recomp.exe'
  there is removed; your saves, settings and custom soundtracks stay. v1.2.8 stays available on the
  release page as a fallback.

Requirements: a graphics card with at least Vulkan 1.3 - without one, the setup and the game show a
message saying so; your own Tony Hawk's Pro Skater 2X (USA) disc
(default.xbe SHA-1 469818E015AED2730641C0BDCBD2557D8A6B8BB4).

Settings: in the game's own options menu, RECOMP OPTIONS: DISPLAY (display mode, render resolution,
aspect ratio, stretch cutscenes / menus / HUD at a wide aspect, field of view), POST PROCESSING
(antialiasing, texture filter, motion blur, brightness, upscaling with AMD FSR 1.0), PERFORMANCE (frame
rate, VSync, FPS / frame time / game speed on screen), RENDERING (shader compile, shader notice), BUTTON
CHEATS, BOOT ANIMATION. Y puts a line back to its default.
Custom soundtracks and your own boot animation: see media\README.txt in the install folder.

Linux: a native Linux version (AppImage or .tar.gz) is a separate download on the release page.

Windows warning: the exe is not signed. SmartScreen shows 'Windows protected your PC' - click
'More info' -> 'Run anyway'. Smart App Control (Windows 11) blocks unsigned apps completely; the port can
only run with Smart App Control turned off.

Everything stays in its folder: no registry, no AppData, no shortcuts. Saves: saves\, logs: data\.
Full documentation, source and build instructions: https://github.com/almost-drowned-rat/THPS2X-Recompiled
Toolkit source: https://github.com/almost-drowned-rat/xboxrecomp-option-c
Licences: LICENSES\ (the port GPL-3.0-or-later; the toolkit MIT, its xemu parts LGPL-2.1-or-later with their
source in tools\licenses\source\ after the setup; llvm-mingw Apache-2.0 with LLVM exception; third-party parts
under their own licences).

Not affiliated with or endorsed by Activision, Neversoft or Treyarch. Tony Hawk's Pro Skater and all related
names and trademarks belong to their respective owners.
