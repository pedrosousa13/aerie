"""The Rust side of the Filter Engine, tested outside Chromium. Needs cargo, a C++ compiler and crates.io.

Off by default, because the rest of the suite is offline. Run it with:

    AERIE_TEST_CARGO=1 python3 -m unittest tests.test_adblock_rust -v

1. `cargo test` over the standalone workspace (core, ffi bridge, build tool).
2. A real FFI smoke test: the cxx bridge is generated and built into a static
   library, and the real browser/adblock_engine.cc is compiled with a C++
   compiler, against stand-ins for the few base/ and url/ headers it includes,
   and linked to it. tests/adblock_ffi_smoke/main.cc then drives the engine
   from C++: block, allow, serialize, reload, refuse junk.
3. The build tool, end to end: lists in, serialized engine out, self-checks
   that pass, self-checks that fail, an empty list, and a missing list.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
COMPONENT = REPO / "overlay" / "components" / "aerie_adblock"
WORKSPACE = COMPONENT / "rust"
ENABLED = os.environ.get("AERIE_TEST_CARGO") == "1" and shutil.which("cargo") is not None


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


@unittest.skipUnless(ENABLED, "set AERIE_TEST_CARGO=1 (needs cargo and crates.io)")
class RustTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.tmp = Path(cls._tmp.name)
        cls.target = cls.tmp / "target"
        cls.env = dict(os.environ, CARGO_TARGET_DIR=str(cls.target))

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_cargo_test(self):
        proc = sh(["cargo", "test", "--manifest-path", str(WORKSPACE / "Cargo.toml")], env=self.env)
        self.assertEqual(proc.returncode, 0, proc.stdout[-3000:] + proc.stderr[-3000:])
        self.assertIn("test result: ok", proc.stdout)

    def test_ffi_smoke_from_cpp(self):
        compiler = shutil.which("g++") or shutil.which("clang++")
        if not compiler:
            self.skipTest("no C++ compiler")
        work = self.tmp / "ffi"
        (work / "src").mkdir(parents=True)
        (work / "Cargo.toml").write_text(f"""[package]
