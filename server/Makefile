CC ?= gcc
CFLAGS = -Wall -Werror -g

all: default

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c

clean:
	rm -f aesdsocket aesdsocket.o

.PHONY: all default clean
