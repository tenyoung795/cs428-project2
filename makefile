CFLAGS += -std=gnu11 -Wall -Wextra

all: client server

client: client.o
client.o: client.c common.h

server: server.o
server.o: server.c common.h

.PHONY: all clean

clean:
	rm -f server server.o
