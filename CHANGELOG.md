# Changelog

All releases of THPS 2X Recompiled. The download is on the
[release page](https://github.com/almost-drowned-rat/THPS2X-Recompiled/releases); only the newest release keeps
its download.

## v1.2.4 - 2026-11-13

### Fixed
- **Philadelphia: crash when the cable to the secret area is cut.** v1.2.2 and v1.2.3 tried to throw the skater off
  the cable the moment it goes; on the real cable that crashed the game. The cable behaves as in the original 2X
  again: you keep grinding where it was until the grind ends.

### Added
- **SPECIAL TRICKS INPUT** (GENERAL OPTIONS -> GAMEPLAY, default TIGHT = the original): with LOOSE a trick combo
  such as a special (for example up, down + B) also comes out when a spin button (LT / RT) is pressed between its
  steps, or when the thumb rolls over a diagonal on the D-pad on its way from up to down. With TIGHT both end the
  combo, as in the original, and the plain trick of the button comes instead. One side effect of LOOSE: a diagonal
  grab you roll into from one of its two directions counts as that direction's grab.
- **180 SPIN TAPS: SEPARATE BUTTONS** (GENERAL OPTIONS -> GAMEPLAY): the triggers only spin while held, the quick
  180s come from their own buttons - Black / White with PLAYSTYLE XBOX, LT / RT with PLAYSTYLE PS2. On a keyboard,
  put Black / White on any keys you like in BUTTON MAPPING. ON (the original) and OFF stay as before.

### Changed
- **PLAYSTYLE** and **REMEMBER CAMERA** moved from GENERAL OPTIONS into GAMEPLAY (PLAYSTYLE its first line, REMEMBER
  CAMERA its last), so the settings of how you play are in one place. Your settings are kept.

## v1.2.3 - 2026-11-13

### Fixed
- **Tips at a level's start at 16:9 / 16:10 / 21:9** (the yellow text, HUD AT 16:9: ORIGINAL PROPORTIONS): a long
  line of a tip ("LAND BIG DROPS") was pulled toward the middle, away from the others. All lines stay flush left now.
- **MSAA 2x / 4x at 16:9 / 16:10 / 21:9** (also the Steam Deck): had no effect in a level - the game's 3D scene is
  drawn into its own (twice as wide) image, which got a new size when the level started and lost its MSAA on the
  way. It keeps it now. At 4:3 MSAA worked, but shows little, as the game already smooths the picture sideways itself.
- **Pause menu:** with QUIT GAME and CAMERA OPTIONS the list reached up into the PAUSED logo and down onto the button
  hints. The lines sit a little closer together now, clear of both.

### Added
- **PAUSE MENU** (GENERAL OPTIONS, last line): pick which lines the pause menu shows - CAMERA OPTIONS, QUIT GAME
  and SOUND OPTIONS, each ON (default) or OFF. CONTINUE, RETRY and END RUN always stay. With all three OFF the pause
  menu is the original one.
- **Freeze report:** if the game ever stops drawing for 10 seconds, it writes a short report into `thps2x.log`
  (where the game stands). If the game freezes, wait about 30 seconds before you close it and send the log with
  the issue.

## v1.2.2 - 2026-11-12

### Added
- **REMEMBER CAMERA** (GENERAL OPTIONS, last line, default OFF): the game puts the camera back to its first view
  (the one Back switches away from) at every level start and every retry. With REMEMBER CAMERA ON the view you
  picked with Back stays, also after a restart of the game.
- **CAMERA OPTIONS in the pause menu** (under SOUND OPTIONS): FIELD OF VIEW, REMEMBER CAMERA, FREE CAMERA,
  SENSITIVITY X / Y and INVERT CAMERA of the player who paused, changed in the level. The same settings as in RECOMP
  OPTIONS and RECOMP CONTROL OPTIONS.
  **Note:** a new FIELD OF VIEW takes effect after a RETRY (pause -> RETRY) or at the next level, not in the run
  that is going on.
- **HUD AT 16:9 / 16:10 / 21:9** (RECOMP OPTIONS -> WIDESCREEN): ORIGINAL PROPORTIONS (the widescreen HUD since
  v1.2.0, default) or STRETCHED (the HUD stretched across the screen as before v1.2.0). Applies at once.
- **BRIGHTNESS** (RECOMP OPTIONS -> PICTURE): ORIGINAL, brighter in five steps (+1 to +5) or darker in three
  (-1 to -3). Applies at once, also to menus and movies. (The darker look is the original Xbox game's - it never
  changes the console's display brightness - so ORIGINAL stays as it was.)

### Changed
- **RECOMP OPTIONS** got two sub-lists, because the list was full: **WIDESCREEN** (MENUS AT 16:9 and the new HUD AT
  16:9; only shown with a wide aspect ratio) and **PICTURE** (ANISOTROPIC FILTER, ANTIALIASING, MOTION BLUR and the
  new BRIGHTNESS). Your settings stay as they were.

### Fixed
- **Floating crate at the start of Warehouse** (a bug of the original game, also on xemu): when you knock away a
  crate, the crates standing on it now go with it. Before, the top crate of the stack by the FRAGILE crate stayed in
  the air until you hit it with an ollie.
- **21:9 on a 3440x1440 monitor:** 21:9 now takes the exact aspect ratio of the monitor the game starts on
  (anything from 2.30:1 to 2.42:1, e.g. 3440x1440 = 2.39:1), so the thin black bars at the sides are gone.
  2560x1080 stays as it was.
- **Menus over gameplay at 16:9, 16:10 and 21:9** (pause, end of run, name entry after a high score): these
  screens stay together in the middle again; since v1.2.0's widescreen HUD parts of them (e.g. the ENTER NAME
  letter grid) were pulled toward the edges.
- **Pop-up messages at 120 fps** (goal, tape, gap and other messages in the middle of the screen): at FRAME RATE
  120 they disappeared twice as fast as at 60. They now stay on screen as long as at 60 fps, the same time as on
  the Xbox.

## v1.2.1 - 2026-11-07

### Changed
- **120 fps with V-Sync OFF on any display:** with FRAME RATE 120 and VSYNC OFF the game now runs at 120 fps also on
  a 60 Hz monitor - smaller tears and slightly fresher input, at twice the GPU work. With V-Sync ON, 120 fps still
  needs a display with 120 Hz or more (the menu says "NEEDS 120 HZ OR VSYNC OFF").

### Fixed
- **Grind balance meter** at 16:9, 16:10 and 21:9: its left end was drawn far off to the left, apart from the rest
  of the meter (since v1.2.0's widescreen HUD). The meter is whole again; SCORE, SPECIAL and SWITCH stay at the
  edges.

## v1.2.0 - 2026-11-07

Ultrawide and 16:10, a FIELD OF VIEW option, a proper widescreen HUD - and two bugs of the original game fixed.

### Added
- **ASPECT RATIO 16:10 and 21:9** besides 4:3 and 16:9 (RECOMP OPTIONS): gameplay fills the whole screen of a
  Steam Deck / 16:10 monitor or an ultrawide (21:9 = 2560x1080; 3440x1440 keeps tiny bars), Hor+ - you see more to
  the sides, nothing is stretched. Menus and movies stay 4:3 as in the original (or stretched with MENUS AT ...).
  Applies after a restart.
- **FIELD OF VIEW** (RECOMP OPTIONS, under ASPECT RATIO): ORIGINAL or +5 up to +30 degrees on the game's own camera,
  in every aspect ratio and in split screen. Applies at once.
- **Widescreen HUD:** at 16:9, 16:10 and 21:9 the gameplay HUD keeps its original proportions - SCORE and the
  SPECIAL bar sit at the left edge, SWITCH at the right edge, trick text in the middle. Before, it was stretched
  with the picture.
- **High-score initials are remembered:** the name entry after a high score starts with the initials you entered
  last time instead of AAA, also after a restart.
- **Mouse cursor hides itself** over the game window and comes back as soon as you move the mouse.

### Changed
- **MOTION BLUR** moved from GENERAL OPTIONS to RECOMP OPTIONS (under ANTIALIASING), with the other picture settings.
- **RENDER RESOLUTION** shows the resolution only ("1920X1080"), and after you change ASPECT RATIO it lists the sizes
  of the new aspect ratio right away (applied after the restart).

### Fixed
- **Overwriting a save from OPTIONS -> MEMORY UNIT -> SAVE GAME:** picking an existing save, answering YES to
  "overwrite?" and keeping its name ended in "A file named ... already exists" - a bug of the original game. Now
  the save is overwritten.
- **FPS COUNTER** was missing from RECOMP OPTIONS -> FRAME RATE since v1.1.0 - it is back.
- **120 fps:** the ollie charged twice as fast - a tapped ollie jumped like a held one. Now the same as at 60 fps.
- **MOTION BLUR** changed together with ASPECT RATIO showed the new value but kept the old effect until the next
  change.

## v1.1.0 - 2026-11-05

Play it like the originals: THPS 1 scoring, THPS 2 PS1 physics, and smooth frame pacing on 120 / 180 / 240 Hz.

### Added
- **SCORE CALCULATION THPS 2 / THPS 1** in OPTIONS -> GENERAL OPTIONS -> GAMEPLAY: with THPS 1, tricks score their
  THPS 1 values, the multiplier counts the combo's tricks plus the last air's spin as in THPS 1, there is no switch
  bonus and no perfect / sloppy landing factor, and repeated tricks lose 100 / 75 / 50 / 25 / 10 %. Tricks THPS 1
  did not have get a value scaled from similar tricks (in doubt the lower one); a tapped grab counts half.
- **The classic career's THPS 1 levels always score as in THPS 1** and have no manuals, as in THPS 1 - whatever the
  two switches say. The THPS 2 and 2X parts follow the switches.
- **PHYSICS 2X / ORIGINAL THPS 2 PS1** in GAMEPLAY: the gravity and flat-ground ollie of THPS 2 on the PlayStation
  (2X has about 8 % less gravity and a 12 % stronger ollie). A flat ollie is about 20 % shorter in the air with
  ORIGINAL THPS 2 PS1. Applies at once.
- **FRAME PACING DISPLAY / 60 HZ TIMER** in RECOMP OPTIONS -> FRAME RATE: on a 120 / 180 / 240 Hz display the game's
  60 Hz clock now follows the display's own refresh (DISPLAY, the default), so each frame stays on screen equally
  long - no more judder from a timer that drifts against the display. 60 / 144 / 165 Hz and VRR displays keep the
  timer. Applies after a restart.


A new setup window, a reworked soundtrack menu and a MANUALS switch.

### Added
- **Setup window:** start `THPS 2X Recomp.exe` in an empty folder - pick your disc image and the folder to install
  to, then Go; it builds the game there and Go turns into Play. Install folders with spaces work.
- **Vulkan 1.3 check:** on a PC without a Vulkan 1.3 graphics card the setup and THPS2X.exe now say so in a message
  box (with an apology) instead of failing silently. The README names Vulkan 1.3 as the minimum.
- **MANUALS ON / OFF** in OPTIONS -> GENERAL OPTIONS -> GAMEPLAY: with OFF, no manuals or nose manuals (also not
  out of the air), so no manual points either - as in THPS 1.
- **Pause menu -> SOUND OPTIONS:** SOUNDTRACK now steps through every source (GAME MUSIC, ALL CUSTOM SOUNDTRACKS,
  COMBINED, each of your albums) and a new PLAYBACK MODE line switches IN ORDER / SHUFFLE; both are saved and apply
  at once. New order: SOUNDTRACK, PLAYBACK MODE, SFX LEVEL, MUSIC LEVEL, SELECT TRACK, NEXT TRACK, BACK.

### Changed
- **SOUNDTRACKS moved into GENERAL OPTIONS** (the line after CAREER MODE) and is one list: SOUNDTRACK (GAME MUSIC,
  ALL CUSTOM SOUNDTRACKS, COMBINED, each album), then PLAYBACK MODE with PLAY IN ORDER / SHUFFLE. The 8 old modes
  are gone; a saved old mode is converted.
- **PLAY IN ORDER** goes on with the next song at a song's end, at a new level and at RETRY - it never starts over
  with the first song.
- **GAMEPLAY** is its own list in GENERAL OPTIONS: 180 SPIN TAPS, BIG DROP, WALL BAIL and MANUALS.
- **Boot animation:** the README conversion keeps your video at 1920x1080 and 60 fps in full quality.

## v1.0.0 - 2026-10-31

The first full release: a classic career, a free camera, more gameplay options and the fixes for the GitHub reports
#2, #3, #5 and #6.

### Added
- **Classic career:** OPTIONS -> GENERAL OPTIONS -> CAREER MODE ORIGINAL / CLASSIC (for new careers). A classic
  career plays the THPS 1 levels first (opened by tapes), a Roswell medal opens the THPS 2 levels (their cash
  thresholds count from $0), a Bullring medal the 2X levels. **The THPS 1 part pays no cash, on purpose:** its goals
  are easier than the THPS 2X career's, so as a small challenge the skater cannot be pushed with stat points until
  the THPS 2 part, where cash and the shop work as usual. Free Skate and Single Session list the levels in the same
  order (THPS 1 first) and open each part's levels only when the career has opened that part (The Hangar after the
  Roswell medal, Club London after the Bullring medal). A classic career saved with v1.0.0 and loaded in an older
  version plays on there as an original career.
- **QUIT GAME** as the last line of OPTIONS and in the pause menu (GitHub issue #5): asks first, opens on NO.
- **FREE CAMERA** per player (GitHub issue #6): OPTIONS -> CONTROLS -> RECOMP CONTROL OPTIONS -> PLAYER 1-4 with
  BUTTON MAPPING, FREE CAMERA ON / OFF, INVERT CAMERA (OFF / X / Y / X AND Y), CAMERA SENSITIVITY X and Y (0-10,
  0 locks that axis) and CAMERA RESET SPEED (0 slow to 9 fast, 10 jumps back at once). With it on, the right stick
  turns the camera and it stays where it was turned; CAMERA RESET (BUTTON MAPPING, right stick click by default)
  brings it back.
- **WALL BAIL ORIGINAL / NO BAIL** in GENERAL OPTIONS: with NO BAIL, holding grind into a wall at an angle too steep
  for a wallride (60 degrees or more) no longer bails; the skater meets the wall as without grind.
- **BUTTON CHEATS** lists 30 cheats, each as WHAT IT DOES (CODE), MOON JUMP (RADBLAST) among them.

### Changed
- **GENERAL OPTIONS order:** CAREER MODE is the first value line.

### Fixed
- **Garbage on water, glass and metal surfaces** (GitHub issue #2).
- **TRICK TIPS, SCORE DISPLAY, QUICK CHAT and MOTION BLUR are kept** between starts (GitHub issue #3).

## v0.9.3.1 - 2026-10-29

Better logs for frame-rate reports (Steam Deck report #1). No gameplay changes.

### Changed
- **`data\thps2x.log` starts with the version and the system:** the release and its source commits, then Windows
  with its build, or Wine / Proton with the Linux version under it, and whether it runs on a Steam Deck in Game Mode
  (Gamescope) or Desktop Mode.
- **With the FPS counter on, the 5-second log lines now also show the GPU time per frame**, and the time spent
  handing the picture to the display is split into its two steps (`present ... (acquire ...)`). Before, the GPU time
  needed the `RECOMP_FRAME_TIME=1` launch option.

## v0.9.3 - 2026-10-29

Compatibility fixes from a player report (AMD RX 580 on Windows). No gameplay changes.

### Fixed
- **A clear message instead of a black screen** when the graphics card or its driver lacks a Vulkan feature the
  renderer needs: the game stops and names the card, the driver's Vulkan version and what is missing (the same text
  goes to `data\thps2x.log`). Before, it ran on without a picture.
- **AMD Polaris (RX 400 / RX 500, e.g. RX 580) on Windows is now supported** through a fallback for drivers without
  `VK_EXT_vertex_input_dynamic_state` (the vertex formats are built into the pipelines). Tested with the fallback
  forced, not yet on a real RX 580 - feedback welcome.
- **The log is written when `THPS2X.exe` is started directly** (without the launcher); before, `data\thps2x.log`
  stayed empty.

### Known
- Linux via Proton / Wine is best effort.

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
- The recomp's FPS counter counts the game's own frames; the Steam overlay measures after Gamescope, so the two can differ.
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
