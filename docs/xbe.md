# THPS 2X – XBE analysis (M0)

Source: `tools\m0_analyze.ps1` (upstream `tools.xiso` + `tools.xbe_parser`) on
`Tony Hawk's Pro Skater 2x (USA).iso`, 2026-10-13. Raw output: `docs/xbe-raw.txt` (local only, gitignored –
contains the certificate keys) and `private\thps2x\` (xbe, `files.txt`, `xbe.json`).

## Identity

| Field | Value |
|---|---|
| Title | Tony Hawk's Pro Skater 2X |
| Title ID | `0x41560001` (matches Xbox-Original-GameList) |
| Region / media | North America, DVD_X2, disc 0, version 1, retail |
| `default.xbe` SHA1 | `469818E015AED2730641C0BDCBD2557D8A6B8BB4` (version identification for the installer) |
| Size | 2 727 936 bytes on disc, image size 0x3198E0 (3.10 MB) |
| Build / cert date | 2001-11-02 00:48:54 UTC / 2001-11-02 18:36:41 UTC |
| Debug path | `C:\th2\TH2_SRC\XBox_Bin\th2_xbox.exe` |

## XDK build: 3947 (mixed libraries)

| Library | Version |
|---|---|
| D3D8 | **1.0.3947** |
| DSOUND | 1.0.3925 |
| XAPILIB, D3DX8, XGRAPHC, XBOXKRNL, XNETS, LIBC | 1.0.3911 |

The highest version (D3D8 3947) matches the GameList entry. DSOUND 3925 is the same build as DOA3 →
`refs/doa3-recomp` is the first stop for DSOUND/kernel quirks; D3D8 3947 is slightly newer than DOA3's 3925.

## Memory layout

- Base `0x00010000`, entry point `0x001B9FD3`, kernel thunk table `0x0021E580` (start of `.rdata`);
  both decoded with the retail XOR keys.
- Stack commit 0x15000, heap reserve 0x100000 / commit 0x1000.
- TLS: no template data, 12 bytes zero fill, index at `0x002A1250`.

## Sections (13)

| # | Name | VirtAddr | VirtSize | RawSize | Flags | Note |
|---|---|---|---|---|---|---|
| 0 | D3D | 0x00011000 | 0x17050 | 0x13DD4 | W PRE X | XDK D3D8, statically linked → needs signatures/HLE or lifting |
| 1 | D3DX | 0x00028060 | 0x1F520 | 0x1F518 | W PRE X | |
| 2 | DSOUND | 0x00047580 | 0x20088 | 0x1FF54 | W PRE X | |
| 3 | WMADECXM | 0x00067620 | 0x01520 | 0x0151C | W PRE X | WMA decoder XMO |
| 4 | .text | 0x00068B40 | 0x189110 | 0x189110 | PRE X | game code (~1.5 MB) incl. Bink |
| 5 | WMADEC | 0x001F1C60 | 0x19A60 | 0x19A60 | W PRE X | |
| 6 | XGRPH | 0x0020B6C0 | 0x02470 | 0x02470 | W PRE X | |
| 7 | XNET | 0x0020DB40 | 0x08EB9 | 0x08EB8 | PRE X | |
| 8 | XPP | 0x00216A00 | 0x07B68 | 0x07B68 | W PRE X | XInput/peripherals |
| 9 | .rdata | 0x0021E580 | 0x1B8E1 | 0x1B8E0 | PRE X | starts with the kernel thunk table |
| 10 | .data | 0x00239E80 | 0xE64A4 | 0x641FC | W PRE X | ~0x822A8 bytes BSS (zero fill) |
| 11 | DOLBY | 0x00320340 | 0x06D98 | 0x06D84 | PRE X TRO | APU DSP code (Dolby Digital encoder) |
| 12 | $$XTIMAGE | 0x003270E0 | 0x02800 | 0x02800 | INS HRO TRO | title image, not preloaded |

All code sections are preloaded; `XeLoadSection`/`XeUnloadSection` are imported (XDK default) but there is no
non-preloaded code section.

## Kernel imports (167)

| Group | n | Notable |
|---|---|---|
| Nt | 45 | file I/O incl. `NtWriteFileGather`, `NtQueryDirectoryFile`, `NtFsControlFile`, io completion, timers, APCs |
| Ke | 29 | `KeConnectInterrupt`/`KeInitializeInterrupt`/DPCs (early D3D handles the GPU interrupt itself), timers, waits, `KeTickCount`, `KeQueryPerformanceCounter` |
| Rtl | 17 | critical sections, strings, `RtlRaiseException`/`RtlUnwind` (SEH) |
| Mm | 14 | contiguous memory, `MmClaimGpuInstanceMemory`, `MmPersistContiguousMemory`, `MmSetAddressProtect`, `MmGetPhysicalAddress` |
| Xc | 10 | SHA/HMAC/RC4/DES/ModExp → save signatures (`XCalculateSignature`) |
| Ex, Hal, Io | 9 each | `ExQueryNonVolatileSetting` (answer US/English/NTSC), `HalBootSMCVideoMode`, `HalReturnToFirmware`, `HalDiskSerialNumber`, `IoCreateDevice`/symbolic links, `IoDismountVolumeByName` |
| Other | 7 | `DbgPrint`, `DbgBreakPoint`, `FscGet/SetCacheSize`, `LaunchDataPage`, `PhyGetLinkState`, `PhyInitialize` |
| Xbox | 6 | `XboxHardwareInfo`, `XboxHDKey`, `XboxSignatureKey`, `XboxLANKey`, `XeLoad/UnloadSection` |
| Av | 4 | `AvSetDisplayMode`, `AvSendTVEncoderOption`, `AvGet/SetSavedDataAddress` |
| Ob, Ps | 3 each | `PsCreateSystemThreadEx`, `ObOpenObjectByName`, `ObReferenceObjectByHandle` |
| Kf | 2 | `KfRaiseIrql`, `KfLowerIrql` |

Full list: `docs/xbe-raw.txt` (local).

## Video and audio formats

- **Movies: Bink**, no XMV. 58 `.bik` files on the disc (`movies/`: publisher/developer logos ATVISION,
  Treyarch, Nevrsoft, `intro.bik`, one video per pro skater, bails, level intros). The decoder is statically
  linked in `.text` (strings "Copyright (C) 1998-1999 RAD Game Tools", "Not a Bink file.", "Bink Logo")
  → early Bink 1. Reference: XboxXBE-Recomp (Bink). The toolkit XMV player does not apply.
- **Music:** 48 WMA tracks in `songs/`, decoded by the in-XBE WMA decoder (`WMADEC`, `WMADECXM`).
- **Custom soundtracks:** strings `SOUNDTRACK` and `%s%04x\%08x.WMA` (= `E:\TDATA\fffe0000\music\<st>\<song>.wma`
  layout) → the game uses the XAPI soundtrack API → hook point for the subfolders of `media/` (template: doaxvb-re).
- **SFX:** 26 `.xsb` banks in `sfx/` (DirectSound, not XACT).
- Textures `.dds` (3333), models/levels in PSX-era formats (`.psx`, `.psh`, `.ddx`/`.ddm`, `.anim`, `.bon`),
  fonts `.fon`, 10 precompiled `.xvu` vertex shaders in `shaders/`. Disc total 1.22 GB, 4517 entries.

## XDK entry points used by the game (stage 3, for the HLE layer)

**XPP 3911 (XInput) – bound to the toolkit HLE** (`src/recomp_manual.c`): XInitDevices 0x0021782B, XGetDevices
0x00217830, XGetDeviceChanges 0x00217852, XInputOpen 0x00218098, XInputClose 0x0021810D, XInputGetCapabilities
0x00218119, XInputGetState 0x0021830B, XInputSetState 0x0021837C, XInputPoll 0x002183B4; XDEVICE_TYPE_GAMEPAD record
0x00216AA0 (memory unit 0x00216A20, a third type 0x00216AB8).

**DSOUND 3925 – bound to the toolkit HLE** (`src/recomp_manual.c`, stage 3 B1; SDL3 mixer, `RECOMP_DSOUND_HLE=0`
= the library on the emulated APU). Direct calls from three game modules – SFX (0x001B2xxx–0x001B4xxx: 64 mono
buffers, 8/16-bit PCM at 44000 Hz, `SetBufferData` into the loaded banks), Bink's DirectSound backend
(0x001D38xx–0x001D40xx: one stereo 16-bit 44100 Hz buffer per movie, `SetBufferData` + `Lock` ring, polled with
`GetCurrentPosition`) and a stream path (0x001AExxx; not used on the menu tour). Wrappers adjust the interface
pointer: IDirectSound `this-8`, IDirectSoundBuffer `this-0x1C`, stream `this+4`.

| VA | Function |
|---|---|
| 0x0004A19C | DirectSoundCreate (game: `DirectSoundCreate(0, &g_pDS 0x0030A724, 0)` at 0x001B29C3) |
| 0x0004A278 / 0x0004A067 | DirectSoundCreateStream / IDirectSound_CreateSoundStream |
| 0x00049325 | DirectSoundDoWork |
| 0x000477CE / 0x000477E4 / 0x0004A043 | IDirectSound AddRef / Release / CreateSoundBuffer |
| 0x0004781A, 0x00049ABC, 0x00049AE0, 0x00049137 | IDirectSoundBuffer Release, Play(r1, r2, flags), Stop, GetStatus |
| 0x00049153, 0x00049173, 0x0004918F | GetCurrentPosition, SetCurrentPosition, Lock (7 args) |
| 0x00049AF8, 0x00049117, 0x00049A68, 0x00048E9C | SetBufferData, SetLoopRegion, SetFrequency, SetVolume |
| 0x000491DF, 0x0004931B | IDirectSoundStream SetVolume, Pause (1 pause / 0 resume) |
| 0x0021EAB0 (vtable) | CDirectSoundStream XMediaObject: AddRef 0x00049972, Release 0x00049982, GetInfo 0x000485EF, GetStatus 0x00048694, Process 0x000486C9, Discontinuity 0x0004863A, Flush 0x00048667; second vtable 0x0021EAA4 |
| 0x0004ABD8 | wave-file media object (file I/O through XAPI only) – stays game code |
| 0x000477FA | stub in the library itself (returns 0) – stays game code |

Before B1 the library needed the emulated APU: without `RECOMP_AC97_READY` it divided by zero (`sub_0004C924`)
once a second controller was connected – and with the APU it mixed silence (`RECOMP_APU_WAV`, menu tour).

## Notable points

1. **PSX heritage:** the debug path `th2_xbox.exe` and `.psx` asset names show a port of the THPS2 PSX
   codebase (Treyarch). Expect PSX-style frame logic.
2. **Frame rate / logic coupling: still open.** Strings "%3.f FPS" and "... in the air for %d ticks" suggest
   tick-based logic; must be confirmed at runtime (M1+) via the present/vblank path
   (`KeTickCount`, D3D present interval). Original rate (30/60) likewise open.
3. **Early D3D:** D3D8 3947 connects its own GPU interrupt (`KeConnectInterrupt`, DPCs) – kernel must
   emulate or bypass this (compare burnout3 `docs/boot.md`, doa3-recomp).
4. **System Link only:** XNETS + `PhyGetLinkState`/`PhyInitialize`, no XOnline strings → report "no link".
5. **Save signing:** `XboxSignatureKey`/`XboxHDKey` + Xc crypto → saves are signed; the kernel must provide
   consistent keys (doaxvb-re saves).
6. **Leftovers on the disc:** Visual SourceSafe files (`vssver.scc`, 32), `test.bin` (0 bytes),
   `tweak.dat`, `prefs.dat`, `CDParks.txt` – harmless, but `tweak.dat`/`prefs.dat` may be worth a look later
   (debug tweaks).
7. `.data` has ~520 KB of BSS – the loader must zero-fill the gap between raw and virtual size.
