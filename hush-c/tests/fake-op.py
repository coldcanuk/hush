#!/usr/bin/env python3
# Test double for the 1Password `op` commands Hush actually runs.
# Records --vault and --title; rejects an nsec on argv (stdin only).
import json
import os
import sys

root = os.environ.get("HUSH_FAKE_OP_DIR", "/tmp/hush-fake-op")
os.makedirs(root, mode=0o700, exist_ok=True)


def item_path(vault, title):
    safe_v = vault.replace("/", "_")
    safe_t = title.replace("/", "_")
    folder = os.path.join(root, "items", safe_v)
    os.makedirs(folder, mode=0o700, exist_ok=True)
    return os.path.join(folder, safe_t)


def write_meta(name, payload):
    path = os.path.join(root, name)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle)
        handle.write("\n")


def reject_secret_on_argv(args, secret=None):
    for arg in args:
        if arg.startswith("nsec"):
            sys.exit(3)
        if secret is not None and secret != "" and arg == secret:
            sys.exit(3)


args = sys.argv[1:]
reject_secret_on_argv(args)

if len(args) == 2 and args[0] == "read":
    parts = args[1].split("/")
    # op://Vault/item/password -> ['op:', '', 'Vault', 'item', 'password']
    if len(parts) != 5 or parts[0] != "op:" or parts[4] != "password":
        sys.exit(2)
    vault = parts[2]
    title = parts[3]
    write_meta(
        "last_read.json",
        {"vault": vault, "title": title, "argv": args},
    )
    path = item_path(vault, title)
    if not os.path.isfile(path):
        sys.exit(1)
    with open(path, "r", encoding="utf-8") as handle:
        sys.stdout.write(handle.read())
    sys.exit(0)

if len(args) >= 2 and args[0] == "item" and args[1] == "create":
    if "--title" not in args or "--vault" not in args or args[-1] != "-":
        sys.exit(2)
    vault = args[args.index("--vault") + 1]
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
    reject_secret_on_argv(args, secret)
    write_meta(
        "last_create.json",
        {
            "vault": vault,
            "title": title,
            "argv": args,
            "stdin_has_password": True,
        },
    )
    with open(item_path(vault, title), "w", encoding="utf-8") as handle:
        handle.write(secret)
    sys.exit(0)

sys.exit(2)
