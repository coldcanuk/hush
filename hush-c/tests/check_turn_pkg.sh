#!/bin/sh
# check_turn_pkg.sh: #353 packaging guard for hush-turn (coturn).
# A staged package install (make install DESTDIR=... PREFIX=/usr, what
# debian/rules and the RPM spec run) must ship no live TURN credential, a
# non-root fail-closed unit, and a first-enable helper that writes a fresh
# secret. Static checks plus the helper run against temp paths: no root, no
# systemd and no coturn are needed (none of them run here).
set -eu
cd "$(dirname "$0")/.."
fail() { echo "turn pkg check failed: $1" >&2; exit 1; }

T=$(mktemp -d "${TMPDIR:-/tmp}/hush-turn-pkg-XXXXXX")
trap 'rm -rf "$T"' EXIT
make -s install DESTDIR="$T/root" PREFIX=/usr >"$T/install.out" 2>&1 \
    || { cat "$T/install.out" >&2; fail "staged install failed"; }
R="$T/root"
unit="$R/lib/systemd/system/hush-turn.service"
tmpl="$R/usr/share/hush/turnserver.conf.in"
setup="$R/usr/share/hush/hush-turn-setup"

# --- no shipped credential ---
if grep -rl 'replace-me' "$R" >"$T/hits" 2>/dev/null; then
    fail "staged install ships a placeholder credential: $(tr '\n' ' ' <"$T/hits")"
fi
[ ! -e "$R/etc/hush/turnserver.conf" ] || fail "package must not install /etc/hush/turnserver.conf"
if grep -rIlE '^(user|static-auth-secret)=' "$R" >"$T/hits" 2>/dev/null; then
    fail "staged text file carries a credential line: $(tr '\n' ' ' <"$T/hits")"
fi
[ -f "$tmpl" ] || fail "template missing from /usr/share/hush"
[ -x "$setup" ] || fail "hush-turn-setup missing or not executable"

# --- unit: unprivileged, no capabilities, fails closed ---
[ -f "$unit" ] || fail "unit not staged"
cmp -s "$unit" "$R/usr/share/hush/systemd/hush-turn.service" || fail "unit copies differ"
has() { grep -qxF "$1" "$unit" || fail "unit lacks: $1"; }
has 'DynamicUser=yes'
has 'ConditionPathExists=/etc/hush/turnserver.conf'
has 'ExecStartPre=+/usr/share/hush/hush-turn-setup --check /etc/hush/turnserver.conf'
has 'LoadCredential=turnserver.conf:/etc/hush/turnserver.conf'
has 'CapabilityBoundingSet='
has 'NoNewPrivileges=true'
has 'ProtectSystem=strict'
grep -q '^ExecStart=/usr/bin/turnserver -c ${CREDENTIALS_DIRECTORY}/turnserver.conf ' "$unit" \
    || fail "coturn must read the credential copy, not /etc/hush directly"
! grep -Eq '^(User|Group)=(root|0)?$' "$unit" || fail "unit names root"
! grep -q '^AmbientCapabilities=' "$unit" || fail "unit grants ambient capabilities"

# --- messages never suggest enabling before a secret exists ---
no_bare_enable() { # $1 label, $2 file
    if grep 'systemctl enable --now hush-turn' "$2" | grep -vq 'hush-turn-setup'; then
        # a continuation line is fine only right after a setup line
        awk '/systemctl enable --now hush-turn/ && !/hush-turn-setup/ && prev !~ /hush-turn-setup/ {bad=1} {prev=$0} END {exit bad}' "$2" \
            || fail "$1 suggests enabling hush-turn before hush-turn-setup"
    fi
    grep -q 'hush-turn-setup' "$2" || fail "$1 does not name hush-turn-setup"
}
no_bare_enable "make install output" "$T/install.out"
./hush-relay --help >"$T/help.out" 2>&1 || true
no_bare_enable "hush-relay --help" "$T/help.out"
grep -q '^%{_datadir}/hush/hush-turn-setup$' ../hush-relay.spec || fail "spec %files lacks hush-turn-setup"
! grep -q '/etc/hush/turnserver.conf' ../hush-relay.spec || fail "spec must not package /etc/hush/turnserver.conf"

# --- helper: --check fails closed ---
H="$T/h"; mkdir "$H"
chk() { HUSH_TURN_TEMPLATE="$tmpl" "$setup" --check "$1" >/dev/null 2>&1; }
! chk "$H/missing.conf" || fail "--check passed a missing conf"
cp "$tmpl" "$H/tmpl.conf"
! chk "$H/tmpl.conf" || fail "--check passed the bare template"
{ cat "$tmpl"; echo 'user=hush:replace-me'; } >"$H/old.conf"
! chk "$H/old.conf" || fail "--check passed a placeholder credential"
{ cat "$tmpl"; echo 'user=hush:0123456789abcdef0123456789abcdef'; echo 'user=x:short'; } >"$H/mixed.conf"
! chk "$H/mixed.conf" || fail "--check passed a conf with one weak user= line"

# --- helper: first enable writes a fresh secret, 0600 ---
gen() { HUSH_TURN_CONF="$1" HUSH_TURN_TEMPLATE="$tmpl" "$setup" >/dev/null 2>&1; }
gen "$H/a/turnserver.conf" || fail "setup failed on a fresh path"
gen "$H/b/turnserver.conf" || fail "setup failed on a second fresh path"
for c in "$H/a/turnserver.conf" "$H/b/turnserver.conf"; do
    [ "$(stat -c %a "$c")" = 600 ] || fail "generated conf is not 0600 ($(stat -c %a "$c"))"
    [ "$(grep -c '^user=' "$c")" = 1 ] || fail "generated conf must have one user= line"
    grep -Eq '^user=hush:[0-9a-f]{32}$' "$c" || fail "generated secret is not 32 lowercase hex"
    chk "$c" || fail "--check rejects a generated conf"
done
sa=$(grep '^user=' "$H/a/turnserver.conf"); sb=$(grep '^user=' "$H/b/turnserver.conf")
[ "$sa" != "$sb" ] || fail "two first enables produced the same secret"
grep -q '^listening-port=3478$' "$H/a/turnserver.conf" || fail "generated conf lost the template body"

# --- helper: keeps a good conf, replaces a placeholder, refuses a custom one ---
before=$(cksum <"$H/a/turnserver.conf")
gen "$H/a/turnserver.conf" || fail "setup failed on a good conf"
[ "$(cksum <"$H/a/turnserver.conf")" = "$before" ] || fail "setup rewrote a conf that had a generated secret"
mkdir "$H/c"; cp "$H/old.conf" "$H/c/turnserver.conf"
gen "$H/c/turnserver.conf" || fail "setup failed on a placeholder conf"
chk "$H/c/turnserver.conf" || fail "setup left a placeholder credential in place"
mkdir "$H/d"; cp "$tmpl" "$H/d/turnserver.conf"; echo 'realm=custom' >>"$H/d/turnserver.conf"
before=$(cksum <"$H/d/turnserver.conf")
! gen "$H/d/turnserver.conf" || fail "setup overwrote a conf with no credential it knows"
[ "$(cksum <"$H/d/turnserver.conf")" = "$before" ] || fail "setup changed a hand-made conf"
if [ "$(id -u)" != 0 ]; then
    if HUSH_TURN_TEMPLATE="$tmpl" "$setup" >"$T/root.out" 2>&1; then
        fail "setup on /etc/hush must require root"
    fi
    grep -q 'run as root' "$T/root.out" || fail "setup on /etc/hush must refuse non-root up front"
fi
echo "turn pkg checks ok"
