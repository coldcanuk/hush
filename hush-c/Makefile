# Makefile for Hush C (legible C11)
# Full workflow support: ./configure, make, make install, make uninstall
# Desktop: installs hush-relay.desktop into the application launcher directory.
# Icon: installs hush-relay.png into the hicolor theme (48/128/256).
# Uninstall removes every PREFIX artifact install owns (bins, desktop entry,
# icons, Hush-owned files under share/hush, staged systemd unit). It never
# touches $(HOME)/.hush/ runtime data or /etc/hush/turnserver.conf, and it is
# idempotent (safe on an already-virgin PREFIX).

CC := gcc
# -fstack-protector-strong and -fPIE are supported by gcc/clang on every
# supported platform; configure probes the linker-visible hardening flags.
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow -Iinclude -O2 \
	-fstack-protector-strong -fPIE
LDFLAGS := -lcrypto
# Parent ./configure writes HUSH_HAVE_X11 and -lX11 when Xlib is present.
-include ../config.mk
override LDFLAGS += -Wl,-z,noexecstack

# These can be overridden by top-level or configure
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
# Isolated-eval honesty (mirrors the top-level Makefile): a command-line
# PREFIX re-derives BINDIR/DATADIR unless they were also given on the
# command line, so a stale ../config.mk cannot scatter files outside PREFIX.
# Environment/command-line values (origin != file) are always honored. This
# block must stay above APPDIR/ICONDIR/HUSHSHARE so they follow DATADIR.
ifeq ($(origin PREFIX),command line)
ifeq ($(origin BINDIR),file)
BINDIR := $(PREFIX)/bin
endif
ifeq ($(origin DATADIR),file)
DATADIR := $(PREFIX)/share
endif
endif
APPDIR ?= $(DATADIR)/applications
ICONDIR ?= $(DATADIR)/icons/hicolor
ICON_SIZES := 48 128 256
ENABLE_STUN_TURN ?= 1
HUSHSHARE ?= $(DATADIR)/hush
override CFLAGS += -DHUSH_SKILL_PACK_SHARE=\"$(HUSHSHARE)/skills/system\"

CODEX_SKILL_SOURCE := $(abspath ../.agents/skills/write-legible-c)
CODEX_SKILL_SHARE := $(HUSHSHARE)/codex/skills/write-legible-c
override CFLAGS += -DHUSH_CODEX_SKILL_SOURCE=\"$(CODEX_SKILL_SOURCE)\"
override CFLAGS += -DHUSH_CODEX_SKILL_SHARE=\"$(CODEX_SKILL_SHARE)\"

# Build stamp baked by ./configure into ../config.mk (git describe + short
# HEAD). Defaults keep non-configure builds working; hush_build.h agrees.
HUSH_BUILD_VERSION ?= 0.0.1
HUSH_BUILD_SHA ?= unknown
override CFLAGS += -DHUSH_BUILD_VERSION=\"$(HUSH_BUILD_VERSION)\"
override CFLAGS += -DHUSH_BUILD_SHA=\"$(HUSH_BUILD_SHA)\"

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o)
# Track header dependencies so a struct/enum change rebuilds every consumer.
-include $(OBJS:.o=.d)
# The generated .d files declare object targets; without this the first object
# would become the default goal and a bare make would stop building the relay.
.DEFAULT_GOAL := all
ICON_PANELS := dogs cats sheep virus robots angevin
ICON_PANEL_OBJS := $(foreach s,$(ICON_PANELS),src/hush_icon_panel_$(s).o)
TESTS := $(wildcard tests/test_*.c)
TEST_BINS := $(TESTS:.c=)

.PHONY: all test clean demo install uninstall check-prefix check-stamp

# An empty PREFIX would resolve BINDIR to /bin and let install/uninstall
# scribble outside any prefix; an empty BINDIR would resolve install targets
# to /hush-relay (root). The top-level Makefile guards too; this
# covers direct `make -C hush-c install PREFIX=` / `BINDIR=` invocations.
# DATADIR is guarded for the same reason (empty DATADIR would drop desktop
# entries and icons at /applications and /icons).
check-prefix:
	@if [ -z "$(PREFIX)" ]; then \
		echo "error: empty PREFIX. Pass a real prefix, e.g. make install PREFIX=/tmp/hush-eval,"; \
		echo "or unset PREFIX to use the configured default (\$$HOME/.local)."; \
		exit 1; \
	fi; \
	if [ -z "$(BINDIR)" ]; then \
		echo "error: empty BINDIR. Pass a real bindir, e.g. make install BINDIR=/tmp/hush-eval/bin,"; \
		echo "or unset BINDIR to derive it from PREFIX."; \
		exit 1; \
	fi; \
	if [ -z "$(DATADIR)" ]; then \
		echo "error: empty DATADIR. Pass a real datadir, e.g. make install DATADIR=/tmp/hush-eval/share,"; \
		echo "or unset DATADIR to derive it from PREFIX."; \
		exit 1; \
	fi

