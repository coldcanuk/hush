# Shared by shell harnesses. Pair the way the app window does: redeem the
# one-time ?pair= link that a --no-open relay prints, into a cookie jar and
# an X-Hush-Nonce file. Owner-stamped writes (notes, signals, presence),
# approval answers, and approval_mode need that header (#336); session.token
# alone is 403.
# Usage: hush_pair port log jar
# Writes the nonce to ${HUSH_HOME:-.}/ui.nonce for curl() wrappers.
hush_pair() {
    _hp_i=0
    while :; do
        _hp_code=$(sed -n 's/.*?pair=\([0-9a-f]\{32\}\).*/\1/p' "$2" 2>/dev/null | tail -n 1)
        [ -n "$_hp_code" ] && break
        _hp_i=$((_hp_i + 1))
        [ "$_hp_i" -lt 100 ] || return 1
        sleep 0.05
    done
    _hp_hdr=$(mktemp)
    command curl -sf -D "$_hp_hdr" -c "$3" "http://127.0.0.1:$1/?pair=${_hp_code}" >/dev/null || {
        rm -f "$_hp_hdr"; return 1
    }
    _hp_nonce=$(sed -n 's/^[Xx]-[Hh]ush-[Nn]once: *\([0-9a-f]\{32\}\).*/\1/p' "$_hp_hdr" | head -n 1)
    rm -f "$_hp_hdr"
    [ -n "$_hp_nonce" ] || return 1
    printf '%s\n' "$_hp_nonce" > "${HUSH_HOME:-.}/ui.nonce"
}
