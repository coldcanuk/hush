import ctypes
import json
import os
import socket
import stat
import sys

out, home, cfg, port, ppid, real_home = sys.argv[1:7]
port = int(port)
ppid = int(ppid)
r = {"env": dict(os.environ), "reads": {}, "writes": {}, "connect": {}, "proc": {},
     "ptrace": None, "leaked_socks": [], "home": {}, "sensitive": {}}


def mark(table, label, path, mode="r"):
    try:
        with open(path, mode) as f:
            if "w" in mode:
                f.write("x")
            else:
                f.read(64)
        table[label] = "ok"
    except Exception as e:
        table[label] = type(e).__name__ + ":" + str(getattr(e, "errno", "?"))


child_home = os.environ.get("HOME", "")
r["home"]["path"] = child_home
try:
    mode = stat.S_IMODE(os.stat(child_home).st_mode)
    r["home"]["mode"] = oct(mode)
except Exception as e:
    r["home"]["mode"] = type(e).__name__
r["home"]["is_stub"] = bool(child_home and child_home != real_home and
                            "hush-job-" in os.path.basename(child_home))

for label, path in (("token", home + "/session.token"), ("vault", home + "/keys.vault"),
                    ("vibe", cfg + "/vibe.json"), ("etc", "/etc/hosts")):
    mark(r["reads"], label, path)
for label, path in (("home", home + "/planted"), ("cfg", cfg + "/planted"), ("cwd", "planted")):
    mark(r["writes"], label, path, "w")
for label, p in (("relay", port), ("other", 9)):
    try:
        s = socket.create_connection(("127.0.0.1", p), timeout=1)
        s.close()
        r["connect"][label] = "ok"
    except Exception as e:
        r["connect"][label] = type(e).__name__ + ":" + str(getattr(e, "errno", "?"))
for name in ("environ", "mem", "fd"):
    mark(r["proc"], name, "/proc/%s/%s" % (ppid, name), "rb")
try:
    libc = ctypes.CDLL(None, use_errno=True)
    rc = libc.ptrace(16, ppid, None, None)
    r["ptrace"] = "ok" if rc == 0 else "rc=%d errno=%d" % (rc, ctypes.get_errno())
except Exception as e:
    r["ptrace"] = type(e).__name__
for fd in os.listdir("/proc/self/fd"):
    try:
        sck = socket.fromfd(int(fd), socket.AF_INET, socket.SOCK_STREAM)
        try:
            name = sck.getsockname()
            if isinstance(name, tuple) and len(name) == 2:
                r["leaked_socks"].append(list(name))
        finally:
            sck.detach()
    except Exception:
        pass
for label, rel in (("ssh", ".ssh/id_canary"), ("gnupg", ".gnupg/trustdb.gpg"),
                   ("pass", ".password-store/canary.gpg")):
    mark(r["sensitive"], label, os.path.join(real_home, rel))
mark(r["sensitive"], "grok_auth", os.path.join(child_home, ".grok", "auth.json"))
open(out, "w").write(json.dumps(r))
print("sandbox stub reply")