# Stale-stamp tripwire (warn-only): ../config.mk bakes HUSH_BUILD_SHA at
# configure time, so committing after configure leaves the binary stamped
# with the old SHA. tests/check_build.sh still fails the suite on mismatch;
# this just warns early during make/install (always exits 0).
check-stamp:
	@baked="$(HUSH_BUILD_SHA)"; \
	head=""; \
	if command -v git >/dev/null 2>&1; then \
		head=$$(git rev-parse --short HEAD 2>/dev/null || true); \
	fi; \
	if [ -n "$$baked" ] && [ "$$baked" != "unknown" ] \
		&& [ -n "$$head" ] && [ "$$baked" != "$$head" ]; then \
		echo "warning: baked build SHA ($$baked) != HEAD short ($$head); re-run ./configure."; \
	fi

all: check-stamp libhush.a hush-relay

libhush.a: $(OBJS) $(ICON_PANEL_OBJS)
	ar rcs $@ $^

src/hush_icon_panel_%.o: demo/icons/icon_panel_%.png
	ld -r -b binary -o $@ $<

hush-relay: src/hush_relay_main.o libhush.a
	$(CC) -o $@ $^ $(LDFLAGS)

src/hush_ui_html.h: demo/index.html demo/sw.js demo/manifest.webmanifest \
		demo/icons/icon-192.png demo/icons/icon-512.png \
		demo/icons/apple-touch-icon.png demo/icons/agent-atlas.png \
		../scripts/embed-ui.sh
	tmp=$@.tmp.$$$$; sh ../scripts/embed-ui.sh demo > $$tmp && mv $$tmp $@

src/hush_http.o: src/hush_ui_html.h
src/http_static.o: src/hush_ui_html.h

src/%.o: src/%.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

tests/test_%: tests/test_%.c libhush.a
	$(CC) $(CFLAGS) $< libhush.a -o $@ $(LDFLAGS)

test: $(TEST_BINS) hush-relay
	@for t in $(TEST_BINS); do echo "RUN $$t"; ./$$t || exit 1; done
	@sh tests/check_pwa.sh
	@sh tests/check_build.sh
	@sh tests/check_launch.sh
	@sh tests/check_restart.sh
	@CC="$(CC)" python3 tests/check_win.py
	@python3 tests/check_browser_launch.py
	@sh tests/check_exit.sh
	@sh tests/check_pkg_upgrade.sh
	@sh tests/check_make_guard.sh
	@sh tests/check_prefix.sh
	@sh tests/check_provider.sh
	@sh tests/check_agent.sh
	@sh tests/check_codex.sh
	@sh tests/check_ollama.sh
	@sh tests/check_failover.sh
	@python3 tests/check_collaboration.py
	@sh tests/check_ws4.sh
	@sh tests/check_ws5m2.sh
	@python3 tests/check_authz.py
	@python3 tests/check_ws.py
	@python3 tests/check_limits.py
	@python3 tests/check_trust.py
	@sh tests/check_fixup.sh
	@sh tests/check_complete.sh
	@sh tests/check_turn.sh
	@echo "ALL TESTS PASSED"

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(ICON_PANEL_OBJS) libhush.a hush-relay \
		$(TEST_BINS) src/hush_relay_main.o src/hush_ui_html.h

demo:
	@echo "See hush-c/demo/ for the chat UI (also served by hush-relay on /)"

