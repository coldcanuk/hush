# Top-level Makefile for Hush (Linux + *BSD via ./configure)
# Requires: ./configure first (produces config.mk)

include config.mk

STAGE_DEB := $(CURDIR)/packaging/deb
DIST_DIR  := $(CURDIR)/dist
VERSION   := 0.1.0

.PHONY: all test clean install package-deb package-rpm packages package-clean

all:
	$(MAKE) -C hush-c all CC="$(CC)" CFLAGS="$(CFLAGS)"

test:
	$(MAKE) -C hush-c test CC="$(CC)" CFLAGS="$(CFLAGS)"

clean:
	$(MAKE) -C hush-c clean
	rm -rf $(DIST_DIR) $(STAGE_DEB)/usr $(STAGE_DEB)/DEBIAN/tmp

install:
	$(MAKE) -C hush-c install PREFIX="$(PREFIX)" BINDIR="$(BINDIR)" DATADIR="$(DATADIR)"

package-clean:
	rm -rf $(DIST_DIR) $(STAGE_DEB)/usr $(STAGE_DEB)/DEBIAN/tmp packaging/rpm/BUILD*

# DEB packaging (functional when dpkg-deb present)
package-deb: all
	@command -v dpkg-deb >/dev/null 2>&1 || { echo "dpkg-deb not found; install dpkg-dev"; exit 1; }
	@mkdir -p $(DIST_DIR)
	@rm -rf $(STAGE_DEB)/usr $(STAGE_DEB)/DEBIAN/tmp
	@mkdir -p $(STAGE_DEB)/usr/bin $(STAGE_DEB)/usr/share/doc/hush-relay
	@install -m 0755 hush-c/hush-relay $(STAGE_DEB)/usr/bin/hush-relay
	@install -m 0644 README.md $(STAGE_DEB)/usr/share/doc/hush-relay/README.md 2>/dev/null || true
	@install -m 0644 LICENSE $(STAGE_DEB)/usr/share/doc/hush-relay/LICENSE 2>/dev/null || true
	@# control already present in packaging/deb/DEBIAN/control; refresh version if needed
	@sed -i 's/^Version: .*/Version: $(VERSION)/' $(STAGE_DEB)/DEBIAN/control 2>/dev/null || true
	@dpkg-deb --build $(STAGE_DEB) $(DIST_DIR)/hush-relay_$(VERSION)_amd64.deb
	@echo "DEB created: $(DIST_DIR)/hush-relay_$(VERSION)_amd64.deb"
	@dpkg-deb --info $(DIST_DIR)/hush-relay_$(VERSION)_amd64.deb | head -10
	@dpkg -c $(DIST_DIR)/hush-relay_$(VERSION)_amd64.deb | head -10

# RPM: prefer nfpm if present; else produce spec + instructions (rpmbuild may be absent)
package-rpm: all
	@mkdir -p $(DIST_DIR)
	@if command -v nfpm >/dev/null 2>&1; then \
		echo "Using nfpm for RPM"; \
		nfpm package --packager rpm --target $(DIST_DIR) --config packaging/nfpm.yaml; \
		ls -l $(DIST_DIR)/*.rpm 2>/dev/null || true; \
	else \
		echo "nfpm not found. Producing RPM spec and source layout for manual rpmbuild."; \
		mkdir -p packaging/rpm/SOURCES; \
		cp -f hush-c/hush-relay packaging/rpm/SOURCES/ 2>/dev/null || true; \
		cp -f README.md LICENSE packaging/rpm/SOURCES/ 2>/dev/null || true; \
		rpmbuild --define "_topdir $(CURDIR)/packaging/rpm" -bb packaging/rpm/hush-relay.spec 2>&1 | tail -5 || echo "rpmbuild not available or failed (expected in many envs). See packaging/rpm/hush-relay.spec"; \
		find packaging/rpm -name '*.rpm' -exec cp {} $(DIST_DIR)/ \; 2>/dev/null || true; \
		ls -l $(DIST_DIR)/*.rpm 2>/dev/null || echo "RPM not built (no rpmbuild/nfpm). Spec ready at packaging/rpm/hush-relay.spec"; \
	fi

packages: package-deb package-rpm
	@echo "All packages in $(DIST_DIR)/ (see GitHub Releases flow for attachment)"

