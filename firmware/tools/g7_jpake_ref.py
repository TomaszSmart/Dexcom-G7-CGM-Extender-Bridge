#!/usr/bin/env python3
"""
Reference implementation of the Dexcom G7 EC-JPAKE handshake, ported 1:1 from
xDrip+'s libkeks (jamorham.keks.Calc / Context / Packet / Config) so the C
firmware port (src/g7_jpake.c) can be checked against known-good bytes.

    python g7_jpake_ref.py            # self-check (both sides agree on the key)
    python g7_jpake_ref.py --header   # regenerate ../src/g7_jpake_testvec.h

Notes on libkeks quirks that this reproduces on purpose:
  * libkeks' Util.hexStringToByteArray swaps the nibbles of every byte, so the
    Config constants ALICE "36C69656E647" / BOB "375627675627" really are the
    ASCII strings "client" / "server".
  * The password is the pairing code's ASCII bytes read as an unsigned
    big-endian integer ("7345" -> 0x37333435); a 6-digit code gets an ASCII
    "00" prefix first.
  * Zero-knowledge-proof hash = SHA-256 over (u32be len || bytes) for g, gv, gx
    and the party id, with points in uncompressed SEC1 form (04 || X || Y),
    reduced mod n.
  * A 160-byte round packet is X || Y || Vx || Vy || r (32 bytes each).
  * Session key = first 16 bytes of SHA-256(x-coordinate of the shared point).
  * Challenge hash = first 8 bytes of AES-128-ECB(key, challenge || challenge).
"""

import hashlib
import os
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

# --- secp256r1 -------------------------------------------------------------
P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
G = (0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
     0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5)

ALICE = b"client"  # our (central / app) party id
BOB = b"server"    # the sensor's party id


def inv(a, m):
    return pow(a, -1, m)


def on_curve(pt):
    if pt is None:
        return False
    x, y = pt
    return 0 <= x < P and 0 <= y < P and (y * y - (x * x * x + A * x + B)) % P == 0


def add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    x1, y1 = p1
    x2, y2 = p2
    if x1 == x2:
        if (y1 + y2) % P == 0:
            return None
        lam = (3 * x1 * x1 + A) * inv(2 * y1, P) % P
    else:
        lam = (y2 - y1) * inv(x2 - x1, P) % P
    x3 = (lam * lam - x1 - x2) % P
    return x3, (lam * (x1 - x3) - y1) % P


def neg(pt):
    return pt[0], (-pt[1]) % P


def mul(k, pt):
    k %= N
    r = None
    while k:
        if k & 1:
            r = add(r, pt)
        pt = add(pt, pt)
        k >>= 1
    return r


def b32(i):
    return i.to_bytes(32, "big")


def enc(pt):  # BouncyCastle ECPoint.getEncoded(false)
    return b"\x04" + b32(pt[0]) + b32(pt[1])


# --- Calc.java -------------------------------------------------------------
def password_int(code):
    pw = code.encode()
    if len(code) == 6:
        pw = b"00" + pw
    return int.from_bytes(pw, "big")


def zkp_hash(g, gv, gx, party):
    d = b""
    for item in (enc(g), enc(gv), enc(gx), party):
        d += len(item).to_bytes(4, "big") + item
    return int.from_bytes(hashlib.sha256(d).digest(), "big") % N


def zkp_make(g, x, X, v, party):
    """Returns the 160-byte packet proving knowledge of x with X = g*x."""
    V = mul(v, g)
    r = (v - zkp_hash(g, V, X, party) * x) % N
    return b32(X[0]) + b32(X[1]) + b32(V[0]) + b32(V[1]) + b32(r)


def packet_parse(pkt):
    ints = [int.from_bytes(pkt[i:i + 32], "big") for i in range(0, 160, 32)]
    return (ints[0], ints[1]), (ints[2], ints[3]), ints[4]


def zkp_check(g, pkt, party):
    X, V, r = packet_parse(pkt)
    if not on_curve(X) or not on_curve(V):
        return False
    return add(mul(r, g), mul(zkp_hash(g, V, X, party), X)) == V


class Party:
    """One side of the handshake. `me`/`peer` are ALICE/BOB (or swapped)."""

    def __init__(self, code, me, peer, xa, xb, v1, v2, v3):
        self.s = password_int(code)
        self.me, self.peer = me, peer
        self.xa, self.xb = xa, xb
        self.Xa, self.Xb = mul(xa, G), mul(xb, G)
        self.v = (v1, v2, v3)
        self.peer1 = self.peer2 = self.peer3 = None

    def round1(self):
        return zkp_make(G, self.xa, self.Xa, self.v[0], self.me)

    def round2(self):
        return zkp_make(G, self.xb, self.Xb, self.v[1], self.me)

    def take_round1(self, pkt):
        assert zkp_check(G, pkt, self.peer)
        self.peer1 = packet_parse(pkt)[0]

    def take_round2(self, pkt):
        assert zkp_check(G, pkt, self.peer)
        self.peer2 = packet_parse(pkt)[0]

    def round3(self):
        g = add(add(self.Xa, self.peer1), self.peer2)
        x = self.xb * self.s % N
        return zkp_make(g, x, mul(x, g), self.v[2], self.me)

    def take_round3(self, pkt):
        g = add(add(self.Xa, self.Xb), self.peer1)
        assert zkp_check(g, pkt, self.peer)
        self.peer3 = packet_parse(pkt)[0]

    def key(self):
        k = mul(self.xb, add(self.peer3, neg(mul(self.xb * self.s % N, self.peer2))))
        return hashlib.sha256(b32(k[0])).digest()[:16]


