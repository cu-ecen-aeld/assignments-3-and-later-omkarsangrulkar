CC ?= gcc
CFLAGS ?= -Wall -Werror -g
LDFLAGS ?=

all: default

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o aesdsocket aesdsocket.c

clean:
	rm -f aesdsocket aesdsocket.o

.PHONY: all default clean
