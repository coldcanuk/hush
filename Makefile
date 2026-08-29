# Top-level Makefile for Hush C (Linux + *BSD)
# Workflow:
#   ./configure [PREFIX=...]
#   make
#   make test
#   make install [PREFIX=...]     # installs hush-relay + .desktop + icons
#   make uninstall
#   make deb                      # Build DEB package (requires debhelper)
#   make rpm                      # Build RPM package (requires rpmbuild)
#   make flatpak                  # Build Flatpak package (requires flatpak-builder)
#   make openbsd                  # Stage OpenBSD destroot; pkg_create on OpenBSD
#   make freebsd                  # Stage FreeBSD destroot; pkg create on FreeBSD
#   make bsd                      # openbsd + freebsd
#   make dist                     # Create source tarball

include config.mk

.PHONY: all test clean install uninstall package-deb package-rpm packages deb rpm flatpak openbsd freebsd bsd dist

all:
	$(MAKE) -C hush-c all CC="$(CC)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)"

test:
	$(MAKE) -C hush-c test CC="$(CC)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)"

clean:
	@sh scripts/kill-relay.sh
	$(MAKE) -C hush-c clean
	rm -f *.tar.gz

install:
	$(MAKE) -C hush-c install \
		DESTDIR="$(DESTDIR)" \
		PREFIX="$(PREFIX)" \
		BINDIR="$(BINDIR)" \
		DATADIR="$(DATADIR)" \
		ENABLE_STUN_TURN="$(ENABLE_STUN_TURN)"
	@echo
	@echo "To refresh your application launcher (Linux):"
	@echo "  update-desktop-database $(DATADIR)/applications || true"
	@echo "  gtk-update-icon-cache -f $(DATADIR)/icons/hicolor || true"
	@echo
	@echo "hush-relay should now appear in your menu / launcher."

uninstall:
	$(MAKE) -C hush-c uninstall \
		DESTDIR="$(DESTDIR)" \
		PREFIX="$(PREFIX)" \
		BINDIR="$(BINDIR)" \
		DATADIR="$(DATADIR)"

# --- DEB packaging ---
deb:
	dpkg-buildpackage -us -uc -b
	@echo
	@echo "DEB package built. Check ../hush-relay_*.deb"

# --- RPM packaging ---
rpm: dist
	rpmbuild -bb hush-relay.spec
	@echo
	@echo "RPM package built. Check ~/rpmbuild/RPMS/*/hush-relay-*.rpm"

# --- Flatpak packaging ---
flatpak:
	flatpak-builder --force-clean build-dir io.github.coldcanuk.hush.yml
	@echo
	@echo "Flatpak built. Install with:"
	@echo "  flatpak-builder --force-clean --install-deps-from=flathub build-dir io.github.coldcanuk.hush.yml"

# --- OpenBSD packaging (pkg_add / pkg_create) ---
openbsd:
	sh scripts/package-openbsd.sh
	@echo
	@echo "OpenBSD stage complete. See dist/openbsd/ and openbsd/README.md"

# --- FreeBSD packaging (pkg / pkg create) ---
freebsd:
	sh scripts/package-freebsd.sh
	@echo
	@echo "FreeBSD stage complete. See dist/freebsd/ and freebsd/README.md"

# --- Both BSD families ---
bsd: openbsd freebsd

# --- Source tarball ---
dist:
	git archive --prefix=hush-$(shell cat VERSION)/ -o hush-$(shell cat VERSION).tar.gz HEAD
	@echo
	@echo "Source tarball created: hush-$(shell cat VERSION).tar.gz"

# Legacy aliases (keep for backward compat)
package-deb: deb
package-rpm: rpm
packages: deb rpm openbsd freebsd
