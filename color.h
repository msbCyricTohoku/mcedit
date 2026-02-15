int mceditcolorset(int hl) {
  switch (hl) {
    case HL_STRING: return 35; //magenta
    case HL_NUMBER: return 91; //bright red
    case HL_COMMENT: return 96; //cyan //set to 36
    case HL_KEYWORD1: return 33;  //yellow
    case HL_KEYWORD2: return 92; //green
      case HL_KEYWORD3: return 94; //blue
    //case HL_MATCH: return 34;
    // 30 black
    // 31 red
    // 32 green
    // 33 yellow
    // 34 blue
    // 35 magenta
    // 36 cyan
    // 37 white
    // 90 Bright Black (Gray)
    // 91 Bright Red
    // 92 Bright Green
    // 93 Bright Yellow
    // 94 Bright Blue
    // 95 Bright Magenta
    // 96 Bright Cyan
    // 97 Bright White
    default: return 37;
  }
}
