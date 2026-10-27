# Input (M3)

## How a key press reaches the game

```
SDL3 gamepads (hot-plug, rumble) ──────────────────────────────┐
SDL3 window (nv2a_vk) ──key edges──▶ xbox_InputKeyEvent ───────┼─▶ xbox_InputGetState ─▶ XInput HLE (toolkit src/xdk) ─▶ game
--input-script / --input-live (pad script) ────────────────────┘          └─(RECOMP_XINPUT_HLE=0)─▶ usb_gamepad_report ─▶ OHCI model ─▶ XPP
```

- **XInput at API level (stage 3, B2):** the game's XPP entry points (XInitDevices, XInputOpen/Close/GetState/SetState/
  Poll/GetCapabilities; addresses in `src/recomp_manual.c`) are overrides that call the toolkit's XDK HLE layer
  (`src/xdk/xdk_xinput.c`). It reads the input layer directly – no USB enumeration, no OHCI frames. XGetDevices and
  XGetDeviceChanges stay game code; the HLE keeps their device-type record (0x00216AA0) current, so hot-plug works.
- `RECOMP_XINPUT_HLE=1` and `RECOMP_KEYBOARD=1` are defaults in `main.c`. `RECOMP_XINPUT_HLE=0` goes back to the
  game's own USB driver on the emulated OHCI controllers (then `RECOMP_USB=1`; XPP 3911 maps HC0 root ports 0/1/2/3 to
  ports 2/3/0/1, so `main.c` sets `RECOMP_USB_NDP=4`, `RECOMP_USB_PORT=2`).
- **Controllers (toolkit `src/input`, SDL3 on every OS):** Xbox, PlayStation, Switch and generic pads; hot-plug
  (re-enumerated at most every 500 ms); rumble through `XInputSetState`. Host pads take free ports in the order SDL
  reports them and keep them while attached. Buttons are positional (A = south button).
- **Ports:** the keyboard is merged on top of port 0 (player 1). `RECOMP_KEYBOARD_PORT=<0-3>` makes it a controller of
  its own on that port, which host pads skip: keyboard + one pad = two players. `RECOMP_PADS=<n>` reports ports 0..n-1
  as connected (tests); a script step for `p2-` connects port 1.
- **Multiplayer (B3):** after Press Start the game listens only to the pad that pressed it, so its main-menu check
  counted one controller and, with no network link, greyed Multiplayer out. An override of the link check at this one
  call site (`sub_001BECBA`, return address 0x001238EC) answers "link" when two or more controllers are connected.
  The kernel still reports no link: SYSTEM LINK stays greyed; 2 PLAYER (3/4 PLAYER with more controllers) works.
- Closing the window ends the process (log drained, exit code 0).

## Keyboard (player 1) – default layout, keep it stable

| Key | Xbox pad |
|---|---|
| W A S D | left stick |
| arrow keys | D-pad |
| Enter (also keypad Enter) / Backspace | Start / Back |
| I / J / K / L | Y / X / A / B |
| U / O | Black / White |
| Z / P | left / right trigger (key = fully pressed) |
| – | right stick, L3/R3: unassigned for now |

Keys are **physical positions** (USB HID usage IDs = SDL scancodes), not labels: the layout feels the same on every
keyboard layout. On a German QWERTZ keyboard the left-trigger key is the one labelled **Y** (US "Z" position).
Layout table: `src/input.c`. The keyboard is merged on top of host pad 0, so a real pad keeps working.

## Rebinding (button bindings per device; toolkit `src/input/xinput_bind.{h,c}`)

- **Devices:** the keyboard (sources = HID usage IDs / SDL scancodes, the physical key) and each host pad model,
  identified by its **SDL GUID** (sources = SDL positional names). **Identical pads share one binding** (same GUID;
  maintainer, 2026-10-25) - two of the same controller always behave alike, whichever port they are on.
- **What can be bound:** D-pad, Start, Back, L3/R3, A B X Y, Black/White, both triggers; on the keyboard also the stick
  half-axes. **Pad sticks stay fixed** (maintainer, 2026-10-25). Up to two sources per control.
- **Defaults:** the keyboard layout above (`src/input.c`), pads positional (south = A, east = B, west = X, north = Y,
  shoulders = Black/White, triggers = triggers). Without a stored binding nothing changes.
- **Rule (maintainer, 2026-10-25): the own binding wins.** In menus a source of the default binding also acts, but only
  when the device's own binding does not use that source - the default layout keeps navigating after a bad binding
  (no lock-out), and a key moved to another control never triggers two (A on the default B key L: L is A only, B is not
  triggered). In gameplay only the own binding acts. "Menu" = the level limiter did not run (`s_in_level`,
  `recomp_manual.c`); the in-level pause menu counts as gameplay.
- **Stored** in `saves\settings.ini`, only what differs from the default: `bind.keyboard=a:sc15,b:sc19`,
  `bind.pad.<GUID>=a:east,b:south` (controls: `up down left right start back lthumb rthumb a b x y black white lt rt
  lleft lright ldown lup rleft rright rdown rup`; keyboard sources `sc<HID>`, pad sources `south east west north back
  start lstick rstick lshoulder rshoulder dup ddown dleft dright ltrigger rtrigger`, `none` unbinds, `+` adds a second
  source). The log says where a binding came from (`INPUT: keyboard binding from settings: ...`).
