// Developed by msb and inspired by kilo editor
// new features: search, line numbers, goToline

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

/*** filetypes ***/

/*
struct mceditSyntax HLDB[] = {
    {"i", C_HL_extensions, C_HL_keywords, "#", "[",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};
*/
struct mceditSyntax HLDB[] = {
    {"C/C++", C_HL_extensions, C_HL_keywords,
     "//", // Single line comment
     "/*", // You can add logic for this later if you expand syntax engine
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
    {"Python", PY_HL_extensions, PY_HL_keywords, "#",
     "'''", // Docstrings (treated as block comments often)
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
    {"Fortran", FORT_HL_extensions, FORT_HL_keywords, "!", NULL,
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
    {"PHITS", PHITS_HL_extensions, PHITS_HL_keywords, "#", "[",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/
void mceditstatbarmsgset(const char *fmt, ...);
void mceditupdatsyntx(erow *row);
int mceditRowCxToRx(erow *row, int cx);
void mceditScroll();

// terminal
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
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
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

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
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
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

int winsizeFunc(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
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
  int in_string2 = 0;

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
int mceditRowCxToRx(erow *row, int cx) {
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
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  mceditrowrefup(row);
  cVar.modfycnt++;
}

/*** mcedit operations ***/
void mceditInsertChar(int c) {
  if (cVar.cy == cVar.Nrow) {
    mceditinsrowfunc(cVar.Nrow, "", 0);
  }
  mceditRowInsertChar(&cVar.row[cVar.cy], cVar.cx, c);
  cVar.cx++;
}

void mceditRowDelChar(erow *row, int at) {
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
char *mceditRowsToString(int *buflen) {
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
  FILE *fp = fopen(filename, "r");
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
  int fd = open(cVar.filename, O_RDWR | O_CREAT, 0644);
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

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}
void abFree(struct abuf *ab) { free(ab->b); }

/*** drawing ***/
void mceditstatbardraw(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     cVar.filename ? cVar.filename : "[No Name]", cVar.Nrow,
                     cVar.modfycnt ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
                      cVar.syntax ? cVar.syntax->filetype : "no ft",
                      cVar.cy + 1, cVar.Nrow);

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
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void mceditstatbarsetup(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(cVar.statusmsg);
  if (msglen > cVar.screencols)
    msglen = cVar.screencols;
  if (msglen && time(NULL) - cVar.statusmsg_time < 5)
    abAppend(ab, cVar.statusmsg, msglen);
}

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

// Draw rows with Line Numbers
void mceditDrawRows(struct abuf *ab) {
  int y;
  // Calculate width for line numbers (e.g., 4 spaces for 999 lines)
  int linenum_width = 4; // Minimal fixed width

  for (y = 0; y < cVar.screenrows; y++) {
    int filerow = y + cVar.rowoff;

    // Draw line number gutter
    if (filerow < cVar.Nrow) {
      char lnum[16];
      int lnumlen =
          snprintf(lnum, sizeof(lnum), "%*d ", linenum_width - 1, filerow + 1);
      abAppend(ab, "\x1b[90m", 5); // Gray for line numbers
      abAppend(ab, lnum, lnumlen);
      abAppend(ab, "\x1b[39m", 5); // Default color
    } else {
      abAppend(ab, "    ", 4); // Empty gutter
    }

    if (filerow >= cVar.Nrow) {
      if (cVar.Nrow == 0 && y == cVar.screenrows / 3) {
        char mssg[100];
        int mssglen =
            snprintf(mssg, sizeof(mssg), "mcedit PHITS editor -- version %s",
                     mceditFORT_VER);
        if (mssglen > cVar.screencols - linenum_width)
          mssglen = cVar.screencols - linenum_width;
        int padding = (cVar.screencols - linenum_width - mssglen) / 2;
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
      // Adjust width for line number column
      if (len > cVar.screencols - linenum_width)
        len = cVar.screencols - linenum_width;

      char *c = &cVar.row[filerow].render[cVar.coloff];
      unsigned char *hl = &cVar.row[filerow].hl[cVar.coloff];
      int current_color = -1;
      int j;
      for (j = 0; j < len; j++) {
        if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5);
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
  mceditstatbarsetup(&ab);
  char buf[32];
  // Adjust cursor position for line numbers
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (cVar.cy - cVar.rowoff) + 1,
           (cVar.rx - cVar.coloff) + 1 + 4); // +4 for gutter
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6);
  abAppend(&ab, "\x1b[0q", 4);
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

/*** Input and Search Utilities ***/
char *mceditPrompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    mceditstatbarmsgset(prompt, buf);
    mceditscreenref();
    int c = mceditkeyintake();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0)
        buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      mceditstatbarmsgset("");
      if (callback)
        callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        mceditstatbarmsgset("");
        if (callback)
          callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
    if (callback)
      callback(buf, c);
  }
}

void mceditFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;
  static int saved_hl_line;
  static char *saved_hl = NULL;

  if (saved_hl) {
    memcpy(cVar.row[saved_hl_line].hl, saved_hl, cVar.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  if (last_match == -1)
    direction = 1;
  int current = last_match;
  int i;
  for (i = 0; i < cVar.Nrow; i++) {
    current += direction;
    if (current == -1)
      current = cVar.Nrow - 1;
    else if (current == cVar.Nrow)
      current = 0;

    erow *row = &cVar.row[current];
    char *match = strstr(row->render, query);
    if (match) {
      last_match = current;
      cVar.cy = current;
      cVar.cx = mceditRowCxToRx(row, match - row->render);
      cVar.rowoff = cVar.Nrow; // Scroll to force center

      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void mceditFind() {
  int saved_cx = cVar.cx;
  int saved_cy = cVar.cy;
  int saved_coloff = cVar.coloff;
  int saved_rowoff = cVar.rowoff;
  char *query = mceditPrompt("Search: %s (ESC to cancel, Arrows/Enter to next)",
                             mceditFindCallback);
  if (query) {
    free(query);
  } else {
    cVar.cx = saved_cx;
    cVar.cy = saved_cy;
    cVar.coloff = saved_coloff;
    cVar.rowoff = saved_rowoff;
  }
}

void mceditGoToLine() {
  char *numstr = mceditPrompt("Go to line: %s", NULL);
  if (numstr) {
    int line = atoi(numstr);
    if (line > 0 && line <= cVar.Nrow) {
      cVar.cy = line - 1;
      cVar.cx = 0;
    }
    free(numstr);
  }
}

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
    if (cVar.cy != 0)
      cVar.cy--;
    break;
  case ARROW_DOWN:
    if (cVar.cy < cVar.Nrow)
      cVar.cy++;
    break;
  }
  row = (cVar.cy >= cVar.Nrow) ? NULL : &cVar.row[cVar.cy];
  int rowlen = row ? row->size : 0;
  if (cVar.cx > rowlen)
    cVar.cx = rowlen;
}

void mceditkeypress() {
  static int QTcount = mceditFORT_QTcount;
  int c = mceditkeyintake();
  switch (c) {
  case '\r':
    mceditinsNline();
    break;
  case CTRL_KEY('q'):
    if (cVar.modfycnt && QTcount > 0) {
      mceditstatbarmsgset(
          "Unsaved changes. Press Ctrl-Q %d more times to quit.", QTcount);
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
  case CTRL_KEY('f'): // New Search Key
    mceditFind();
    break;
  case CTRL_KEY('g'): // New GoTo Key
    mceditGoToLine();
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

  if (winsizeFunc(&cVar.screenrows, &cVar.screencols) == -1)
    die("winsizeFunc");
  cVar.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enRM();
  initmcedit();
  if (argc >= 2) {
    mceditOpen(argv[1]);
  }
  mceditstatbarmsgset(
      "Help: Ctrl-S = Save | Ctrl-Q = Quit | Ctrl-F = Find | Ctrl-G = GoTo");
  while (1) {
    mceditscreenref();
    mceditkeypress();
  }
  return 0;
}
