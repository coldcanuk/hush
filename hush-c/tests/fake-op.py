#!/usr/bin/env python3
# Test double for the 1Password `op` commands Hush actually runs.
import json
import os
import sys

root = os.environ.get("HUSH_FAKE_OP_DIR", "/tmp/hush-fake-op")
os.makedirs(root, mode=0o700, exist_ok=True)


def item_path(title):
    safe = title.replace("/", "_")
    return os.path.join(root, safe)


args = sys.argv[1:]
if len(args) == 2 and args[0] == "read":
    parts = args[1].split("/")
    # op://Vault/item/password -> ['op:', '', 'Vault', 'item', 'password']
    if len(parts) != 5 or parts[0] != "op:" or parts[4] != "password":
        sys.exit(2)
    path = item_path(parts[3])
    if not os.path.isfile(path):
        sys.exit(1)
    with open(path, "r", encoding="utf-8") as handle:
        sys.stdout.write(handle.read())
    sys.exit(0)

if len(args) >= 2 and args[0] == "item" and args[1] == "create":
    if "--title" not in args or "--vault" not in args or args[-1] != "-":
        sys.exit(2)
    title = args[args.index("--title") + 1]
    raw = sys.stdin.read()
    try:
        doc = json.loads(raw)
    except json.JSONDecodeError:
        sys.exit(2)
    secret = ""
    for field in doc.get("fields", []):
        if field.get("id") == "password":
            secret = field.get("value", "")
    if secret == "":
        sys.exit(1)
    with open(item_path(title), "w", encoding="utf-8") as handle:
        handle.write(secret)
    sys.exit(0)

sys.exit(2)
