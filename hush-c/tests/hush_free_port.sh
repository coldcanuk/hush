# Shared by shell harnesses. Print N free IPv4 loopback ports, one per line.
# Bind-then-release, the same approach as the Python harnesses (bind port 0).
# A later listen can lose the race. Callers that need several ports at once
# pass the count so the sockets stay open together until every port is chosen.
# Usage: hush_free_port [count]
hush_free_port() {
    _hfp_n=${1:-1}
    python3 -c '
import socket, sys
n = int(sys.argv[1])
if n < 1:
    raise SystemExit(1)
held = []
try:
    for _ in range(n):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.bind(("127.0.0.1", 0))
        held.append(s)
    for s in held:
        print(s.getsockname()[1])
finally:
    for s in held:
        s.close()
' "$_hfp_n"
}
