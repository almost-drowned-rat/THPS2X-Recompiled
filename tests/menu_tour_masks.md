# Menu-tour screenshot masks (frozen)

Reference and masks live in `private\reference\menu_tour` (game imagery, never committed). Frozen on 2026-10-17 under
the maintainer's mask rule (THPS 2X only, CLAUDE.md "Resources"): at most 25 % masked per screenshot,
no re-learning or widening without the maintainer's OK (`tools\shot_compare.ps1` refuses without `-MaintainerOk`), changes
reported as masked % before/after. Images over the cap are listed in `manual.txt` and checked by eye.

| Image | Masked | Compare | SHA-256 of the mask (first 16) |
|---|---|---|---|
| 01_main_menu | 13,5 % | automatic | A75A80EE957CAC9C |
| 02_career | 17,5 % | automatic | 5DAC59454B0BE41C |
| 03_career_cursor | 22,5 % | automatic | 021FA834C81EAC25 |
| 04_main_single_session | 9,8 % | automatic | 86B9E56B90639040 |
| 05_select_player | 24,4 % | automatic | C93F0DC16DF26963 |
| 06_next_skater | 23,4 % | automatic | 5D557066401A2423 |
| 07_select_level | 43,4 % | manual visual check | 2BBF5BE5454C34A7 |
| 08_multiplayer | 22,3 % | automatic | C75767A0D961A1F3 |
| 09_multiplayer_game | 17,8 % | automatic | 66B116689279E572 |
| 10_create_skater | 17,6 % | automatic | 5579AD4D3EEF1D78 |
| 11_options | 17,3 % | automatic | 7FA4BA2A92F5CBCF |
| 12_options_cursor | 28,2 % | manual visual check | 8E4454A5095D79F7 |
| 13_main_menu_options | 9,1 % | automatic | 2B9FD9DFD0B553FA |

07 is over the cap from its fixed rectangles (level preview, globe: 28.3 %) plus the grid; 12 grew from 17.0 % to 28.2 % when
all masks were re-learned on 2026-10-17 (no backup of the old mask). Neither has a deterministic capture point: shots are
timed in milliseconds and the menus animate in real time, so a fixed frame number would still land on a different phase.
