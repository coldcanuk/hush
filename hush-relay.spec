Name:           hush-relay
Version:        0.0.1
Release:        1%{?dist}
Summary:        Lightweight, legible C11 Nostr relay core
License:        GPLv3+
URL:            https://github.com/coldcanuk/hush
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make

%description
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

%prep
%autosetup

%build
./configure --prefix=/usr
%make_build

%install
%make_install PREFIX=/usr DESTDIR=%{buildroot}

%files
/usr/bin/hush-relay
%{_datadir}/applications/hush-relay.desktop

%changelog
* Sun Aug 17 2026 Hush Contributors <coldcanuk@users.noreply.github.com> - 0.0.1-1
- Initial RPM package for Hush relay
