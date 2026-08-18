# FreeBSD packaging for hush-relay

Hush ships a drop-in FreeBSD port and a helper that stages a destroot.
Binary packages are `.pkg` files consumed by `pkg add` / `pkg install`.

User-facing reference: <https://www.freebsdsoftware.org/blog/freebsd-pkg-reference.html>

## Install a local package

```sh
pkg add ./hush-relay-0.0.1.pkg
```

Other everyday commands:

```sh
pkg search hush                        # search
pkg info hush-relay                    # installed info
pkg info -l hush-relay                 # files
pkg which /usr/local/bin/hush-relay    # owning package
pkg update && pkg upgrade              # catalog + upgrade
pkg delete hush-relay                  # remove
pkg autoremove                         # leftover dependencies
```

Official repo config lives in `/etc/pkg/FreeBSD.conf` (not yet submitted).
A custom/Poudriere repo can be added under `/usr/local/etc/pkg/repos/`.

## Build with the in-tree helper

From a Hush checkout (FreeBSD, after `pkg install -y gmake`):

```sh
./configure --prefix=/usr/local
gmake freebsd
pkg add ./dist/freebsd/hush-relay-*.pkg
```

On a non-FreeBSD host the same target **stages** `dist/freebsd/root` and
a `+MANIFEST` but will **not** invent a fake `.pkg` (wrong ABI).

## Drop the port into `/usr/ports`

```sh
mkdir -p /usr/ports/net
cp -R freebsd/net/hush-relay /usr/ports/net/hush-relay
cd /usr/ports/net/hush-relay
make makesum
make package
pkg add /usr/ports/packages/All/hush-relay-*.pkg
```

The port uses `USES=gmake`, `HAS_CONFIGURE=yes` (Hush's POSIX `configure`,
not GNU autoconf), `USE_GITHUB`, and `LICENSE=GPLv3+`. Keep `DISTVERSION`
aligned with the top-level `VERSION` file.

Poudriere users can add `net/hush-relay` to a pkglist and build as usual.

## Layout

```
freebsd/net/hush-relay/Makefile    # bsd.port.mk port
freebsd/net/hush-relay/pkg-descr   # long description + WWW
freebsd/net/hush-relay/pkg-plist   # file list (also PLIST_FILES)
scripts/package-freebsd.sh         # destroot + pkg create on FreeBSD
```
