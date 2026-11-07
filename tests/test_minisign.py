import base64
import tempfile
import unittest
from pathlib import Path

from aerie_build import minisign
from aerie_build.minisign import SignatureError

# RFC 8032 section 7.1, TEST 1 (empty message) and TEST 2 (one byte 0x72).
RFC_PK1 = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
RFC_SIG1 = bytes.fromhex(
    "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555"
    "fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"
)
RFC_PK2 = bytes.fromhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c")
RFC_SIG2 = bytes.fromhex(
    "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
    "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"
)

# ccache's published key (https://ccache.dev/minisign.pub) and the real
# signature of ccache-4.14.2-linux-x86_64-musl-static.tar.xz from its GitHub
# release, plus the BLAKE2b-512 of that tarball.
CCACHE_PUB = "RWQX7yXbBedVfI4PNx6FLdFXu9GHUFsr28s4BVGxm4BeybtnX3P06saF"
CCACHE_FILE = "ccache-4.14.2-linux-x86_64-musl-static.tar.xz"
CCACHE_SIG = (
    "untrusted comment: signature from minisign secret key\n"
    "RUQX7yXbBedVfLXj5XMUVmwgSVzwlObPImpD629tEraYRocKvoGxoTVMoSN9AkFqKYDMNTCYHM4rVZdJNazGgMcCxez8MlCKOgg=\n"
    "trusted comment: timestamp:1792324703\tfile:ccache-4.14.2-linux-x86_64-musl-static.tar.xz\thashed\n"
    "jf1T4N40XOV9P4LpxkXqzeysCr3mRfge8IS0dcsepWdUQHqM4t9mCIikzWQxfzyTnJvEkgjIlaPgxdSUjCySDQ==\n"
)
CCACHE_BLAKE2B = bytes.fromhex(
    "308fe144b753cf528c3044cfc143d30e5e67b79a3e7df748cf3f7825cae9b894"
    "b800ee9fa19ce9a46d7522d808c0466d2ac8d16df95c59159a1aca2007eca286"
)


def flip(data: bytes, index: int) -> bytes:
    out = bytearray(data)
    out[index] ^= 0x01
    return bytes(out)


class Ed25519Test(unittest.TestCase):
    def test_rfc8032_vectors_verify(self):
        self.assertTrue(minisign.ed25519_verify(RFC_PK1, b"", RFC_SIG1))
        self.assertTrue(minisign.ed25519_verify(RFC_PK2, b"\x72", RFC_SIG2))

    def test_rejects_wrong_message_key_or_signature(self):
        self.assertFalse(minisign.ed25519_verify(RFC_PK2, b"\x73", RFC_SIG2))
        self.assertFalse(minisign.ed25519_verify(RFC_PK1, b"\x72", RFC_SIG2))
        self.assertFalse(minisign.ed25519_verify(RFC_PK2, b"\x72", flip(RFC_SIG2, 0)))
        self.assertFalse(minisign.ed25519_verify(RFC_PK2, b"\x72", flip(RFC_SIG2, 40)))

    def test_rejects_non_canonical_s(self):
        # S + L verifies with a naive implementation; RFC 8032 requires S < L.
        s = int.from_bytes(RFC_SIG1[32:], "little") + minisign.L
        if s < 2**256:
            bad = RFC_SIG1[:32] + s.to_bytes(32, "little")
            self.assertFalse(minisign.ed25519_verify(RFC_PK1, b"", bad))

    def test_rejects_malformed_lengths(self):
        self.assertFalse(minisign.ed25519_verify(RFC_PK1[:31], b"", RFC_SIG1))
        self.assertFalse(minisign.ed25519_verify(RFC_PK1, b"", RFC_SIG1[:63]))


class MinisignTest(unittest.TestCase):
    def test_real_ccache_release_signature_verifies(self):
        comment = minisign.verify_prehashed(CCACHE_PUB, CCACHE_SIG, CCACHE_BLAKE2B)
        self.assertIn(f"file:{CCACHE_FILE}", comment)

    def test_fails_on_different_file_contents(self):
        with self.assertRaisesRegex(SignatureError, "signature"):
            minisign.verify_prehashed(CCACHE_PUB, CCACHE_SIG, flip(CCACHE_BLAKE2B, 3))

    def test_fails_on_tampered_trusted_comment(self):
        tampered = CCACHE_SIG.replace("timestamp:1792324703", "timestamp:1792324704")
        with self.assertRaisesRegex(SignatureError, "trusted comment"):
            minisign.verify_prehashed(CCACHE_PUB, tampered, CCACHE_BLAKE2B)

    def test_fails_on_other_key(self):
        raw = base64.b64decode(CCACHE_PUB)
        other_id = base64.b64encode(raw[:2] + flip(raw[2:10], 0) + raw[10:]).decode()
        with self.assertRaisesRegex(SignatureError, "key id"):
            minisign.verify_prehashed(other_id, CCACHE_SIG, CCACHE_BLAKE2B)
        other_key = base64.b64encode(raw[:10] + flip(raw[10:], 5)).decode()
        with self.assertRaises(SignatureError):
            minisign.verify_prehashed(other_key, CCACHE_SIG, CCACHE_BLAKE2B)

    def test_rejects_legacy_non_prehashed_signatures(self):
        lines = CCACHE_SIG.splitlines()
        raw = base64.b64decode(lines[1])
        lines[1] = base64.b64encode(b"Ed" + raw[2:]).decode()
        with self.assertRaisesRegex(SignatureError, "algorithm"):
            minisign.verify_prehashed(CCACHE_PUB, "\n".join(lines), CCACHE_BLAKE2B)

    def test_rejects_garbage(self):
        with self.assertRaises(SignatureError):
            minisign.verify_prehashed(CCACHE_PUB, "not a signature", CCACHE_BLAKE2B)
        with self.assertRaises(SignatureError):
            minisign.verify_prehashed("bm90IGEga2V5", CCACHE_SIG, CCACHE_BLAKE2B)

    def test_verify_file_rejects_other_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "renamed.tar.xz"
            path.write_bytes(b"x")
            with self.assertRaises(SignatureError):
                minisign.verify_file(path, CCACHE_PUB, CCACHE_SIG)

    def test_verify_file_rejects_content_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / CCACHE_FILE
            path.write_bytes(b"not the real tarball")
            with self.assertRaisesRegex(SignatureError, "signature"):
                minisign.verify_file(path, CCACHE_PUB, CCACHE_SIG)


if __name__ == "__main__":
    unittest.main()
