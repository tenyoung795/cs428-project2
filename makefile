CFLAGS += -std=gnu11 -Wall -Wextra

server: server.o
server.o: server.c common.h

.PHONY: clean

clean:
	rm -f server server.o
