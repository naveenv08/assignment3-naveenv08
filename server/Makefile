CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -DUSE_AESD_CHAR_DEVICE
LDFLAGS += -pthread

all: aesdsocket

default: all

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) aesdsocket.c -o aesdsocket $(LDFLAGS)

clean:
	rm -f aesdsocket
