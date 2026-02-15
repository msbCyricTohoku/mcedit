CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99
SRC = mcedit.c
OBJ = $(SRC:.c=.o)
EXEC = mcedit
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(OBJ) -o $(EXEC)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(EXEC)
	install -d $(BINDIR)
	install -m 755 $(EXEC) $(BINDIR)

clean:
	rm -f $(OBJ) $(EXEC)

remove:
	rm -f $(BINDIR)/$(EXEC)

.PHONY: all install clean remove
