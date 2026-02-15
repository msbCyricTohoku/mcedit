// Debeloped by msb and inspired by kilo editor
// incluces
// #define _DEFAULT_SOURCE
// #define _BSD_SOURCE
// #define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "color.h"
#include "config.h"
#include "def.h"
#include "keys.h"
#include "param.h"
#include "sytnx.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

struct mceditConfig cVar;

/*** filetypes ***/ //, ".c", ".h", ".cpp"
char *C_HL_extensions[] = {".i", ".inp",
                           NULL}; // these are the filetypes for now
// char *C_HL_comments[] = {"$","c", NULL };

struct mceditSyntax HLDB[] = {
    {
        "i", // for f90 files, this detects the file extension
        C_HL_extensions, C_HL_keywords,
        "#", // this detects the comment in fortran90
        "[",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS //    HL_HIGHLIGHT_NUMBERS,
    },
};
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/
void mceditstatbarmsgset(const char *fmt, ...);

// terminal
void die(const char *s) {
  // these here will clear the screen on error
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s); // from stdio prints the errno
  exit(1);   // from stdlib
}

void disRM() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &cVar.orig_termios) == -1)
    die("tcsetattr");
}

void enRM() {

  if (tcgetattr(STDIN_FILENO, &cVar.orig_termios) == -1)
    die("tcgetattr");
  atexit(disRM);

  struct termios raw = cVar.orig_termios;

  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

  raw.c_oflag &= ~(OPOST);

  raw.c_cflag |= (CS8);

  // playing with bits
  raw.c_lflag &=
      ~(ECHO | ICANON | IEXTEN | ISIG); // turn off canonical mode GNU ISIG
                                        // disable ctrl-c or ctrl-z and etc...
  // ISIG   When any of the characters INTR, QUIT, SUSP, or DSUSP are received,
  // generate the corresponding signal

  // here we set the timeout for read
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr"); // set attributes
}

// simply reads the key press
int mceditkeyintake() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY; //<esc>[1~  these are VT100 codes for Home  key
          case '3':
            return DEL_KEY; //<esc>[3~  these are VT100 codes for Del   key
          case '4':
            return END_KEY; //<esc>[4~  these are VT100 codes for End   key
          case '5':
            return PAGE_UP; //<esc>[5~  these are VT100 codes for PageU key
          case '6':
            return PAGE_DOWN; //<esc>[6~  these are VT100 codes for PageD key
          case '7':
            return HOME_KEY; //<esc>[7~  these are VT100 codes for Home  key
          case '8':
            return END_KEY; //<esc>[8~  these are VT100 codes for End   key
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP; //<esc>[A  The arrow key codes
        case 'B':
          return ARROW_DOWN; //<esc>[B
        case 'C':
          return ARROW_RIGHT; //<esc>[C
        case 'D':
          return ARROW_LEFT; //<esc>[D
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

// gets the cursor position to estimate screen size
int getcurspos(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;
  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;
  return 0;
}

int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

// this gets the size of the terminal using the command TIOCGWINSZ
int winsizeFunc(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1; // we send two escape sequence here, C moves the mouse right
                 // forward and B moves the mouse down, the value 999 is rather
                 // large so to ensurewe reached edge of the screen
    return getcurspos(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** syntax highlighting ***/

void mceditupdatsyntx(erow *row) {
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);
  if (cVar.syntax == NULL)
    return;

  char **keywords = cVar.syntax->keywords;

  char *scs = cVar.syntax->singleline_comment_start;
  int scs_len = scs ? strlen(scs) : 0;
  int prev_sep = 1;
  int in_string = 0;
  int i = 0;

  char *scs2 = cVar.syntax->singleline_comment_start2;
  int scs_len2 = scs2 ? strlen(scs2) : 0;
  // int prev_sep = 1;
  int in_string2 = 0;
  // int i = 0;

  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;
    if (scs_len && !in_string) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }

    if (scs_len2 && !in_string2) {
      if (!strncmp(&row->render[i], scs2, scs_len2)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }

    if (cVar.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string)
          in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }
    if (cVar.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '/';
        if (kw2)
          klen--;
        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);

          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }

    if (prev_sep) {
      int j3;
      for (j3 = 0; keywords[j3]; j3++) {
        int klen3 = strlen(keywords[j3]);
        int kw3 = keywords[j3][klen3 - 1] == '}';

        if (kw3)
          klen3--;
        if (!strncmp(&row->render[i], keywords[j3], klen3) &&
            is_separator(row->render[i + klen3])) {
          memset(&row->hl[i], HL_KEYWORD3, klen3);

          i += klen3;
          break;
        }
        // memset(&row->hl[i], HL_KEYWORD3, klen);
      }
      if (keywords[j3] != NULL) {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator(c);
    i++;
  }
}

