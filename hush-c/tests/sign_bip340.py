#!/usr/bin/env python3
"""BIP-340 signer for Hush tests. No external dependencies.

Signs a NIP-42 (kind 22242) AUTH event and prints it as compact JSON.
Pure-Python EC arithmetic, implemented independently of the C verifier so
wire tests do not self-certify. Used by check_authz.py at test time and
once to pin the fixed-key fixture in tests/test_nip42.c.

Usage: sign_bip340.py --key <64-hex seckey> --challenge <str> [--relay url]
"""

import argparse
import hashlib
import json
import sys
import time

P = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
GX = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
GY = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8


def point_add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    x1, y1 = p1
    x2, y2 = p2
    if x1 == x2 and (y1 + y2) % P == 0:
        return None
    if p1 == p2:
        slope = (3 * x1 * x1) * pow(2 * y1, -1, P) % P
    else:
        slope = (y2 - y1) * pow((x2 - x1) % P, -1, P) % P
    x3 = (slope * slope - x1 - x2) % P
    y3 = (slope * (x1 - x3) - y1) % P
    return (x3, y3)


def point_mul(k, point):
    result = None
    addend = point
    while k:
        if k & 1:
            result = point_add(result, addend)
        addend = point_add(addend, addend)
        k >>= 1
    return result


def pubkey_xonly(seckey):
    point = point_mul(seckey, (GX, GY))
    return point[0].to_bytes(32, "big")


def tagged_hash(tag, data):
    tag_hash = hashlib.sha256(tag.encode()).digest()
    return hashlib.sha256(tag_hash + tag_hash + data).digest()


def sign(seckey, message):
    """BIP-340 sign. Deterministic aux = 32 zero bytes (test-only)."""
    d = seckey
    point_p = point_mul(d, (GX, GY))
    if point_p[1] % 2 == 1:
        d = N - d
    aux = b"\x00" * 32
    while True:
        t = (d ^ int.from_bytes(tagged_hash("BIP0340/aux", aux), "big")).to_bytes(32, "big")
        k = int.from_bytes(tagged_hash("BIP0340/nonce", t + pubkey_xonly(d) + message), "big") % N
        if k == 0:
            aux = hashlib.sha256(aux).digest()
            continue
        point_r = point_mul(k, (GX, GY))
        if point_r is None:
            aux = hashlib.sha256(aux).digest()
            continue
        rx = point_r[0]
        if point_r[1] % 2 == 1:
            k = N - k
        e = int.from_bytes(
            tagged_hash("BIP0340/challenge", rx.to_bytes(32, "big") + pubkey_xonly(d) + message),
            "big",
        ) % N
        s = (k + e * d) % N
        if s != 0:
            return (rx.to_bytes(32, "big") + s.to_bytes(32, "big")).hex()


def make_auth(seckey_hex, challenge, relay, created_at, no_relay=False,
              no_challenge=False, kind=22242, content=""):
    seckey = int(seckey_hex, 16)
    pub = pubkey_xonly(seckey).hex()
    tags = []
    if not no_relay:
        tags.append(["relay", relay])
    if not no_challenge:
        tags.append(["challenge", challenge])
    serialized = json.dumps([0, pub, created_at, kind, tags, content],
                            separators=(",", ":"), ensure_ascii=False)
    event = {
        "id": hashlib.sha256(serialized.encode()).hexdigest(),
        "pubkey": pub,
        "kind": kind,
        "created_at": created_at,
        "content": content,
        "tags": tags,
    }
    event["sig"] = sign(seckey, bytes.fromhex(event["id"]))
    return event


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--key", required=True, help="seckey as 64 hex chars")
    parser.add_argument("--challenge", required=True)
    parser.add_argument("--relay", default="ws://localhost")
    parser.add_argument("--created-at", type=int, default=None)
    parser.add_argument("--no-relay", action="store_true")
    parser.add_argument("--no-challenge", action="store_true")
    parser.add_argument("--kind", type=int, default=22242)
    parser.add_argument("--content", default="")
    args = parser.parse_args()
    created = args.created_at if args.created_at is not None else int(time.time())
    event = make_auth(args.key, args.challenge, args.relay, created,
                      no_relay=args.no_relay, no_challenge=args.no_challenge,
                      kind=args.kind, content=args.content)
    sys.stdout.write(json.dumps(event, separators=(",", ":")) + "\n")


if __name__ == "__main__":
    main()
