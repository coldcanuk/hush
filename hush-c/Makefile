# Makefile for Hush C (legible C11)
# Full workflow support: ./configure, make, make install, make uninstall
# Desktop: installs hush-relay.desktop into the application launcher directory.
# Icon: installs hush-relay.png into the hicolor theme (48/128/256).

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Iinclude -O2
LDFLAGS := -lcrypto

# These can be overridden by top-level or configure
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
APPDIR ?= $(DATADIR)/applications
ICONDIR ?= $(DATADIR)/icons/hicolor
ICON_SIZES := 48 128 256
ENABLE_STUN_TURN ?= 1
HUSHSHARE ?= $(DATADIR)/hush

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

src/hush_ui_html.h: demo/index.html demo/sw.js demo/manifest.webmanifest \
		demo/icons/icon-192.png demo/icons/icon-512.png \
		demo/icons/apple-touch-icon.png ../scripts/embed-ui.sh
	sh ../scripts/embed-ui.sh demo > $@

src/hush_http.o: src/hush_ui_html.h

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_%: tests/test_%.c libhush.a
	$(CC) $(CFLAGS) $< libhush.a -o $@ $(LDFLAGS)

test: $(TEST_BINS) hush-relay
	@for t in $(TEST_BINS); do echo "RUN $$t"; ./$$t || exit 1; done
	@sh tests/check_pwa.sh
	@sh tests/check_launch.sh
	@sh tests/check_turn.sh
	@echo "ALL TESTS PASSED"

clean:
	rm -f $(OBJS) libhush.a hush-relay $(TEST_BINS) src/hush_relay_main.o src/hush_ui_html.h

demo:
	@echo "See hush-c/demo/ for the chat UI (also served by hush-relay on /)"

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 hush-relay $(DESTDIR)$(BINDIR)/hush-relay
	install -d $(DESTDIR)$(APPDIR)
	install -m 0644 ../hush-relay.desktop $(DESTDIR)$(APPDIR)/hush-relay.desktop
	@for size in $(ICON_SIZES); do \
		install -d $(DESTDIR)$(ICONDIR)/$${size}x$${size}/apps; \
		install -m 0644 ../assets/icons/$${size}x$${size}/hush-relay.png \
			$(DESTDIR)$(ICONDIR)/$${size}x$${size}/apps/hush-relay.png; \
	done
	@echo
	@echo "Installed:"
	@echo "  $(DESTDIR)$(BINDIR)/hush-relay"
	@echo "  $(DESTDIR)$(APPDIR)/hush-relay.desktop"
	@echo "  $(DESTDIR)$(ICONDIR)/{48,128,256}x{48,128,256}/apps/hush-relay.png"
	@echo
	@echo "Update desktop database and icon cache (Linux):"
	@echo "  update-desktop-database $(DESTDIR)$(APPDIR) || true"
	@echo "  gtk-update-icon-cache -f $(DESTDIR)$(ICONDIR) || true"
	@echo
	@echo "System-wide install: sudo make install PREFIX=/usr"
ifneq ($(ENABLE_STUN_TURN),0)
	install -d $(DESTDIR)$(HUSHSHARE)/systemd
	install -m 0644 ../contrib/systemd/hush-turn.service \
		$(DESTDIR)$(HUSHSHARE)/systemd/hush-turn.service
	install -m 0644 ../contrib/turnserver.conf.in \
		$(DESTDIR)$(HUSHSHARE)/turnserver.conf.in
	@if [ -n "$(DESTDIR)" ] || [ "$$(id -u)" = "0" ]; then \
		install -d $(DESTDIR)/lib/systemd/system; \
		install -m 0644 ../contrib/systemd/hush-turn.service \
			$(DESTDIR)/lib/systemd/system/hush-turn.service; \
		echo "Installed hush-turn.service (not enabled)."; \
		echo "  sudo systemctl enable --now hush-turn"; \
		echo "  or use Settings → Daemon mode"; \
	else \
		echo "STUN/TURN templates: $(DESTDIR)$(HUSHSHARE)/"; \
		echo "Daemon mode needs: sudo make install PREFIX=/usr"; \
	fi
endif

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/hush-relay
	rm -f $(DESTDIR)$(APPDIR)/hush-relay.desktop
	@for size in $(ICON_SIZES); do \
		rm -f $(DESTDIR)$(ICONDIR)/$${size}x$${size}/apps/hush-relay.png; \
	done
	@echo "Removed hush-relay, desktop entry, and launcher icons."
