CC = gcc
CFLAGS = -Wall -g -std=c11 -I./common
LDFLAGS = -pthread

CLIENT_SRC = client/main.c \
             client/cli.c \
             client/monitor.c \
             client/integrity.c \
             client/encryption.c \
             client/recovery.c \
             client/remote.c client/backup.c \
             common/queue.c \
             common/logger.c

SERVER_SRC = server/server.c

.PHONY: all clean

all: client_app server_app

client_app: $(CLIENT_SRC)
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o client_app $(LDFLAGS) -lcrypto

server_app: $(SERVER_SRC)
	$(CC) $(CFLAGS) $(SERVER_SRC) -o server_app $(LDFLAGS)

clean:
	rm -f client_app server_app
