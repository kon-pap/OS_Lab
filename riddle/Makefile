CC = gcc

all: memwriter server

memwriter: ch12memwriter.c
	$(CC) -o ch12memwriter ch12memwriter.c

server: server.c
	$(CC) -o server server.c

clean:
	rm -f server ch12memwriter