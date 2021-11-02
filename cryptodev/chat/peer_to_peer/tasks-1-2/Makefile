dependencies = chat-app-commons.h
CC = gcc
CFLAGS = -D CRYPT_ENABLE

all: crypto simple

crypto: crypto-server crypto-client
simple: simple-server simple-client

simple-server: chat-server.c $(dependencies)
	$(CC) -o simple-server.exe chat-server.c

simple-client: chat-client.c $(dependencies)
	$(CC) -o simple-client.exe chat-client.c

crypto-server: chat-server.c $(dependencies)
	$(CC) $(CFLAGS) -o crypto-server.exe chat-server.c

crypto-client: chat-client.c $(dependencies)
	$(CC) $(CFLAGS) -o crypto-client.exe chat-client.c

clean:
	rm -f *.exe