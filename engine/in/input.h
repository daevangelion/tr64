/* SPDX-License-Identifier: MIT */
#ifndef TR64_INPUT_H
#define TR64_INPUT_H
#include "tr64/in.h"
/* libdragon joypad_buttons_t wire order (MIPS big endian), not game bits. */
enum {
 TR64_PAD_A=0x8000,TR64_PAD_B=0x4000,TR64_PAD_Z=0x2000,
 TR64_PAD_START=0x1000,TR64_PAD_UP=0x0800,TR64_PAD_DOWN=0x0400,
 TR64_PAD_LEFT=0x0200,TR64_PAD_RIGHT=0x0100,TR64_PAD_L=0x0020,
 TR64_PAD_R=0x0010,TR64_PAD_CUP=0x0008,TR64_PAD_CDOWN=0x0004,
 TR64_PAD_CLEFT=0x0002,TR64_PAD_CRIGHT=0x0001
};
/* Quantize the clamped stick to the fixed game's digital movement actions. */
uint16_t tr64_input_game(tr64_input_t input);
enum {
 TR64_DEMO_NONE,TR64_DEMO_RESTART,TR64_DEMO_PREVIOUS,TR64_DEMO_NEXT,
 TR64_DEMO_FIRST,TR64_DEMO_GYM
};
/* L is a modifier; a command fires only on the other button's rising edge. */
unsigned tr64_input_demo(tr64_input_t input,uint16_t previous_buttons);
enum { TR64_PERF_CHORD_NONE,TR64_PERF_CHORD_OVERLAY,TR64_PERF_CHORD_PREVIEW };
/* Only an exact L+D-pad chord and a new D-pad edge. Extra buttons or a
 * digital stick action make it ambiguous, including demo and gate chords. */
unsigned tr64_input_perf(tr64_input_t input,uint16_t previous_buttons);
enum { TR64_COOP_NONE,TR64_COOP_JOIN,TR64_COOP_LEAVE,TR64_COOP_WARP };
/* Demo/gate chords own pad 1 first. Pad 2's L+START beats warp; only same-poll START edges collide. */
unsigned tr64_input_coop(tr64_input_t input,uint16_t previous,unsigned slot,
                         int paired,uint16_t primary_pressed);
#endif
