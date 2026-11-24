
CC = gcc
CFLAGS = -Wall -g -std=c11 -I./common
LDFLAGS = -pthread

all: client_app

client_app: client/main.c client/cli.c client/monitor.c common/queue.c common/logger.c
	$(CC) $(CFLAGS) client/main.c client/cli.c client/monitor.c common/queue.c common/logger.c -o client_app $(LDFLAGS)

clean:
	rm -f client_app server_app

