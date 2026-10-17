# Shared by shell harnesses. Pair the way the app window does: redeem the
# one-time ?pair= link that a --no-open relay prints, into a cookie jar.
# Approval answers (Yes/No to a held turn, team offer, or loop) and
# approval_mode need the hush_ui it sets (#336); session.token alone is 403.
# Usage: hush_pair port log jar
hush_pair() {
    _hp_i=0
    while :; do
        _hp_code=$(sed -n 's/.*?pair=\([0-9a-f]\{32\}\).*/\1/p' "$2" 2>/dev/null | tail -n 1)
        [ -n "$_hp_code" ] && break
        _hp_i=$((_hp_i + 1))
        [ "$_hp_i" -lt 100 ] || return 1
        sleep 0.05
    done
    command curl -sf -c "$3" "http://127.0.0.1:$1/?pair=${_hp_code}" >/dev/null || return 1
    grep -q 'hush_ui' "$3"
}
