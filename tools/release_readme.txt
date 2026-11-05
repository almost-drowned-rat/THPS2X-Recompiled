THPS 2X Recompiled v{VERSION} (Windows 10/11, 64-bit; Linux via Proton / Wine: best effort)

A static recompilation of Tony Hawk's Pro Skater 2X (Xbox, USA) into native PC code with a Vulkan renderer.
This download contains no game data, no game code and no keys. You need your own copy of the game (USA disc).

AI disclosure:  AI assistance yes (Claude Code by Anthropic).

Start:
  1. Unpack this zip (keep the tools\ folder next to the exe) and start 'THPS 2X Recomp.exe'.
  2. The setup window asks for two things: your disc image (.iso) and the folder to install into.
     Press Go.
  3. No internet needed: the compiler (llvm-mingw, in tools\download\, checked by its SHA-256) is copied
     along and unpacked, then the game's code is translated and compiled on your PC - about 2 GB of disk
     space and a few minutes. Then press Play. Later, start 'THPS 2X Recomp.exe' in the install folder -
     it goes straight into the game. Only if tools\download\ is missing is the compiler downloaded
     (about 190 MB).
  Your disc image is only read; it is never changed or deleted.

Requirements: a graphics card with at least Vulkan 1.3 - without one, the setup and the game show a
message saying so; your own Tony Hawk's Pro Skater 2X (USA) disc
(default.xbe SHA-1 469818E015AED2730641C0BDCBD2557D8A6B8BB4).

Linux via Proton / Wine (best effort, less tested than Windows): needs Vulkan (winevulkan + the host's
Vulkan driver), 64-bit. The first start is slower than on Windows.

Windows warning: the exe is not signed. SmartScreen shows 'Windows protected your PC' - click
'More info' -> 'Run anyway'. Smart App Control (Windows 11) blocks unsigned apps completely; the port can
only run with Smart App Control turned off.

Everything stays in its folder: no registry, no AppData, no shortcuts. Saves: saves\, logs: data\.
Full documentation, source and build instructions: https://github.com/almost-drowned-rat/THPS2X-Recompiled
Toolkit source: https://github.com/almost-drowned-rat/xboxrecomp-vulkan
Licences: LICENSES\ (the port GPL-3.0-or-later; the toolkit MIT; llvm-mingw Apache-2.0 with LLVM exception;
third-party parts under their own licences).

Not affiliated with or endorsed by Activision, Neversoft or Treyarch. Tony Hawk's Pro Skater and all related
names and trademarks belong to their respective owners.
