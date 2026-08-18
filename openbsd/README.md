# OpenBSD packaging for hush-relay

Hush ships a drop-in OpenBSD port and a helper that stages a destroot.
Binary packages are `.tgz` files consumed by `pkg_add(1)`.

Official user-facing reference: <https://www.openbsd.org/faq/faq15.html>

## Install a local package

```sh
doas pkg_add ./hush-relay-0.0.1.tgz
```

Other everyday commands (FAQ 15):

```sh
pkg_info -aQ hush          # search
pkg_info hush-relay        # installed info
doas pkg_add -u            # update all packages
doas pkg_delete hush-relay # remove
doas pkg_delete -a         # remove leftover dependencies
```

`PKG_PATH` or `/etc/installurl` selects the mirror when installing from
the official package collection (not yet submitted).

## Build with the in-tree helper

From a Hush checkout (OpenBSD, after `pkg_add gmake`):

```sh
./configure --prefix=/usr/local
make openbsd
doas pkg_add ./dist/openbsd/hush-relay-*.tgz
```

On a non-OpenBSD host the same target **stages** `dist/openbsd/root` and
packing metadata but will **not** invent a fake `.tgz` (wrong ABI).

## Drop the port into `/usr/ports`

```sh
doas mkdir -p /usr/ports/net
doas cp -R openbsd/net/hush-relay /usr/ports/net/hush-relay
cd /usr/ports/net/hush-relay
make makesum
make package
# package lands under /usr/ports/packages/<arch>/all/
doas pkg_add hush-relay
```

The port uses `USE_GMAKE`, `CONFIGURE_STYLE=simple`, `WANTLIB=c`, and
`GH_ACCOUNT`/`GH_PROJECT`/`GH_TAGNAME`. Keep `GH_TAGNAME` aligned with
the top-level `VERSION` file.

## Layout

```
openbsd/net/hush-relay/Makefile   # bsd.port.mk port
openbsd/net/hush-relay/pkg/DESCR  # long description
openbsd/net/hush-relay/pkg/PLIST  # packing list (@bin + data files)
scripts/package-openbsd.sh        # destroot + pkg_create(1) on OpenBSD
```
