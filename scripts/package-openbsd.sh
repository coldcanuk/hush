#!/bin/sh
# package-openbsd.sh — stage hush-relay for OpenBSD pkg_add(1).
#
# Always:
#   configure --prefix=/usr/local, build, DESTDIR-install, write packing metadata
# On OpenBSD:
#   invoke pkg_create(1) → dist/openbsd/hush-relay-<ver>.tgz
# Elsewhere:
#   leave destroot + metadata; do not emit a fake .tgz (wrong ABI).
#
# Usage (from repo root, or via make openbsd):
#   ./scripts/package-openbsd.sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

if [ ! -f VERSION ] || [ ! -f configure ] || [ ! -f openbsd/net/hush-relay/pkg/DESCR ]; then
    echo "package-openbsd.sh: run from a Hush checkout" >&2
    exit 1
fi

VERSION=$(cat VERSION | tr -d ' \t\r\n')
NAME=hush-relay
PREFIX=/usr/local
STAGE="$ROOT/dist/openbsd"
DESTROOT="$STAGE/root"
META="$STAGE/meta"
PKGFILE="${NAME}-${VERSION}.tgz"
COMMENT="Lightweight, legible C11 Nostr relay core"
FULLPKGPATH="net/hush-relay"

rm -rf "$STAGE"
mkdir -p "$DESTROOT" "$META"

echo "=== OpenBSD package stage (${NAME}-${VERSION}) ==="
echo "PREFIX=$PREFIX"
echo "DESTROOT=$DESTROOT"

./configure --prefix="$PREFIX"
make
make install \
    DESTDIR="$DESTROOT" \
    PREFIX="$PREFIX" \
    BINDIR="${PREFIX}/bin" \
    DATADIR="${PREFIX}/share"

if [ ! -x "${DESTROOT}${PREFIX}/bin/${NAME}" ]; then
    echo "package-openbsd.sh: missing ${DESTROOT}${PREFIX}/bin/${NAME}" >&2
    exit 1
fi

cp openbsd/net/hush-relay/pkg/DESCR "$META/DESCR"
cp openbsd/net/hush-relay/pkg/PLIST "$META/port-PLIST"

# Packing list for pkg_create(1): names relative to -p PREFIX.
# @bin marks an OpenBSD ELF executable (man pkg_create.1).
{
    echo "@comment ${NAME} ${VERSION}"
    echo "@name ${NAME}-${VERSION}"
    echo "@cwd ${PREFIX}"
    echo "@bin bin/${NAME}"
    echo "share/applications/${NAME}.desktop"
    echo "share/icons/hicolor/48x48/apps/${NAME}.png"
    echo "share/icons/hicolor/128x128/apps/${NAME}.png"
    echo "share/icons/hicolor/256x256/apps/${NAME}.png"
} > "$META/PLIST"

{
    echo "Hush OpenBSD package staging"
    echo "name:     ${NAME}"
    echo "version:  ${VERSION}"
    echo "origin:   ${FULLPKGPATH}"
    echo "prefix:   ${PREFIX}"
    echo "destroot: ${DESTROOT}"
    echo "plist:    ${META}/PLIST"
    echo "descr:    ${META}/DESCR"
} > "$STAGE/README"

echo
echo "Staged files:"
find "$DESTROOT" -type f | sort

UNAME=$(uname -s)
if [ "$UNAME" = "OpenBSD" ]; then
    if ! command -v pkg_create >/dev/null 2>&1; then
        echo "package-openbsd.sh: pkg_create(1) not found" >&2
        exit 1
    fi
    pkg_create \
        -d "$META/DESCR" \
        -D "COMMENT=${COMMENT}" \
        -D "FULLPKGPATH=${FULLPKGPATH}" \
        -D "HOMEPAGE=https://github.com/coldcanuk/hush" \
        -D "MAINTAINER=Hush Contributors <coldcanuk@users.noreply.github.com>" \
        -f "$META/PLIST" \
        -p "$PREFIX" \
        -B "$DESTROOT" \
        "$STAGE/$PKGFILE"
    echo
    echo "OpenBSD package: $STAGE/$PKGFILE"
    echo "Install: doas pkg_add $STAGE/$PKGFILE"
    echo "Remove:  doas pkg_delete ${NAME}"
    exit 0
fi

echo
echo "Not OpenBSD (${UNAME}): destroot + packing metadata only."
echo "  destroot: $DESTROOT"
echo "  plist:    $META/PLIST"
echo "  descr:    $META/DESCR"
echo
echo "On OpenBSD, either:"
echo "  1. ./configure --prefix=/usr/local && make openbsd && doas pkg_add ./dist/openbsd/${PKGFILE}"
echo "  2. Copy openbsd/net/hush-relay to /usr/ports/net/hush-relay && make package"
exit 0
