#!/bin/sh
# package-freebsd.sh — stage hush-relay for FreeBSD pkg(8).
#
# Always:
#   configure --prefix=/usr/local, build, DESTDIR-install, write +MANIFEST
# On FreeBSD:
#   invoke pkg create → dist/freebsd/hush-relay-<ver>.pkg
# Elsewhere:
#   leave destroot + metadata; do not emit a fake .pkg (wrong ABI).
#
# Usage (from repo root, or via make freebsd):
#   ./scripts/package-freebsd.sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

if [ ! -f VERSION ] || [ ! -f configure ] || [ ! -f freebsd/net/hush-relay/pkg-descr ]; then
    echo "package-freebsd.sh: run from a Hush checkout" >&2
    exit 1
fi

VERSION=$(cat VERSION | tr -d ' \t\r\n')
NAME=hush-relay
PREFIX=/usr/local
STAGE="$ROOT/dist/freebsd"
DESTROOT="$STAGE/root"
META="$STAGE/meta"
PKGFILE="${NAME}-${VERSION}.pkg"
COMMENT="Lightweight, legible C11 Nostr relay core"
ORIGIN="net/hush-relay"

rm -rf "$STAGE"
mkdir -p "$DESTROOT" "$META"

echo "=== FreeBSD package stage (${NAME}-${VERSION}) ==="
echo "PREFIX=$PREFIX"
echo "DESTROOT=$DESTROOT"

./configure --prefix="$PREFIX"
make
make install \
    DESTDIR="$DESTROOT" \
    PREFIX="$PREFIX" \
    BINDIR="${PREFIX}/bin" \
    DATADIR="${PREFIX}/share"

BIN="${DESTROOT}${PREFIX}/bin/${NAME}"
if [ ! -x "$BIN" ]; then
    echo "package-freebsd.sh: missing $BIN" >&2
    exit 1
fi

cp freebsd/net/hush-relay/pkg-descr "$META/pkg-descr"
cp freebsd/net/hush-relay/pkg-plist "$META/pkg-plist"

# sha256 of a staged file (Linux / BSD / OpenSSL fallbacks).
file_sha256() {
    _f=$1
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$_f" | awk '{print $1}'
    elif command -v sha256 >/dev/null 2>&1; then
        sha256 -q "$_f"
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$_f" | awk '{print $1}'
    elif command -v openssl >/dev/null 2>&1; then
        openssl dgst -sha256 "$_f" | awk '{print $NF}'
    else
        echo "package-freebsd.sh: no sha256 tool" >&2
        exit 1
    fi
}

hash_bin=$(file_sha256 "$BIN")
hash_desktop=$(file_sha256 "${DESTROOT}${PREFIX}/share/applications/${NAME}.desktop")
hash_48=$(file_sha256 "${DESTROOT}${PREFIX}/share/icons/hicolor/48x48/apps/${NAME}.png")
hash_128=$(file_sha256 "${DESTROOT}${PREFIX}/share/icons/hicolor/128x128/apps/${NAME}.png")
hash_256=$(file_sha256 "${DESTROOT}${PREFIX}/share/icons/hicolor/256x256/apps/${NAME}.png")

# +MANIFEST in UCL (pkg-create(8) MANIFEST FILE DETAILS).
# Paths are as installed (absolute under PREFIX).
cat > "$META/+MANIFEST" << MANIFEST
name = "${NAME}"
version = "${VERSION}"
origin = "${ORIGIN}"
comment = "${COMMENT}"
maintainer = "Hush Contributors <coldcanuk@users.noreply.github.com>"
www = "https://github.com/coldcanuk/hush"
prefix = "${PREFIX}"
categories = [ "net" ]
licenselogic = "single"
licenses = [ "GPLv3+" ]
desc = <<EOD
Hush is a minimal, self-hosted Nostr relay written in strict C11.

Features:
 - NIP-01 basics for chat (kinds 0, 1, 5, 7, 9)
 - EVENT ingestion with bounded in-memory store
 - REQ with filter matching (kinds, authors, ids, since/until, #h)
 - CLOSE command
 - Simple TCP newline-delimited JSON protocol
 - poll(2) single-threaded server
 - Strict build: -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow

Optimized for the Goose AI agent. Designed for set-and-forget
self-hosting and embedding.
EOD
files = {
    "${PREFIX}/bin/${NAME}" = "${hash_bin}"
    "${PREFIX}/share/applications/${NAME}.desktop" = "${hash_desktop}"
    "${PREFIX}/share/icons/hicolor/48x48/apps/${NAME}.png" = "${hash_48}"
    "${PREFIX}/share/icons/hicolor/128x128/apps/${NAME}.png" = "${hash_128}"
    "${PREFIX}/share/icons/hicolor/256x256/apps/${NAME}.png" = "${hash_256}"
}
MANIFEST

{
    echo "Hush FreeBSD package staging"
    echo "name:     ${NAME}"
    echo "version:  ${VERSION}"
    echo "origin:   ${ORIGIN}"
    echo "prefix:   ${PREFIX}"
    echo "destroot: ${DESTROOT}"
    echo "manifest: ${META}/+MANIFEST"
} > "$STAGE/README"

echo
echo "Staged files:"
find "$DESTROOT" -type f | sort

UNAME=$(uname -s)
if [ "$UNAME" = "FreeBSD" ]; then
    if ! command -v pkg >/dev/null 2>&1; then
        echo "package-freebsd.sh: pkg(8) not found (pkg bootstrap?)" >&2
        exit 1
    fi
    pkg create \
        -M "$META/+MANIFEST" \
        -r "$DESTROOT" \
        -o "$STAGE"
    echo
    echo "FreeBSD package written under $STAGE"
    echo "Install: pkg add $STAGE/${PKGFILE}"
    echo "         (or the name pkg create emitted)"
    echo "Remove:  pkg delete ${NAME}"
    exit 0
fi

echo
echo "Not FreeBSD (${UNAME}): destroot + +MANIFEST only."
echo "  destroot: $DESTROOT"
echo "  manifest: $META/+MANIFEST"
echo
echo "On FreeBSD, either:"
echo "  1. ./configure --prefix=/usr/local && gmake freebsd && pkg add ./dist/freebsd/${PKGFILE}"
echo "  2. Copy freebsd/net/hush-relay to /usr/ports/net/hush-relay && make package"
exit 0
