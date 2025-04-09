CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -ldl -Wl,--export-dynamic

all: test-linking

test-linking: test-linking.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f test-linking

.PHONY: all clean
