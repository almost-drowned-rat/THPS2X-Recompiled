/* input.c - THPS 2X keyboard layout and input wiring (M3).
 *
 * The title's XInput calls go to the toolkit's XDK HLE layer (src/xdk, bound
 * here and in recomp_manual.c), which reads the toolkit input layer
 * (src/input: SDL3 host pads, keyboard, pad script from --input-script).
 * RECOMP_XINPUT_HLE=0 runs the title's own XPP USB driver on the emulated
 * OHCI controllers (src/usb) instead. This file decides THPS 2X's keyboard
 * layout (CLAUDE.md, Features -> Input -- documented there, keep it stable):
 *
 *   W A S D      left stick           arrow keys   D-pad
 *   Enter        Start                Backspace    Back
 *   I J K L      Y X A B              U O          Black White
 *   Z P          left / right trigger (key = fully pressed)
 *   right stick, L3/R3: unassigned for now
 *
 * Keys are USB HID usage IDs (= SDL scancodes): the physical position, so
 * the layout is the same on QWERTZ/AZERTY keyboards ("Z" is the key left of
 * X on a US board).
 */
#include <stdint.h>

#include "xinput_xbox.h"
#include "xdk_xinput.h"
#include "input.h"

/* HID usage IDs, keyboard page. */
enum {
    HID_A = 4, HID_D = 7, HID_I = 12, HID_J = 13, HID_K = 14, HID_L = 15,
    HID_O = 18, HID_P = 19, HID_S = 22, HID_U = 24, HID_W = 26, HID_Z = 29,
    HID_ENTER = 40, HID_BACKSPACE = 42,
    HID_RIGHT = 79, HID_LEFT = 80, HID_DOWN = 81, HID_UP = 82,
    HID_KP_ENTER = 88,
};

static const XBOX_KEY_BINDING s_thps_keymap[] = {
    { HID_W, XBOX_KEY_LY_POS }, { HID_S, XBOX_KEY_LY_NEG },
    { HID_A, XBOX_KEY_LX_NEG }, { HID_D, XBOX_KEY_LX_POS },
    { HID_UP, XBOX_KEY_DPAD_UP }, { HID_DOWN, XBOX_KEY_DPAD_DOWN },
    { HID_LEFT, XBOX_KEY_DPAD_LEFT }, { HID_RIGHT, XBOX_KEY_DPAD_RIGHT },
    { HID_ENTER, XBOX_KEY_START }, { HID_KP_ENTER, XBOX_KEY_START },
    { HID_BACKSPACE, XBOX_KEY_BACK },
    { HID_I, XBOX_KEY_Y }, { HID_J, XBOX_KEY_X },
    { HID_K, XBOX_KEY_A }, { HID_L, XBOX_KEY_B },
    { HID_U, XBOX_KEY_BLACK }, { HID_O, XBOX_KEY_WHITE },
    { HID_Z, XBOX_KEY_LTRIGGER }, { HID_P, XBOX_KEY_RTRIGGER },
};

void thps_input_init(void)
{
    xbox_InputSetKeymap(s_thps_keymap,
                        (int)(sizeof s_thps_keymap / sizeof s_thps_keymap[0]));
    /* XDEVICE_TYPE_GAMEPAD (XPP 3911): the first entry of the type table the
     * game passes to XInitDevices from 0x001B0830 and the type it opens. */
    xdk_xinput_bind(0x00216AA0u);
}