def challenge_hash(key, chal8):
    e = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    return (e.update(chal8 + chal8) + e.finalize())[:8]


# --- fixed test vector -----------------------------------------------------
def scalar(tag):
    return int.from_bytes(hashlib.sha256(b"g7-jpake-test:" + tag).digest(), "big") % (N - 1) + 1


CODE = "7345"
CHALLENGE = bytes.fromhex("0123456789abcdef")


def build_vector():
    app = Party(CODE, ALICE, BOB, *(scalar(t) for t in (b"x1", b"x2", b"v1", b"v2", b"v3")))
    sensor = Party(CODE, BOB, ALICE, *(scalar(t) for t in (b"x3", b"x4", b"w1", b"w2", b"w3")))

    # Order on the wire (libkeks Plugin.aNext): the sensor sends each of its
    # packets first, then we answer with ours.
    s1 = sensor.round1(); app.take_round1(s1)
    a1 = app.round1(); sensor.take_round1(a1)
    s2 = sensor.round2(); app.take_round2(s2)
    a2 = app.round2(); sensor.take_round2(a2)
    s3 = sensor.round3(); app.take_round3(s3)
    a3 = app.round3(); sensor.take_round3(a3)

    key = app.key()
    assert key == sensor.key(), "shared keys differ"
    assert Party(CODE[:-1] + "0", ALICE, BOB, 1, 2, 3, 4, 5).s != app.s
    return dict(app=app, s1=s1, s2=s2, s3=s3, a1=a1, a2=a2, a3=a3, key=key,
                chal=CHALLENGE, chal_hash=challenge_hash(key, CHALLENGE))


def c_bytes(name, data):
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 12]) for i in range(0, len(data), 12)]
    return f"static const uint8_t {name}[{len(data)}] = {{\n\t" + ",\n\t".join(rows) + ",\n};\n"


def write_header(v, path):
    app = v["app"]
    out = [
        "/* Generated by tools/g7_jpake_ref.py -- do not edit. */\n",
        "/* Known-good EC-JPAKE transcript (xDrip+ libkeks semantics), app = \"client\". */\n",
        "#ifndef G7_JPAKE_TESTVEC_H_\n#define G7_JPAKE_TESTVEC_H_\n\n#include <stdint.h>\n\n",
        f"#define TV_CODE \"{CODE}\"\n\n",
    ]
    for name, val in (("tv_x1", app.xa), ("tv_x2", app.xb), ("tv_v1", app.v[0]),
                      ("tv_v2", app.v[1]), ("tv_v3", app.v[2])):
        out.append(c_bytes(name, b32(val)))
    for name in ("s1", "s2", "s3", "a1", "a2", "a3"):
        out.append(c_bytes("tv_" + name, v[name]))
    out.append(c_bytes("tv_key", v["key"]))
    out.append(c_bytes("tv_chal", v["chal"]))
    out.append(c_bytes("tv_chal_hash", v["chal_hash"]))
    out.append("\n#endif /* G7_JPAKE_TESTVEC_H_ */\n")
    with open(path, "w", newline="\n") as f:
        f.write("".join(out))


def selfcheck():
    # Random run, plus: a wrong pairing code must yield a different key.
    rnd = lambda: int.from_bytes(os.urandom(32), "big") % (N - 1) + 1
    a = Party("7345", ALICE, BOB, *(rnd() for _ in range(5)))
    s = Party("7346", BOB, ALICE, *(rnd() for _ in range(5)))
    a.take_round1(s.round1()); s.take_round1(a.round1())
    a.take_round2(s.round2()); s.take_round2(a.round2())
    a.take_round3(s.round3()); s.take_round3(a.round3())
    assert a.key() != s.key(), "wrong code must not produce the same key"
    assert not zkp_check(G, s.round1(), ALICE), "party id must be bound into the proof"
    v = build_vector()
    print("OK  key =", v["key"].hex(), " chal_hash =", v["chal_hash"].hex())
    return v


if __name__ == "__main__":
    vec = selfcheck()
    if "--header" in sys.argv:
        dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "g7_jpake_testvec.h")
        write_header(vec, dst)
        print("wrote", os.path.normpath(dst))