name = "ffitest"
version = "0.1.0"
edition = "2024"
[lib]
crate-type = ["staticlib"]
[dependencies]
aerie_adblock_ffi = {{ path = "{WORKSPACE / 'ffi'}" }}
cxx = "1"
[build-dependencies]
cxx-build = "1"
""")
        (work / "src" / "lib.rs").write_text("pub use aerie_adblock_ffi::*;\n")
        (work / "build.rs").write_text(
            f'fn main() {{ cxx_build::bridge("{WORKSPACE / "ffi/src/lib.rs"}").std("c++20").compile("aerie_bridge"); }}\n')
        env = dict(self.env, CARGO_TARGET_DIR=str(work / "target"))
        proc = sh(["cargo", "build", "--manifest-path", str(work / "Cargo.toml")], env=env)
        self.assertEqual(proc.returncode, 0, proc.stderr[-3000:])
        build = work / "target" / "debug" / "build"
        generated = next(p for p in build.rglob("lib.rs.h") if "include/ffitest" in str(p))
        bridge_lib = next(build.rglob("libaerie_bridge.a"))
        cxx_h = next((Path.home() / ".cargo/registry/src").glob("*/cxx-1.*/include/cxx.h"))
        inc = work / "inc"
        for rel, text in {
            "base/containers/span.h": "#pragma once\n#include <span>\n#include <string_view>\n#include <cstdint>\n"
                                      "namespace base { template <class T> using span = std::span<T>;\n"
                                      "inline std::span<const uint8_t> as_byte_span(std::string_view s){"
                                      "return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};} }\n",
            "base/containers/span_rust.h": '#pragma once\n#include "base/containers/span.h"\n'
                                           '#include "third_party/rust/cxx/v1/cxx.h"\n'
                                           "namespace base { inline rust::Slice<const uint8_t> SpanToRustSlice("
                                           "span<const uint8_t> s){ return rust::Slice<const uint8_t>(s.data(), s.size()); } }\n",
            "base/strings/string_util.h": "#pragma once\n#include <string_view>\nnamespace base { inline bool "
                                          "IsStringASCII(std::string_view s){ for(unsigned char c: s) if(c>127) "
                                          "return false; return true; } }\n",
            "url/origin.h": "#pragma once\nnamespace url { class Origin; }\n",
        }.items():
            (inc / rel).parent.mkdir(parents=True, exist_ok=True)
            (inc / rel).write_text(text)
        for src, rel in [(generated, "components/aerie_adblock/rust/ffi/src/lib.rs.h"),
                         (cxx_h, "third_party/rust/cxx/v1/cxx.h")]:
            (inc / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(src, inc / rel)
        exe = work / "smoke"
        staticlib = work / "target" / "debug" / "libffitest.a"
        cc = sh([compiler, "-std=c++20", "-Wall", f"-I{inc}", f"-I{REPO / 'overlay'}",
                 str(REPO / "tests/adblock_ffi_smoke/main.cc"), str(COMPONENT / "browser/adblock_engine.cc"),
                 "-o", str(exe), str(staticlib), str(bridge_lib), "-lpthread", "-ldl"])
        self.assertEqual(cc.returncode, 0, cc.stderr[-3000:])
        run = sh([str(exe)])
        self.assertEqual((run.returncode, run.stdout.strip()), (0, "ok"), run.stderr)

    def test_compile_tool(self):
        build = sh(["cargo", "build", "--manifest-path", str(WORKSPACE / "Cargo.toml"), "-p", "aerie_adblock_compile"],
                   env=self.env)
        self.assertEqual(build.returncode, 0, build.stderr[-3000:])
        tool = self.target / "debug" / "aerie_adblock_compile"
        lst = self.tmp / "l.txt"
        lst.write_text("||ads.example^$script\n")
        out = self.tmp / "engine.dat"
        ok = sh([str(tool), "--out", str(out), "--expect-block", "https://ads.example/a.js|ads.example|n.test|script|true",
                 "--expect-allow", "https://n.test/a.js|n.test|n.test|script|false", str(lst)])
        self.assertEqual(ok.returncode, 0, ok.stderr)
        self.assertGreater(out.stat().st_size, 0)
        out.unlink()
        wrong = sh([str(tool), "--out", str(out), "--expect-block", "https://other.example/a.js|other.example|n.test|script|true", str(lst)])
        self.assertNotEqual(wrong.returncode, 0)
        self.assertIn("self-check failed", wrong.stderr)
        self.assertFalse(out.exists(), "a failed self-check must leave no engine")
        empty = self.tmp / "empty.txt"
        empty.write_text("")
        self.assertNotEqual(sh([str(tool), "--out", str(out), str(empty)]).returncode, 0)
        self.assertNotEqual(sh([str(tool), "--out", str(out), str(self.tmp / "missing.txt")]).returncode, 0)
        self.assertFalse(out.exists())
        self.assertNotEqual(sh([str(tool), str(lst)]).returncode, 0)  # no --out

    def test_compile_tool_cosmetic_self_checks(self):
        build = sh(["cargo", "build", "--manifest-path", str(WORKSPACE / "Cargo.toml"), "-p", "aerie_adblock_compile"],
                   env=self.env)
        self.assertEqual(build.returncode, 0, build.stderr[-3000:])
        tool = self.target / "debug" / "aerie_adblock_compile"
        lst = self.tmp / "c.txt"
        lst.write_text("##.ad-slot\nnews.test##.news-only\n")
        out = self.tmp / "cosmetic.dat"
        ok = sh([str(tool), "--out", str(out),
                 "--expect-hide", "https://news.test/|ad-slot||.ad-slot",
                 "--expect-hide", "https://news.test/|||.news-only",
                 "--expect-keep", "https://other.test/|||.news-only",
                 "--expect-keep", "https://news.test/|story||.story", str(lst)])
        self.assertEqual(ok.returncode, 0, ok.stderr)
        out.unlink()
        for spec, flag in [("https://news.test/|story||.ad-slot", "--expect-hide"),
                           ("https://news.test/|ad-slot||.ad-slot", "--expect-keep")]:
            bad = sh([str(tool), "--out", str(out), flag, spec, str(lst)])
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn("self-check failed", bad.stderr)
            self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