install: check-prefix check-stamp all
	install -d $(DESTDIR)$(CODEX_SKILL_SHARE)/references
	install -m 0644 $(CODEX_SKILL_SOURCE)/SKILL.md $(CODEX_SKILL_SOURCE)/LICENSE \
		$(CODEX_SKILL_SOURCE)/UPSTREAM.md $(DESTDIR)$(CODEX_SKILL_SHARE)/
	install -m 0644 $(CODEX_SKILL_SOURCE)/references/c-standard.md \
		$(DESTDIR)$(CODEX_SKILL_SHARE)/references/
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 hush-relay $(DESTDIR)$(BINDIR)/hush-relay
	install -m 0755 ../scripts/hush-pass $(DESTDIR)$(BINDIR)/hush-pass
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
	@echo "  $(DESTDIR)$(BINDIR)/hush-pass"
	@echo "  $(DESTDIR)$(APPDIR)/hush-relay.desktop"
	@echo "  $(DESTDIR)$(ICONDIR)/{48,128,256}x{48,128,256}/apps/hush-relay.png"
	@echo
	@echo "Update desktop database and icon cache (Linux):"
	@echo "  update-desktop-database $(DESTDIR)$(APPDIR) || true"
	@echo "  gtk-update-icon-cache -f $(DESTDIR)$(ICONDIR) || true"
	@echo
	@if [ -d ../skills/system ]; then \
		for d in ../skills/system/*; do \
			[ -f "$$d/SKILL.md" ] || continue; \
			slug=$$(basename "$$d"); \
			install -d "$(DESTDIR)$(HUSHSHARE)/skills/system/$$slug"; \
			install -m 0644 "$$d/SKILL.md" \
				"$(DESTDIR)$(HUSHSHARE)/skills/system/$$slug/SKILL.md"; \
		done; \
	fi
	@if [ -z "$(DESTDIR)" ] && [ -n "$(HOME)" ]; then \
		mkdir -p "$(HOME)/.hush/config" "$(HOME)/.hush/agents" \
			"$(HOME)/.hush/skills/system" "$(HOME)/.hush/skills/user" \
			"$(HOME)/.hush/skills/robots"; \
		if [ -d ../skills/system ]; then \
			for d in ../skills/system/*; do \
				[ -f "$$d/SKILL.md" ] || continue; \
				slug=$$(basename "$$d"); \
				mkdir -p "$(HOME)/.hush/skills/system/$$slug"; \
				if [ ! -f "$(HOME)/.hush/skills/system/$$slug/SKILL.md" ]; then \
					install -m 0644 "$$d/SKILL.md" \
						"$(HOME)/.hush/skills/system/$$slug/SKILL.md"; \
				fi; \
			done; \
		fi; \
		echo "  $(HOME)/.hush/{config,agents,skills}"; \
	fi
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
		install -d $(DESTDIR)/etc/hush; \
		if [ ! -f $(DESTDIR)/etc/hush/turnserver.conf ]; then \
			install -m 0640 ../contrib/turnserver.conf.in \
				$(DESTDIR)/etc/hush/turnserver.conf; \
		fi; \
		echo "Installed hush-turn.service (not enabled)."; \
		echo "  sudo systemctl enable --now hush-turn"; \
		echo "  or use Settings → Daemon mode"; \
	else \
		echo "STUN/TURN templates: $(DESTDIR)$(HUSHSHARE)/"; \
		echo "Daemon mode needs: sudo make install PREFIX=/usr"; \
	fi
endif

uninstall: check-prefix
	rm -f $(DESTDIR)$(BINDIR)/hush-relay $(DESTDIR)$(BINDIR)/hush-pass
	rm -f $(DESTDIR)$(APPDIR)/hush-relay.desktop
	@for size in $(ICON_SIZES); do \
		rm -f $(DESTDIR)$(ICONDIR)/$${size}x$${size}/apps/hush-relay.png; \
		rmdir $(DESTDIR)$(ICONDIR)/$${size}x$${size}/apps 2>/dev/null || true; \
		rmdir $(DESTDIR)$(ICONDIR)/$${size}x$${size} 2>/dev/null || true; \
	done
	rm -f $(DESTDIR)$(CODEX_SKILL_SHARE)/SKILL.md $(DESTDIR)$(CODEX_SKILL_SHARE)/LICENSE \
		$(DESTDIR)$(CODEX_SKILL_SHARE)/UPSTREAM.md \
		$(DESTDIR)$(CODEX_SKILL_SHARE)/references/c-standard.md
	rm -rf $(DESTDIR)$(HUSHSHARE)/skills/system
	rm -f $(DESTDIR)$(HUSHSHARE)/systemd/hush-turn.service \
		$(DESTDIR)$(HUSHSHARE)/turnserver.conf.in
	@if [ -n "$(DESTDIR)" ] || [ "$$(id -u)" = "0" ]; then \
		rm -f $(DESTDIR)/lib/systemd/system/hush-turn.service; \
	fi
	@for d in $(DESTDIR)$(HUSHSHARE)/skills \
		$(DESTDIR)$(CODEX_SKILL_SHARE)/references \
		$(DESTDIR)$(CODEX_SKILL_SHARE) \
		$(DESTDIR)$(HUSHSHARE)/codex/skills \
		$(DESTDIR)$(HUSHSHARE)/codex \
		$(DESTDIR)$(HUSHSHARE)/systemd \
		$(DESTDIR)$(HUSHSHARE); do \
		rmdir "$$d" 2>/dev/null || true; \
	done
	@echo "Removed hush-relay, hush-pass, desktop entry, launcher icons,"
	@echo "and Hush-owned files under $(DESTDIR)$(HUSHSHARE)."
	@echo "Kept: ~/.hush/ runtime data and /etc/hush/turnserver.conf."
