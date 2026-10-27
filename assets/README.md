# assets/ – the port's icon

The approved icon (maintainer, 2026-10-27): a plain skateboard, an original drawing - no game logo, cover art,
title lettering or Activision / Neversoft branding (toolkit porting guide M6, "Icon").

| File | Use |
|---|---|
| `icon.svg` | the source drawing |
| `icon.ico` | the exe's icon in Explorer: 16, 32, 48 and 256 px (PNG entries), embedded through `thps2x.rc` (`llvm-rc`) |
| `icon_64.bmp` | the window / taskbar icon: 64x64, 32-bit with alpha (BITMAPV4, BI_BITFIELDS); CMake embeds its bytes (`port_icon.h`), `main.c` hands them to the toolkit (`xbox_set_window_icon`) before the first window |

The `.ico` and the BMP were drawn from the same shapes at each size (not scaled down from one image).
