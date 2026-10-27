/* input.h - THPS 2X keyboard layout and input wiring (see input.c). */
#ifndef THPS_INPUT_H
#define THPS_INPUT_H

/* Install the THPS 2X keyboard layout. The keyboard answers as player 1 while
 * RECOMP_KEYBOARD=1 (main.c sets it by default). */
void thps_input_init(void);

#endif
