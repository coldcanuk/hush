# Top-level Makefile for Hush
# After ./configure, this delegates to hush-c using config.mk

include config.mk

.PHONY: all test clean install package-deb package-rpm packages

all:
	$(MAKE) -C hush-c all CC="$(CC)" CFLAGS="$(CFLAGS)"

test:
	$(MAKE) -C hush-c test CC="$(CC)" CFLAGS="$(CFLAGS)"

clean:
	$(MAKE) -C hush-c clean

install:
	$(MAKE) -C hush-c install PREFIX="$(PREFIX)" BINDIR="$(BINDIR)" DATADIR="$(DATADIR)"

# Packaging placeholders (implemented in later milestones)
package-deb:
	@echo "DEB packaging target (M3.2). Run after build."
	@mkdir -p dist
	# Will be filled by M3.2 tasks (dpkg-deb or nfpm)

package-rpm:
	@echo "RPM packaging target (M3.2)."
	@mkdir -p dist

packages: package-deb package-rpm
	@echo "Packages staged in dist/ (see M3.2)"
