#ifndef PORT_INPUT_STYLE_ORIGINAL_H
#define PORT_INPUT_STYLE_ORIGINAL_H
#include "input_types.h"

/* The accurate N64 stick response (Input.N64StickFeel = 1): linear 32767 ->
 * INPUT_STICK_MAX, a 2500 centre deadzone, nothing else. */
int inputN64StickAxis(int v);

/* The N64 button bit (GE_CONT_*) of each INNB_* button. */
unsigned inputN64ButtonBit(int innb);

extern const InputStrategy g_inputStyleOriginal;
#endif
