#!/usr/bin/env python3
"""theme_styles_diff.py: compare two theme_styles_dump.cjs outputs.

usage: python3 tests/theme_styles_diff.py <old.json> <new.json>
Per width and theme it prints the number of elements present in both, the
custom properties (--*) on <html> that are new or changed, and the number of
element/property values that differ. Not part of make test.
"""
import json, sys


def main():
    a = json.load(open(sys.argv[1]))
    b = json.load(open(sys.argv[2]))
    for w in sorted(a, key=int):
        for t in a[w]:
            ea, eb = a[w][t], b[w][t]
            common = [k for k in ea if k in eb]
            root = next(k for k in common if k.startswith("html"))
            ta = {p: v for p, v in ea[root].items() if p.startswith("--")}
            tb = {p: v for p, v in eb[root].items() if p.startswith("--")}
            new = sorted(p for p in tb if p not in ta)
            changed = sorted(p for p in tb if p in ta and ta[p] != tb[p])
            diffs = sum(1 for k in common for p, v in eb[k].items()
                        if not p.startswith("--") and ea[k].get(p) != v)
            print(f"{w} {t}: {len(common)} elements, {len(tb)} tokens, "
                  f"{len(new)} new ({' '.join(new[:3])}{' ...' if len(new) > 3 else ''}), "
                  f"{len(changed)} changed, {diffs} element value diffs")


if __name__ == "__main__":
    main()
