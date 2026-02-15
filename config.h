#ifndef CONFIG_H
#define CONFIG_H

#include <termios.h>
#include <time.h>

typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
  unsigned char *hl;
} erow;

struct mceditConfig {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int Nrow;
  erow *row;
  int modfycnt;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct mceditSyntax *syntax;
  struct termios orig_termios;
};

#endif
