#ifndef COLOR_H
#define COLOR_H

#include "sytnx.h"

int mceditcolorset(int hl) {
  switch (hl) {
  case HL_STRING:
    return 35; // magenta
  case HL_NUMBER:
    return 91; // bright red
  case HL_COMMENT:
    return 96; // cyan
  case HL_KEYWORD1:
    return 33; // yellow
  case HL_KEYWORD2:
    return 92; // green
  case HL_KEYWORD3:
    return 94; // blue
  case HL_MATCH:
    return 34; // Blue for search matches
  default:
    return 37;
  }
}

#endif
