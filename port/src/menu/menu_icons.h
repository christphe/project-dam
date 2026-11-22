#pragma once
/* The face-button icons of a PlayStation-style pad, as alpha masks (tools_pc/gen_menuicons.py). The index is the
 * button's place in Xbox names: cross = A (bottom), circle = B (right), square = X (left), triangle = Y (top). */
enum { kMenuIconCross, kMenuIconCircle, kMenuIconSquare, kMenuIconTriangle, kMenuIconCount };
extern const int kMenuIconSize;
extern const unsigned char kMenuIcons[kMenuIconCount][64 * 64];   /* kMenuIconSize x kMenuIconSize, 255 = opaque */
