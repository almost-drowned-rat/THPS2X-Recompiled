# Changelog

All releases of THPS 2X Recompiled. The download is on the
[release page](https://github.com/almost-drowned-rat/THPS2X-Recompiled/releases); only the newest release keeps
its download.

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
