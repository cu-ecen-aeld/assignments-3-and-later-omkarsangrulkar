CROSS_COMPILE ?=
CC ?= $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -Werror -g
LDFLAGS ?=

all: default
default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f aesdsocket

.PHONY: all default clean
