##
# Makefile
# Crypto TCP/IP chat app using sockets
#
##

CC = gcc

CFLAGS = -Wall -pthread
CFLAGS += -g

BINS = server-side client-side

all: $(BINS)

server-side: server-side.c crypto-chat-common.h
	$(CC) $(CFLAGS) $< -o $@

client-side: client-side.c crypto-chat-common.h
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f *.o *~ $(BINS)