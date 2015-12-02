CFLAGS += -std=c11 -Wall -Wextra -O3

server: server.c common.h

.PHONY: clean

clean:
	rm -f server
