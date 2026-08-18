# $OpenBSD$
# Drop-in OpenBSD port for hush-relay.
# Copy this directory to /usr/ports/net/hush-relay and run:
#   make makesum
#   make package
# Keep GH_TAGNAME / PKGNAME in sync with the top-level VERSION file.

COMMENT =	Lightweight, legible C11 Nostr relay core

GH_ACCOUNT =	coldcanuk
GH_PROJECT =	hush
GH_TAGNAME =	0.0.1
PKGNAME =	hush-relay-${GH_TAGNAME}

CATEGORIES =	net

HOMEPAGE =	https://github.com/coldcanuk/hush
MAINTAINER =	Hush Contributors <coldcanuk@users.noreply.github.com>

# GPLv3+
PERMIT_PACKAGE =	Yes

WANTLIB =		c

USE_GMAKE =		Yes

CONFIGURE_STYLE =	simple
CONFIGURE_SCRIPT =	configure
CONFIGURE_ARGS =	--prefix=${PREFIX}

TEST_TARGET =		test

.include <bsd.port.mk>
