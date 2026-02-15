#ifndef SYTNX_H
#define SYTNX_H

enum mceditfort_verHighlight {
  HL_NORMAL = 0,
  HL_STRING,
  HL_COMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_KEYWORD3,
  HL_NUMBER,
  HL_MATCH // for search highlighting
};

// data
struct mceditSyntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *singleline_comment_start2;
  int flags;
};

#endif
