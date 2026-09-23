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
 - Optional coturn STUN/TURN + conference signaling
 - Strict build: -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow

Developed with the Codex AI agent. Designed for set-and-forget
self-hosting and embedding.

%pre
# Stop-before-upgrade belongs here: this %pre (from the NEW package) runs
# before the new binary lands ($1 == 2 on upgrade, $1 == 1 on fresh
# install), which mirrors Debian's old-prerm-upgrade ordering so deb and
# rpm stay aligned. Upgrade is graceful-only: SIGTERM, then a generous
# poll so agent jobs can reap. There is deliberately NO SIGKILL on
# upgrade — a survivor keeps serving the old binary until the operator
# restarts it. Erase handling lives in %preun below.
if [ "${1:-0}" = "2" ]; then
    if [ -x %{_datadir}/hush/hush-relay-stop ]; then
        %{_datadir}/hush/hush-relay-stop upgrade || true
    else
        # Fallback with identical semantics for upgrades from
        # helper-less packages: SIGTERM + poll, no SIGKILL on upgrade.
        grace="${HUSH_STOP_GRACE_S:-30}"
        case "$grace" in ''|*[!0-9]*) grace=30 ;; esac
        pids=$(ps -axo pid=,comm= 2>/dev/null | awk '$2 == "hush-relay" {print $1}') || true
        if [ -n "$pids" ]; then
            kill $pids 2>/dev/null || true
            waited=0
            while [ "$waited" -lt "$grace" ]; do
                rest=""
                for p in $pids; do
                    if kill -0 "$p" 2>/dev/null; then
                        if [ -z "$rest" ]; then rest="$p"; else rest="$rest $p"; fi
                    fi
                done
                [ -z "$rest" ] && break
                pids=$rest
                sleep 1
                waited=$((waited + 1))
            done
        fi
    fi
fi
exit 0

%preun
# $1 == 1 upgrade (old package on the way out after the new one is in),
# $1 == 0 erase. Upgrade stays graceful-only like %pre; SIGKILL after the
# grace is reserved for erase, so a remove never leaves a stale relay
# behind. Canonical logic is scripts/hush-relay-stop (shipped as
# %{_datadir}/hush/hush-relay-stop); the inline branch below is the same
# fallback as in %pre for helper-less systems.
mode=upgrade
if [ "${1:-0}" = "0" ]; then mode=remove; fi
if [ -x %{_datadir}/hush/hush-relay-stop ]; then
    %{_datadir}/hush/hush-relay-stop "$mode" || true
else
    grace="${HUSH_STOP_GRACE_S:-30}"
    case "$grace" in ''|*[!0-9]*) grace=30 ;; esac
    pids=$(ps -axo pid=,comm= 2>/dev/null | awk '$2 == "hush-relay" {print $1}') || true
    if [ -n "$pids" ]; then
        kill $pids 2>/dev/null || true
        waited=0
        while [ "$waited" -lt "$grace" ]; do
            rest=""
            for p in $pids; do
                if kill -0 "$p" 2>/dev/null; then
                    if [ -z "$rest" ]; then rest="$p"; else rest="$rest $p"; fi
                fi
            done
            [ -z "$rest" ] && break
            pids=$rest
            sleep 1
            waited=$((waited + 1))
        done
        if [ -n "$pids" ] && { [ "$mode" = "remove" ] || [ "${HUSH_RELAY_ALLOW_KILL:-}" = "1" ]; }; then
            kill -KILL $pids 2>/dev/null || true # remove-gated last resort only
        fi
    fi
fi
exit 0

%prep
%autosetup

%build
./configure --prefix=/usr
%make_build

%install
%make_install PREFIX=/usr DESTDIR=%{buildroot}
install -D -m 0755 scripts/hush-relay-stop %{buildroot}%{_datadir}/hush/hush-relay-stop

%files
/usr/bin/hush-relay
%{_datadir}/applications/hush-relay.desktop
%{_datadir}/hush/hush-relay-stop
%{_datadir}/hush/turnserver.conf.in
%{_datadir}/hush/systemd/hush-turn.service
/lib/systemd/system/hush-turn.service

%changelog
* Sun Aug 17 2026 Hush Contributors <coldcanuk@users.noreply.github.com> - 0.0.1-1
- Initial RPM package for Hush relay
