#!/bin/bash
# Syntax-check (clang -fsyntax-only) the overlay's C++ against the real Chromium
# headers of a source tree. Needs a tree that has been built at least to the
# point of generating headers (out/Default/gen), only READS it, and needs cargo
# (to generate the cxx bridge header). No ninja, no gn, no link.
#
#   tools/check-overlay-syntax.sh [file.cc ...]     (default: every .cc in the overlay)
#   TREE=/path/to/src tools/check-overlay-syntax.sh
#
# It does not run anything and does not replace the real build: GN wiring, link
# errors and presubmit rules are not checked. It does catch missing includes,
# wrong signatures and type errors, which is most of what a first compile finds.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
TREE=${TREE:-/mnt/linux2/aerie-build/src/helium-0.21.1.1}
CLANG=$TREE/third_party/llvm-build/Release+Asserts/bin/clang++
GEN=$TREE/out/Default/gen
[ -x "$CLANG" ] && [ -d "$GEN" ] || { echo "need clang and out/Default/gen in $TREE" >&2; exit 2; }
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
O=$REPO/overlay

# The include order puts the stub and the repo's overlay before the tree: a prepared tree holds an older copy of
# the overlay, and the check must read the files being edited, not that copy.
# The cxx bridge header, generated from the real bridge, and the grit header (a stand-in).
mkdir -p "$WORK/ffi/src" "$WORK/stub/components/aerie_adblock/rust/ffi/src" "$WORK/stub/components/aerie_adblock/resources/grit"
cat > "$WORK/ffi/Cargo.toml" <<TOML
[package]
name = "ffitest"
version = "0.1.0"
edition = "2024"
[lib]
crate-type = ["staticlib"]
[dependencies]
aerie_adblock_ffi = { path = "$O/components/aerie_adblock/rust/ffi" }
cxx = "1"
[build-dependencies]
cxx-build = "1"
TOML
echo 'pub use aerie_adblock_ffi::*;' > "$WORK/ffi/src/lib.rs"
echo "fn main() { cxx_build::bridges([\"$O/components/aerie_adblock/rust/ffi/src/lib.rs\", \"$O/components/aerie_adblock/rust/ffi/src/safety.rs\"]).std(\"c++20\").compile(\"b\"); }" > "$WORK/ffi/build.rs"
(cd "$WORK/ffi" && CARGO_TARGET_DIR="$WORK/target" cargo build -q)
cp "$(find "$WORK/target" -path '*include/ffitest*' -name lib.rs.h | head -1)" "$WORK/stub/components/aerie_adblock/rust/ffi/src/lib.rs.h"
cp "$(find "$WORK/target" -path '*include/ffitest*' -name safety.rs.h | head -1)" "$WORK/stub/components/aerie_adblock/rust/ffi/src/safety.rs.h"
printf '#define IDR_AERIE_ADBLOCK_ENGINE 1\n#define IDR_AERIE_ADBLOCK_EASYLIST 2\n#define IDR_AERIE_ADBLOCK_EASYPRIVACY 3\n#define IDR_AERIE_PROTECTION_ENGINE 4\n' > "$WORK/stub/components/aerie_adblock/resources/grit/aerie_adblock_resources.h"
mkdir -p "$WORK/stub/chrome/browser/ui/webui/aerie_filters/grit"
printf '#define IDR_AERIE_FILTERS_HTML 1\n#define IDR_AERIE_FILTERS_JS 2\n#define IDR_AERIE_FILTERS_CSS 3\n' > "$WORK/stub/chrome/browser/ui/webui/aerie_filters/grit/aerie_filters_resources.h"

# The mojom bindings of the overlay's interfaces, from the tree's own generator (reads the tree, writes only $WORK).
for m in $(cd "$O" && find components -name '*.mojom' | sort); do
  mkdir -p "$WORK/mj/src/$(dirname "$m")"; cp "$O/$m" "$WORK/mj/src/$m"
  (cd "$WORK/mj/src" && python3 "$TREE/mojo/public/tools/mojom/mojom_parser.py" --input-root "$WORK/mj/src" \
      --output-root "$WORK/mj/gen" --mojoms "$m" && \
   python3 "$TREE/mojo/public/tools/bindings/mojom_bindings_generator.py" -o "$WORK/mj/gen" generate -d "$WORK/mj/src" \
      --bytecode_path "$GEN/mojo/public/tools/bindings" -g c++ --gen_dir "$WORK/mj/gen" --generate_non_variant_code \
      "$WORK/mj/src/$m")
done

if [ $# -eq 0 ]; then mapfile -t FILES < <(find "$O/components" "$O/chrome" -name '*.cc' | sort); else FILES=("$@"); fi
status=0
for f in "${FILES[@]}"; do
  if "$CLANG" -std=c++23 -w -fsyntax-only -fno-exceptions -DNDEBUG -DUNIT_TEST -DUSE_UDEV -DUSE_AURA=1 -DUSE_OZONE=1 \
      -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE -DLIBCXX_BUILDING_LIBCXXABI=0 \
      -nostdinc++ -isystem "$TREE/third_party/libc++/src/include" -isystem "$TREE/third_party/libc++abi/src/include" \
      -isystem "$TREE/buildtools/third_party/libc++" --sysroot="$TREE/build/linux/debian_bullseye_amd64-sysroot" \
      -I"$WORK/stub" -I"$WORK/mj/gen" -I"$O" -I"$TREE" -I"$GEN" -I"$TREE/v8/include" -I"$GEN/v8/include" \
      -I"$TREE/third_party/abseil-cpp" -I"$TREE/third_party/protobuf/src" -I"$TREE/base/allocator/partition_allocator/src" \
      -I"$GEN/base/allocator/partition_allocator/src" -I"$TREE/third_party/perfetto/include" -I"$GEN/third_party/perfetto" \
      -I"$GEN/third_party/perfetto/build_config" -I"$TREE/third_party/boringssl/src/include" -I"$TREE/third_party/icu/source/common" \
      -I"$TREE/third_party/skia" -I"$TREE/third_party/skia/include/config" -I"$TREE/third_party/vulkan-headers/src/include" \
      -I"$TREE/third_party/libyuv/include" -I"$TREE/third_party/mesa_headers" -I"$TREE/third_party/blink/renderer" \
      -I"$TREE/third_party/ced/src" -I"$TREE/third_party/leveldatabase/src/include" -I"$TREE/net/third_party/quiche/src" \
      -I"$TREE/net/third_party/quiche/overrides" -I"$TREE/third_party/googletest/src/googletest/include" \
      -I"$TREE/third_party/googletest/src/googlemock/include" -ferror-limit=20 "$f" 2> "$WORK/err"; then
    echo "ok     ${f#$REPO/}"
  else
    echo "ERROR  ${f#$REPO/}"; grep -E 'error' -A3 "$WORK/err" | head -30; status=1
  fi
done
exit $status
