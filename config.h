typedef struct erow {
  int size;    //creating data types to store row of text
  int rsize;
  char *chars;
  char *render;
  unsigned char *hl;  //unsigned value 0 to 255
} erow;


struct mceditConfig {
  int cx, cy;    //cursor position
  int rx;
  int rowoff;   //vertical scrolling
  int coloff;  //horizontal scrolling
  int screenrows;
  int screencols;
  int Nrow;
  erow *row;
  int modfycnt;
  char *filename;  //display of filename in status bar
  char statusmsg[80];  //display message
  time_t statusmsg_time;
  struct mceditSyntax *syntax;
  struct termios orig_termios;
};
