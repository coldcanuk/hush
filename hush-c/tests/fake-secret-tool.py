#!/usr/bin/env python3
# Test double for the secret-tool commands Hush actually runs.
# Rejects an nsec on argv (stdin only). Records last store argv.
import json
import os
import sys

root = os.environ.get("HUSH_FAKE_SECRET_DIR", "/tmp/hush-fake-secret")
os.makedirs(root, mode=0o700, exist_ok=True)


def value_after(argv, name):
    if name not in argv:
        return None
    idx = argv.index(name)
    if idx + 1 >= len(argv):
        return None
    return argv[idx + 1]


def reject_secret_on_argv(args, secret=None):
    for arg in args:
        if arg.startswith("nsec"):
            sys.exit(3)
        if secret is not None and secret != "" and arg == secret:
            sys.exit(3)


args = sys.argv[1:]
if not args:
    sys.exit(2)
reject_secret_on_argv(args)
item = value_after(args, "item")
service = value_after(args, "service")
if item is None or service != "hush":
    sys.exit(2)
path = os.path.join(root, item.replace("/", "_"))
if args[0] == "lookup":
    if not os.path.isfile(path):
        sys.exit(1)
    with open(path, "r", encoding="utf-8") as handle:
        sys.stdout.write(handle.read())
    sys.exit(0)
if args[0] == "store":
    secret = sys.stdin.read()
    if secret == "":
        sys.exit(1)
    reject_secret_on_argv(args, secret)
    with open(os.path.join(root, "last_store.json"), "w", encoding="utf-8") as handle:
        json.dump({"argv": args, "stdin_has_secret": True}, handle)
        handle.write("\n")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(secret)
    sys.exit(0)
sys.exit(2)