// this tries to match filename to one of te filematch in HLDB
void mceditsyntxcolorsel() {
  cVar.syntax = NULL;
  if (cVar.filename == NULL)
    return;
  char *ext = strrchr(cVar.filename, '.');
  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct mceditSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(cVar.filename, s->filematch[i]))) {
        cVar.syntax = s;

        int filerow;
        for (filerow = 0; filerow < cVar.Nrow; filerow++) {
          mceditupdatsyntx(&cVar.row[filerow]);
        }

        return;
      }
      i++;
    }
  }
}

/*** row operations ***/

int mceditRowCxToRx(erow *row, int cx) { // calc how many spaces tab key takes
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (mceditFORT_TAB_STOP - 1) - (rx % mceditFORT_TAB_STOP);
    rx++;
  }
  return rx;
}

void mceditrowrefup(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t')
      tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs * (mceditFORT_TAB_STOP - 1) + 1);
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % mceditFORT_TAB_STOP != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
  mceditupdatsyntx(row);
}

void mceditinsrowfunc(int at, char *s, size_t len) {
  if (at < 0 || at > cVar.Nrow)
    return;
  cVar.row = realloc(cVar.row, sizeof(erow) * (cVar.Nrow + 1));
  memmove(&cVar.row[at + 1], &cVar.row[at], sizeof(erow) * (cVar.Nrow - at));
  //  int at = cVar.Nrow;
  cVar.row[at].size = len;
  cVar.row[at].chars = malloc(len + 1);
  memcpy(cVar.row[at].chars, s, len);
  cVar.row[at].chars[len] = '\0';
  cVar.row[at].rsize = 0;
  cVar.row[at].render = NULL;

  cVar.row[at].hl = NULL;

  mceditrowrefup(&cVar.row[at]);
  cVar.Nrow++;
  cVar.modfycnt++;
}

void mceditinsNline() {
  if (cVar.cx == 0) {
    mceditinsrowfunc(cVar.cy, "", 0);
  } else {
    erow *row = &cVar.row[cVar.cy];
    mceditinsrowfunc(cVar.cy + 1, &row->chars[cVar.cx], row->size - cVar.cx);
    row = &cVar.row[cVar.cy];
    row->size = cVar.cx;
    row->chars[row->size] = '\0';
    mceditrowrefup(row);
  }
  cVar.cy++;
  cVar.cx = 0;
}

void mceditFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

void mceditDelRow(int at) {
  if (at < 0 || at >= cVar.Nrow)
    return;
  mceditFreeRow(&cVar.row[at]);
  memmove(&cVar.row[at], &cVar.row[at + 1],
          sizeof(erow) * (cVar.Nrow - at - 1));
  cVar.Nrow--;
  cVar.modfycnt++;
  // free(cVar.row->hl); //this make the program to crash after line backspace
}

void mceditRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  mceditrowrefup(row);
  cVar.modfycnt++;
}

void mceditRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at],
          row->size - at + 1); // make room for new char
  row->size++;
  row->chars[at] = c;
  mceditrowrefup(row);
  cVar.modfycnt++;
}
// here we insert the character(s)
/*** mcedit operations ***/
void mceditInsertChar(int c) {
  if (cVar.cy == cVar.Nrow) {
    mceditinsrowfunc(cVar.Nrow, "", 0);
  }
  mceditRowInsertChar(&cVar.row[cVar.cy], cVar.cx, c);
  cVar.cx++;
}

void mceditRowDelChar(erow *row, int at) { // backspace delete
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  mceditrowrefup(row);
  cVar.modfycnt++;
}

void mceditDelChar() {
  if (cVar.cy == cVar.Nrow)
    return;
  if (cVar.cx == 0 && cVar.cy == 0)
    return;
  erow *row = &cVar.row[cVar.cy];
  if (cVar.cx > 0) {
    mceditRowDelChar(row, cVar.cx - 1);
    cVar.cx--;
  } else {
    cVar.cx = cVar.row[cVar.cy - 1].size;
    mceditRowAppendString(&cVar.row[cVar.cy - 1], row->chars, row->size);
    mceditDelRow(cVar.cy);
    cVar.cy--;
  }
}

/*** file i/o ***/
char *mceditRowsToString(int *buflen) { // saving to disk
  int totlen = 0;
  int j;
  for (j = 0; j < cVar.Nrow; j++)
    totlen += cVar.row[j].size + 1;
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < cVar.Nrow; j++) {
    memcpy(p, cVar.row[j].chars, cVar.row[j].size);
    p += cVar.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void mceditOpen(char *filename) {
  free(cVar.filename);
  cVar.filename = strdup(filename);

  mceditsyntxcolorsel();

  FILE *fp = fopen(filename, "r"); // open a actual file
  if (!fp)
    die("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    mceditinsrowfunc(cVar.Nrow, line, linelen);
  }
  free(line);
  fclose(fp);
  cVar.modfycnt = 0;
}

void mceditSave() {
  if (cVar.filename == NULL)
    return;
  int len;
  char *buf = mceditRowsToString(&len);
  int fd = open(cVar.filename, O_RDWR | O_CREAT,
                0644); // O_CREAT open existing file, O_RDWR read and write,
                       // 0644 standard permission
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        cVar.modfycnt = 0;
        mceditstatbarmsgset("%d bytes saved to disk", len);
        return;
      }
      mceditsyntxcolorsel();
    }
    close(fd);
  }
  free(buf);
  mceditstatbarmsgset("cannot save: error: %s", strerror(errno));
}

/*** append buffer ***/
struct abuf {
  char *b;
  int len;
};
#define ABUF_INIT                                                              \
  { NULL, 0 }

// buffer
void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}
void abFree(struct abuf *ab) { free(ab->b); }

// all these from VT100
/*
void mceditscreenref() {   //note 4 as there 4 args \x1b, [, 2, J
  write(STDOUT_FILENO, "\x1b[2J", 4); //\x1b (decimal 27) escape character, J to
clear screen write(STDOUT_FILENO, "\x1b[H", 3); //cursor position on screen

  mceditDrawRows();

  write(STDOUT_FILENO, "\x1b[H", 3);
}                                    //[2J clear the entire screen [1J up to
cursor point
*/

