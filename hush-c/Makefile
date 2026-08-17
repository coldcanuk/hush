# Makefile for Hush C (legible C11)
# Full workflow support: ./configure, make, make install, make uninstall
# Desktop: installs hush-relay.desktop into the application launcher directory.

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Iinclude -O2
LDFLAGS :=

# These can be overridden by top-level or configure
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
APPDIR ?= $(DATADIR)/applications

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o)
TESTS := $(wildcard tests/test_*.c)
TEST_BINS := $(TESTS:.c=)

.PHONY: all test clean demo install uninstall

all: libhush.a hush-relay

libhush.a: $(OBJS)
	ar rcs $@ $^

hush-relay: src/hush_relay_main.o libhush.a
	$(CC) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_%: tests/test_%.c libhush.a
	$(CC) $(CFLAGS) $< libhush.a -o $@ $(LDFLAGS)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "RUN $$t"; ./$$t || exit 1; done
	@echo "ALL TESTS PASSED"

clean:
	rm -f $(OBJS) libhush.a hush-relay $(TEST_BINS) src/hush_relay_main.o

demo:
	@echo "See hush-c/demo/ for Tailwind UI demo (static)"

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 hush-relay $(DESTDIR)$(BINDIR)/hush-relay
	install -d $(DESTDIR)$(APPDIR)
	install -m 0644 ../hush-relay.desktop $(DESTDIR)$(APPDIR)/hush-relay.desktop
	@echo
	@echo "Installed:"
	@echo "  $(DESTDIR)$(BINDIR)/hush-relay"
	@echo "  $(DESTDIR)$(APPDIR)/hush-relay.desktop"
	@echo
	@echo "Update desktop database (Linux):"
	@echo "  update-desktop-database $(DESTDIR)$(APPDIR) || true"
	@echo
	@echo "User-local example: make install PREFIX=\$$HOME/.local"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/hush-relay
	rm -f $(DESTDIR)$(APPDIR)/hush-relay.desktop
	@echo "Removed hush-relay and desktop entry."
