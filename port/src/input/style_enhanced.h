#ifndef PORT_INPUT_STYLE_ENHANCED_H
#define PORT_INPUT_STYLE_ENHANCED_H
#include "input_types.h"

/* The Enhanced presets (Input.PadPreset): the preset picks the stick roles; the
 * buttons are the bind table the adapter resolves (the preset's defaults, or the
 * seat's own strings for Custom). */
enum { INPRESET_JINX = 0, INPRESET_CHRISTMAS = 1, INPRESET_FROST = 2, INPRESET_CUSTOM = 3, INPRESET_ELEKTRA = 4 };

/* The stick roles of a preset. 1.2 Christmas and 1.3 Frost force HONEY, 1.4
 * Elektra forces SOLITARE; 1.1 Jinx and Custom follow Input.NaturalPitch.
 * 1.3 Frost and 1.4 Elektra swap the two sticks. */
int inputEnhancedSolitare(int preset, int naturalPitch);
int inputEnhancedSwap(int preset);

extern const InputStrategy g_inputStyleEnhanced;
#endif
