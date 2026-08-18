# Drop-in FreeBSD port for hush-relay.
# Copy this directory to /usr/ports/net/hush-relay and run:
#   make makesum
#   make package
# Keep DISTVERSION / GH_TAGNAME in sync with the top-level VERSION file.
# Created by: Hush Contributors <coldcanuk@users.noreply.github.com>

PORTNAME=	hush-relay
DISTVERSION=	0.0.1
CATEGORIES=	net

MAINTAINER=	coldcanuk@users.noreply.github.com
COMMENT=	Lightweight, legible C11 Nostr relay core
WWW=		https://github.com/coldcanuk/hush

LICENSE=	GPLv3+
LICENSE_FILE=	${WRKSRC}/LICENSE

USES=		gmake
USE_GITHUB=	yes
GH_ACCOUNT=	coldcanuk
GH_PROJECT=	hush
GH_TAGNAME=	${DISTVERSION}

HAS_CONFIGURE=	yes
CONFIGURE_ARGS=	--prefix=${PREFIX}

# Tiny payload — list files here instead of a generated pkg-plist.
PLIST_FILES=	bin/hush-relay \
		share/applications/hush-relay.desktop \
		share/icons/hicolor/48x48/apps/hush-relay.png \
		share/icons/hicolor/128x128/apps/hush-relay.png \
		share/icons/hicolor/256x256/apps/hush-relay.png

do-test:
	${MAKE_CMD} -C ${WRKSRC} test

.include <bsd.port.mk>