- **Env overrides:** `THPS_BIND_KEYBOARD=<map>`, `THPS_BIND_PAD_<GUID>=<map>`, `THPS_BIND_PAD=<map>` (every pad), and
  `THPS_BIND_RESET=1` ignores the stored bindings for that run. The pad GUID is logged when a pad is opened
  (`INPUT: pad "..." on port n, GUID ...`).
- **Duplicates** (one source on two controls) are detected (`xbox_bind_duplicates`) and shown in the menu (`DOUBLE`); not refused.
- **Menu (maintainer's layout, 2026-10-25):** Options -> CONTROLS -> BUTTON MAPPING (the fifth line under PLAYER 1-4
  CONTROLS, which stay the game's own Autokick/Vibration screens). One screen: `PLAYER: n` (left/right or A: 1-4),
  `DEVICE:` the keyboard (it plays on port 0 = player 1, `RECOMP_KEYBOARD_PORT` moves it) or the controller on that
  player's port (left/right or A), `RESET TO DEFAULT`, then that device's bindings, 8 per page, `NEXT PAGE`. A on a
  binding records a press: release everything, then press the new key/button (5 s without a press cancels; the game
  sees no input meanwhile). Saved at once. Not in the game's PLAYER n CONTROLS screen: it keeps per-line state for
  three lines only (a fourth selectable line crashed; `THPS_CTL_PANEL_LINE=1` shows the old, inert line).
- **Front end only:** BUTTON MAPPING is reachable from the front-end Options only - the in-level pause menu has no
  Options entry (CONTINUE, RETRY, SOUND OPTIONS, END RUN), and the menu code refuses the list while a level runs
  (`s_in_level`). The pause menu has no lock-out protection (maintainer, 2026-10-25: the front end is protected).
- **Emergency exit:** start once with `THPS_BIND_RESET=1` (stored bindings ignored for that run), then RESET TO
  DEFAULT in BUTTON MAPPING; or delete the `bind.` lines from `saves\settings.ini`. Also in the README.
- **Tests:** toolkit `tests/input_bind` (defaults, text form, duplicates, the rule incl. the A-on-default-B case);
  end-to-end with `key=<HID>` script steps (below).

## `--input-script <file>` (automated runs)

The toolkit's pad script (`src/input/xinput_script.c`). One entry per line (or comma separated), `#` starts a comment:

```
<ms>:<button>[+<button>...][:<hold ms>]      default hold 150 ms
3000:start:200
24000:a:200
30000:down+a
40000:p2-start          # "p2-" .. "p4-": that controller
0:p2-connect            # player 2 connected, nothing pressed
```

- Times count from the first pad read of the game (about 1 s after process start).
- Every controller the script names counts as connected from the start.
- Buttons: `up down left right start back lthumb rthumb` (digital), `a b x y black white lt rt` (analog, fully
  pressed), `lleft lright lup ldown rleft rright rup rdown` (stick at full throw).
- Every step is logged when it fires: `PAD: t=24000 ms step 5 (digital 0x00 analog 0x01)`.
- `--input-live <file>`: the same without times – each line appended to the file while the game runs is pressed as
  soon as it is read (lines read together fire one after another, 200 ms apart). Useful for exploring menus by hand
  from a script or an agent.
- `<ms>:key=<HID usage ID>[:<hold ms>]` holds a physical key (e.g. `key=15` = L) - read like a real key, so it goes through the keyboard binding (rebinding tests); plain button names act on the pad state after the bindings.
- `<ms>:shot=<name>` saves the next presented frame as `<RECOMP_SHOT_PREFIX or shot_><name>.bmp` (in `data/` by
  default); `tools/dump_png.ps1` turns the BMPs into 4:3 PNGs.
- Examples: `tests/input/main_menu.txt` (movies → title → main menu → Career),
  `tests/input/menu_tour.txt` (two controllers; every main-menu entry and back, Multiplayer sub-menu, 13 screenshots).

```
THPS2X.exe --max-seconds 45 --input-script tests\input\main_menu.txt
```

## Diagnostics

| Variable | Effect |
|---|---|
| `RECOMP_INPUT_DIAG=1` | once a second: pad 0 as the game sees it (buttons, analog bytes, sticks) |
| `RECOMP_USB_STATS=1` | (LLE path) periodic OHCI stats: reports sent, control transfers, endpoint lists |
| `RECOMP_USB_TRACE=1` | (LLE path) every OHCI register access and transfer (enumeration debugging) |
| `RECOMP_PAD_PRESS=0x10,3000,250` | pulse a digital mask (here Start) every 3 s for 250 ms |
| `RECOMP_PAD_TRACE=1` | every change of what the game's `XInputGetState` returns, plus `XInputOpen/Close` and device changes (`recomp_manual.c`) |
| `RECOMP_USB_REPORT_TRACE=1` | (LLE path) every change of the USB report as sent (toolkit) |

Press times: hold 300 ms in scripts – Select Player polls the pad less often than the 2D menus, 150 ms presses
were missed there.
