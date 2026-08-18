# Top-level Makefile for Hush C (Linux + *BSD)
# Workflow:
#   ./configure [PREFIX=...]
#   make
#   make test
#   make install [PREFIX=...]     # installs hush-relay + .desktop + icons
#   make uninstall

include config.mk

.PHONY: all test clean install uninstall package-deb package-rpm packages

all:
	$(MAKE) -C hush-c all CC="$(CC)" CFLAGS="$(CFLAGS)"

test:
	$(MAKE) -C hush-c test CC="$(CC)" CFLAGS="$(CFLAGS)"

clean:
	$(MAKE) -C hush-c clean

install:
	$(MAKE) -C hush-c install \
		PREFIX="$(PREFIX)" \
		BINDIR="$(BINDIR)" \
		DATADIR="$(DATADIR)"
	@echo
	@echo "To refresh your application launcher (Linux):"
	@echo "  update-desktop-database $(DATADIR)/applications || true"
	@echo "  gtk-update-icon-cache -f $(DATADIR)/icons/hicolor || true"
	@echo
	@echo "hush-relay should now appear in your menu / launcher."

uninstall:
	$(MAKE) -C hush-c uninstall \
		PREFIX="$(PREFIX)" \
		BINDIR="$(BINDIR)" \
		DATADIR="$(DATADIR)"

package-deb package-rpm packages:
	@echo "See packaging targets in previous milestones (DEB/RPM ready)."