void mceditstatbardraw(struct abuf *ab) { // the status bar, <esc>[7m escape seq
                                          // for inverted colors, <esc>[m normal
  abAppend(ab, "\x1b[3m", 4); // also try x1b[5m or x1b[7m
  abAppend(ab, "\x1b[7m", 4);
  abAppend(ab, "\x1b[1m", 4);
  //[ 1 m    bold    Set "bright" attribute
  //[ 2 m    dim    Set "dim" attribute
  //[ 3 m    smso    Set "standout" attribute
  //[ 4 m    set smul unset rmul :?:    Set "underscore" (underlined text)
  //attribute [ 5 m    blink    Set "blink" attribute [ 7 m    rev    Set
  //"reverse" attribute [ 8 m    invis    Set "hidden" attribute
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     cVar.filename ? cVar.filename : "[No Name]", cVar.Nrow,
                     cVar.modfycnt ? "(phits script was modified)" : "");

  int rlen = snprintf(
      rstatus, sizeof(rstatus), "%s | %d/%d", // show filetype in statusbar
      cVar.syntax ? cVar.syntax->filetype : "no ft", cVar.cy + 1, cVar.Nrow);

  //  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
  //     cVar.cy + 1, cVar.Nrow);
  if (len > cVar.screencols)
    len = cVar.screencols;
  abAppend(ab, status, len);
  while (len < cVar.screencols) {
    if (cVar.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3); // normal color mode
  abAppend(ab, "\r\n", 2);
}

void mceditstatbarsetup(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(cVar.statusmsg);
  if (msglen > cVar.screencols)
    msglen = cVar.screencols;
  if (msglen && time(NULL) - cVar.statusmsg_time <
                    5) // after 5 seconds, when keys are pressed, screen
                       // refreshes by keypress
    abAppend(ab, cVar.statusmsg, msglen);
}

// output
void mceditScroll() {
  cVar.rx = 0;
  if (cVar.cy < cVar.Nrow) {
    cVar.rx = mceditRowCxToRx(&cVar.row[cVar.cy], cVar.cx);
  }
  if (cVar.cy < cVar.rowoff) {
    cVar.rowoff = cVar.cy;
  }
  if (cVar.cy >= cVar.rowoff + cVar.screenrows) {
    cVar.rowoff = cVar.cy - cVar.screenrows + 1;
  }
  if (cVar.rx < cVar.coloff) {
    cVar.coloff = cVar.rx;
  }
  if (cVar.rx >= cVar.coloff + cVar.screencols) {
    cVar.coloff = cVar.rx - cVar.screencols + 1;
  }
}

void mceditDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < cVar.screenrows; y++) {
    int filerow = y + cVar.rowoff;
    if (filerow >= cVar.Nrow) {
      if (cVar.Nrow == 0 && y == cVar.screenrows / 3) {
        //  char welcome[80];
        char mssg[100];
        int mssglen =
            snprintf(mssg, sizeof(mssg),
                     "mcedit PHITS editor Developed by Mehrdad S.Beni, Hiroshi "
                     "Watabe & Peter K.N. Yu -- version %s",
                     mceditFORT_VER);
        if (mssglen > cVar.screencols)
          mssglen = cVar.screencols;
        int padding = (cVar.screencols - mssglen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--)
          abAppend(ab, " ", 1);
        abAppend(ab, mssg, mssglen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = cVar.row[filerow].rsize - cVar.coloff;
      if (len < 0)
        len = 0;
      if (len > cVar.screencols)
        len = cVar.screencols;
      char *c = &cVar.row[filerow].render[cVar.coloff];
      unsigned char *hl = &cVar.row[filerow].hl[cVar.coloff];
      int current_color = -1;
      int j;
      for (j = 0; j < len; j++) {
        if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(
                ab, "\x1b[39m",
                5); //<esc>[39m to make sure we’re using the default text color
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);
        } else {
          int color = mceditcolorset(hl[j]);
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);
    }
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void mceditscreenref() {
  mceditScroll();
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  mceditDrawRows(&ab);

  mceditstatbardraw(&ab);
  // mceditstatbardraw(&ab);

  mceditstatbarsetup(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (cVar.cy - cVar.rowoff) + 1,
           (cVar.rx - cVar.coloff) + 1);
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (cVar.cy - cVar.rowoff) + 1,
           cVar.cx + 1);
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6); // make cursor visible
  abAppend(&ab, "\x1b[0q", 4);   // cursor blink
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void mceditstatbarmsgset(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(cVar.statusmsg, sizeof(cVar.statusmsg), fmt, ap);
  va_end(ap);
  cVar.statusmsg_time = time(NULL);
}

// this allows move of cursor in the mcedit environement
void mceditcursmov(int key) {
  erow *row = (cVar.cy >= cVar.Nrow) ? NULL : &cVar.row[cVar.cy];
  switch (key) {
  case ARROW_LEFT:
    if (cVar.cx != 0) {
      cVar.cx--;
    } else if (cVar.cy > 0) {
      cVar.cy--;
      cVar.cx = cVar.row[cVar.cy].size;
    }
    break;
  case ARROW_RIGHT:
    if (row && cVar.cx < row->size) {
      cVar.cx++;
    } else if (row && cVar.cx == row->size) {
      cVar.cy++;
      cVar.cx = 0;
    }
    break;
  case ARROW_UP:
    if (cVar.cy != 0) {
      cVar.cy--;
    }
    break;
  case ARROW_DOWN:
    if (cVar.cy < cVar.Nrow) {
      cVar.cy++;
    }
    break;
  }
  row = (cVar.cy >= cVar.Nrow) ? NULL : &cVar.row[cVar.cy];
  int rowlen = row ? row->size : 0;
  if (cVar.cx > rowlen) {
    cVar.cx = rowlen;
  }
}

/*** input ***/
// key handler

void mceditkeypress() {
  static int QTcount = mceditFORT_QTcount;

  int c = mceditkeyintake();
  switch (c) {
  case '\r': // this is the enter key
    mceditinsNline();
    break;
  case CTRL_KEY('q'):
    if (cVar.modfycnt && QTcount > 0) {
      mceditstatbarmsgset("unsaved changes to your phits script. "
                          "Press ctrl-q %d more time to quit.",
                          QTcount);
      QTcount--;
      return;
    }
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;

  case CTRL_KEY('s'):
    mceditSave();
    break;

  case HOME_KEY:
    cVar.cx = 0;
    break;
  case END_KEY:
    if (cVar.cy < cVar.Nrow)
      cVar.cx = cVar.row[cVar.cy].size;
    break;
  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    if (c == DEL_KEY)
      mceditcursmov(ARROW_RIGHT);
    mceditDelChar();
    break;
  case PAGE_UP:
  case PAGE_DOWN: {
    if (c == PAGE_UP) {
      cVar.cy = cVar.rowoff;
    } else if (c == PAGE_DOWN) {
      cVar.cy = cVar.rowoff + cVar.screenrows - 1;
      if (cVar.cy > cVar.Nrow)
        cVar.cy = cVar.Nrow;
    }
    int times = cVar.screenrows;
    while (times--)
      mceditcursmov(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;
  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    mceditcursmov(c);
    break;
  case CTRL_KEY('l'):
  case '\x1b':
    break;
  default:
    mceditInsertChar(c);
    break;
  }
  QTcount = mceditFORT_QTcount;
}

// init
void initmcedit() {
  cVar.cx = 0;
  cVar.cy = 0;
  cVar.rx = 0;
  cVar.rowoff = 0;
  cVar.coloff = 0;
  cVar.Nrow = 0;
  cVar.row = NULL;
  cVar.modfycnt = 0;
  cVar.filename = NULL;
  cVar.statusmsg[0] = '\0';
  cVar.statusmsg_time = 0;
  cVar.syntax = NULL;

  // cVar.screenrows +=1;
  if (winsizeFunc(&cVar.screenrows, &cVar.screencols) == -1)
    die("winsizeFunc");
  cVar.screenrows -= 2; // only one row at the end
}

int main(int argc, char *argv[]) {
  enRM();
  initmcedit();
  if (argc >= 2) {
    mceditOpen(argv[1]);
  }

  // mceditstatbarmsgset("HELP: Ctrl-Q = quit");
  mceditstatbarmsgset("to save: ctrl+s , to quit: ctrl+q");
  while (1) {
    mceditscreenref(); // escape sequence
    mceditkeypress();
  }
  return 0;
}
