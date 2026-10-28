# Changelog

All releases of THPS 2X Recompiled. The download is on the
[release page](https://github.com/almost-drowned-rat/THPS2X-Recompiled/releases); only the newest release keeps
its download.

## v0.9.2 - 2026-10-28

Fixes and wishes from player reports on the Steam Deck (Proton) and on Arch Linux (Wine), new gameplay options,
the button cheats as a list, and diagnostics for frame-rate reports.

### New
- **GENERAL OPTIONS** is now one list: the game's own Trick tips, Score display, Quick chat and Motion blur, plus
  the new gameplay options - **Playstyle** XBOX / PS2 (spins on Black / White, i.e. LB / RB on a modern pad;
  nollie and switch stay on the triggers), **180 spin taps** ON / OFF, **Big drop** ORIGINAL / NO BAIL / OFF.
  Every one defaults to the original game; **Y puts the selected line back to its default.**
- **RECOMP OPTIONS -> BUTTON CHEATS:** the game's button cheats as a list (23 of them; each line says what it does,
  ON / OFF, any combination; Y switches the selected one off). No typing codes with LT held.
- **Diagnostics for frame-rate reports:** with the FPS counter on (or the launch option `RECOMP_FPS_LOG=1`),
  `data\thps2x.log` gets an `[fps]` line every 5 seconds with the time split of a frame (game, the game's wait for
  the display, graphics translation, GPU, present) and a `[hitch]` line per frame over 50 ms naming its cause
  (shader compiles, textures, file reads, sleeps). The start line no longer says "display 0.00 Hz" before the
  display's rate is known.

### Fixed
- **A 3-second busy wait after a save is loaded:** the game holds its screen for 3 s there by polling the clock
  in a loop - one CPU core fully loaded (on the Steam Deck tens of millions of calls between the intro and
  Select Player). It now sleeps through those 3 s; the screen shows as long as before.
- **Saves under Proton / Wine:** the first save worked, every later one said "the memory unit was removed during
  use", and saves and replays could not be deleted. Overwriting a save deletes the old one first; under Wine the
  port could not list the save's folder while the game held it open, so nothing was deleted and the new save
  collided with the old folder. The folder is now listed through the game's own handle. Tested under Wine 9.0:
  three saves in a row, saving in a level over an existing save, deleting a save from the menu.
- **Start under Proton / Wine:** the game's memory window is checked as a whole before it is used; if a part of
  it would land on memory Wine already uses, another place is taken. If none fits, the game stops at once with a
  message and a reason in `data\thps2x.log`, instead of crashing after the intro.

### Notes
- Overwriting a save from **Options -> Load/Save -> Save Game** keeps refusing an existing name ("already
  exists"), as the game's own menu logic does (the same on an emulator - thanks to the tester who checked); save
  over a career from the end-of-run menu (SAVE CAREER), or pick a new name.
- Our FPS counter counts the game's own frames; the Steam overlay measures after Gamescope, so the two can differ.
- THPS 2X shows no interpolated or generated frames: 60 and 120 fps are the game's own frames.

## v0.9.1.1 - 2026-10-27

Linux via Proton / Wine and an offline first start. The game itself is unchanged.

**Tested on Arch Linux with Proton 11.0-2 and Wine 11.20:** install, first start and the game work.

### Fixed
- **Crash at the title screen under Proton / Wine:** the game's memory window is now placed where all of it is
  free. Under Wine two of its parts had landed on memory Wine already uses, and the first title-screen drawing
  ended the game. Windows is unchanged.
- **"No internet" while downloading the compiler (Proton / Wine):** the launcher no longer runs Windows' own
  `curl.exe`; the download runs in-process (WinHTTP, the system's proxy and certificate settings). There is no
  "is the network up" check any more: a failed download names the real reason - name lookup (DNS), connection,
  secure connection (TLS) or the server's HTTP answer - and offers **Retry**; short network hiccups are retried
  automatically.
- **Unpacking the compiler failed (it expected `tar.exe`):** the compiler zip is unpacked in-process by the
  port's own zip reader (every file checked by its CRC-32).

### Changed
- **No internet needed at the first start:** the release zip now carries the compiler
  ([llvm-mingw](https://github.com/mstorsjo/llvm-mingw) 20260908, the unchanged upstream zip, Apache-2.0 with LLVM
  exception) in `tools\download\`. The launcher checks its SHA-256 and unpacks it; only if the file is missing is
  it downloaded. The zip is about 210 MB (was 23 MB) - the same amount as before, all in one download.
- The first start uses no external system tools at all (only the bundled Python and the compiler).
- README: *Linux via Proton / Wine* with the tested versions and known limits.

### Update from v0.9.0
Unpack the new zip over your old folder (keep `tools\` next to the exe): the launcher unpacks its new files and
builds the game once more. Your `saves\` and `media\` stay.

## v0.9.0 - 2026-10-27

The first public release: *Tony Hawk's Pro Skater 2X* (Xbox, USA) as a native Windows port with a Vulkan
renderer.

- The whole game: career, single session, free skate, create-a-skater, park editor, 2-player split screen.
- **RECOMP OPTIONS** in the game's Options menu: display mode, render resolution up to 5x, real 16:9 widescreen,
  anisotropic filter, FXAA / SMAA / MSAA, 60 or 120 fps, V-Sync, FPS counter, RetroArch slang shaders, boot
  animation.
- **SOUNDTRACKS:** your own music (MP3, FLAC, Ogg Vorbis, WAV) with 8 playback modes.
- **CONTROLS:** keyboard and every gamepad, remappable per device.
- The launcher: one exe that installs the game from your disc image, downloads the compiler, translates and
  compiles the game on your PC.
