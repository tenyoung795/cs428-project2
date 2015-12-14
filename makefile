CFLAGS += -std=gnu11 -Wall -Wextra

server: server.c common.h

.PHONY: clean

clean:
	rm -f server
