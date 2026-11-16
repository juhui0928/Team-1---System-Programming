CC = gcc
CFLAGS = -Wall -g -I./common
LDFLAGS = -lpthread

all: client_app server_app

client_app: client/main.c
	$(CC) $(CFLAGS) client/main.c -o client_app $(LDFLAGS)

server_app: server/server.c
	$(CC) $(CFLAGS) server/server.c -o server_app $(LDFLAGS)

clean:
	rm -f client_app server_app *.o