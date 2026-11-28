/*
 * Commands -> the emulated N64 pad. The bridge the port-driven styles use until
 * the game is driven directly.
 */
#ifndef PORT_INPUT_N64BRIDGE_H
#define PORT_INPUT_N64BRIDGE_H
#include "input_types.h"

/* N64 button bits (from PR/os.h -- duplicated here to avoid pulling os.h,
 * whose `u8 errno;` field collides with <errno.h>'s macro). */
#define GE_CONT_A      0x8000
#define GE_CONT_B      0x4000
#define GE_CONT_G      0x2000  /* Z trigger */
#define GE_CONT_START  0x1000
#define GE_CONT_UP     0x0800
#define GE_CONT_DOWN   0x0400
#define GE_CONT_LEFT   0x0200
#define GE_CONT_RIGHT  0x0100
#define GE_CONT_L      0x0020
#define GE_CONT_R      0x0010
#define GE_CONT_E      0x0008  /* C-up    */
#define GE_CONT_D      0x0004  /* C-down  */
#define GE_CONT_C      0x0002  /* C-left  */
#define GE_CONT_F      0x0001  /* C-right */

typedef struct N64BridgeOpts {
    int slotNative;   /* the slot's use / reload go out as the native B (no dedicated game call) */
    int southpaw;     /* swap the fire (Z) and aim (R) trigger bits */
} N64BridgeOpts;

/* The pad's contribution to the button mask, ORed into `button` (which already
 * holds what the keyboard and the earlier code put there). Same order as the
 * inline block it replaces: C bits, triggers, southpaw swap, then the faces. */
unsigned n64BridgeButtons(unsigned button, const InputCommands *c, const N64BridgeOpts *o);

/* The aim assists (zoom, slow strafe). A separate step: the crouch code runs
 * between the two and reads the C-down bit, so they must come after it. */
unsigned n64BridgeAssist(unsigned button, const InputCommands *c);

#endif
