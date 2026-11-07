"""Verify minisign signatures with the Python standard library only.

minisign (https://jedisct1.github.io/minisign/) signs the BLAKE2b-512 hash of
a file with Ed25519 ("ED" algorithm), then signs that signature plus a
"trusted comment" with the same key (the global signature). Both must hold.
The stdlib has SHA-512 and BLAKE2b but no Ed25519, so Ed25519 verification
is implemented here from RFC 8032 section 5.1.7. Only verification is
needed, so no secret material and no constant-time concerns are involved.

Every failure raises SignatureError; nothing returns a "maybe".
"""

from __future__ import annotations

import base64
import binascii
import hashlib
from pathlib import Path

# --- Ed25519 (RFC 8032) ------------------------------------------------------

P = 2**255 - 19
L = 2**252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)

_BY = 4 * pow(5, P - 2, P) % P


def _recover_x(y: int, sign: int) -> int | None:
    if y >= P:
        return None
    x2 = (y * y - 1) * pow(D * y * y + 1, P - 2, P) % P
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P != 0:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P != 0:
        return None
    if (x & 1) != sign:
        x = P - x
    return x


_BX = _recover_x(_BY, 0)
_B = (_BX, _BY, 1, _BX * _BY % P)  # extended coordinates (X, Y, Z, T)
_IDENTITY = (0, 1, 1, 0)


def _add(p1, p2):
    a = (p1[1] - p1[0]) * (p2[1] - p2[0]) % P
    b = (p1[1] + p1[0]) * (p2[1] + p2[0]) % P
    c = 2 * p1[3] * p2[3] * D % P
    d = 2 * p1[2] * p2[2] % P
    e, f, g, h = b - a, d - c, d + c, b + a
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _mul(scalar: int, point):
    result = _IDENTITY
    while scalar:
        if scalar & 1:
            result = _add(result, point)
        point = _add(point, point)
        scalar >>= 1
    return result


def _equal(p1, p2) -> bool:
    return (p1[0] * p2[2] - p2[0] * p1[2]) % P == 0 and (p1[1] * p2[2] - p2[1] * p1[2]) % P == 0


def _decompress(data: bytes):
    if len(data) != 32:
        return None
    y = int.from_bytes(data, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    if x is None:
        return None
    return (x, y, 1, x * y % P)


def ed25519_verify(public_key: bytes, message: bytes, signature: bytes) -> bool:
    if len(public_key) != 32 or len(signature) != 64:
        return False
    a = _decompress(public_key)
    r = _decompress(signature[:32])
    if a is None or r is None:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= L:
        return False
    h = int.from_bytes(hashlib.sha512(signature[:32] + public_key + message).digest(), "little") % L
    return _equal(_mul(s, _B), _add(r, _mul(h, a)))


# --- minisign ----------------------------------------------------------------


class SignatureError(Exception):
    """A minisign signature or key is invalid or does not match."""


def _b64(text: str, what: str) -> bytes:
    try:
        return base64.b64decode(text.strip(), validate=True)
    except (binascii.Error, ValueError) as exc:
        raise SignatureError(f"{what} is not valid base64") from exc


def parse_public_key(text: str) -> tuple[bytes, bytes]:
    """Return (key_id, ed25519_key) from a minisign public key (bare or .pub file)."""
    lines = [ln for ln in text.strip().splitlines() if ln and not ln.startswith("untrusted comment:")]
    if len(lines) != 1:
        raise SignatureError("public key must be one base64 line")
    raw = _b64(lines[0], "public key")
    if len(raw) != 42 or raw[:2] != b"Ed":
        raise SignatureError("not a minisign Ed25519 public key")
    return raw[2:10], raw[10:]


def verify_prehashed(public_key: str, minisig: str, blake2b_digest: bytes) -> str:
    """Check a .minisig against a file's BLAKE2b-512 digest. Returns the trusted comment."""
    key_id, key = parse_public_key(public_key)
    lines = minisig.strip("\n").split("\n")
    if len(lines) != 4 or not lines[2].startswith("trusted comment: "):
        raise SignatureError("malformed .minisig file")
    sig = _b64(lines[1], "signature")
    if len(sig) != 74:
        raise SignatureError("malformed signature")
    if sig[:2] != b"ED":
        raise SignatureError("unsupported signature algorithm (only prehashed ED is accepted)")
    if sig[2:10] != key_id:
        raise SignatureError("signature key id does not match the trusted public key")
    if not ed25519_verify(key, blake2b_digest, sig[10:]):
        raise SignatureError("signature does not match the file contents")
    trusted = lines[2][len("trusted comment: "):]
    global_sig = _b64(lines[3], "global signature")
    if not ed25519_verify(key, sig[10:] + trusted.encode(), global_sig):
        raise SignatureError("trusted comment signature is invalid")
    return trusted


def blake2b_file(path: Path) -> bytes:
    digest = hashlib.blake2b(digest_size=64)
    with open(path, "rb") as fh:
        while block := fh.read(1 << 20):
            digest.update(block)
    return digest.digest()


def verify_file(path: Path, public_key: str, minisig: str) -> str:
    """Verify path against its minisign signature; the signed file name must match too."""
    trusted = verify_prehashed(public_key, minisig, blake2b_file(path))
    fields = dict(f.split(":", 1) for f in trusted.split("\t") if ":" in f)
    if fields.get("file") != path.name:
        raise SignatureError(
            f"signature is for {fields.get('file')!r}, not {path.name!r}"
        )
    return trusted
