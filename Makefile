CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
STRIP ?= strip
GTK = $(shell pkg-config --cflags --libs gtk+-3.0)

gametimer: gametimer.c
	$(CC) $(CFLAGS) -o $@ $< $(GTK) -lm
	$(STRIP) $@

clean:
	rm -f gametimer

.PHONY: clean
