// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_crash_reports/dump_facts.h"

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/numerics/byte_conversions.h"

namespace aerie_crash_reports {

namespace {

// The minidump layout is Crashpad's (third_party/crashpad/crashpad/minidump/
// minidump_extensions.h); every number is little-endian. These are the only parts read:
//
//   header (32 bytes)            "MDMP", version, stream count @8, directory rva @12
//   directory entry (12 bytes)   type, size, rva
//   Crashpad info stream         simple annotations {size, rva} @36, module list {size, rva} @44
//   simple annotations           count, then {key rva, value rva}
//   module list                  count, then {module index, size, rva} -> module info
//   module info                  version, 3 x {size, rva}; annotation objects @20
//   annotation objects           count, then {name rva, type u16, reserved u16, value rva}
//   string                       length, then the bytes
constexpr uint32_t kCrashpadInfoStreamType = 0x43500001;
constexpr uint16_t kAnnotationTypeString = 1;
constexpr size_t kHeaderSize = 32;
constexpr size_t kDirectoryEntrySize = 12;
constexpr size_t kInfoSize = 52;  // through the module list descriptor
constexpr size_t kModuleLinkSize = 12;
constexpr size_t kModuleInfoSize = 28;
constexpr size_t kObjectSize = 12;
constexpr size_t kPairSize = 8;

// Bounds on the work a file can ask for. A real dump has about nine streams, one module and
// fewer than 100 annotations.
constexpr uint32_t kMaxStreams = 256;
constexpr uint32_t kMaxModules = 16;
constexpr uint32_t kMaxEntries = 2048;
constexpr uint32_t kMaxNameLength = 32;
constexpr uint32_t kMaxValueLength = 32;

class Reader {
 public:
  explicit Reader(base::File file)
      : file_(std::move(file)),
        size_(file_.IsValid() ? std::max<int64_t>(file_.GetLength(), 0) : 0) {}

  bool valid() const { return file_.IsValid(); }

  // True when [offset, offset + length) is inside the file.
  bool Contains(uint64_t offset, uint64_t length) const {
    return offset <= size_ && length <= size_ - offset;
  }

  bool ReadBytes(uint64_t offset, base::span<uint8_t> out) {
    if (!Contains(offset, out.size())) {
      return false;
    }
    if (out.empty()) {
      return true;
    }
    const std::optional<size_t> read =
        file_.ReadNoBestEffort(static_cast<int64_t>(offset), out);
    return read && *read == out.size();
  }

  bool ReadU32(uint64_t offset, uint32_t* out) {
    std::array<uint8_t, 4> bytes;
    if (!ReadBytes(offset, bytes)) {
      return false;
    }
    *out = base::U32FromLittleEndian(bytes);
    return true;
  }

  bool ReadU16(uint64_t offset, uint16_t* out) {
    std::array<uint8_t, 2> bytes;
    if (!ReadBytes(offset, bytes)) {
      return false;
    }
    *out = base::U16FromLittleEndian(bytes);
    return true;
  }

  // A minidump string of at most `max_length` bytes. False when it is longer, or not
  // inside the file.
  bool ReadString(uint64_t rva, uint32_t max_length, std::string* out) {
    uint32_t length = 0;
    if (!ReadU32(rva, &length) || length > max_length) {
      return false;
    }
    out->assign(length, '\0');
    return ReadBytes(rva + 4, base::as_writable_byte_span(*out));
  }

 private:
  base::File file_;
  const uint64_t size_;
};

// A plain token: letters, digits and . - _ + only, at most kMaxValueLength bytes.
std::string PlainToken(const std::string& value) {
  if (value.empty() || value.size() > kMaxValueLength) {
    return std::string();
  }
  for (char c : value) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                    c == '_' || c == '+';
    if (!ok) {
      return std::string();
    }
  }
  return value;
}

bool NameIs(Reader& reader, uint32_t rva, std::string_view want) {
  std::string name;
  return reader.ReadString(rva, kMaxNameLength, &name) && name == want;
}

// "ver" from the simple annotations dictionary of the Crashpad info stream.
std::string FindInDictionary(Reader& reader,
                             uint32_t rva,
                             uint32_t size,
                             std::string_view key) {
  uint32_t count = 0;
  if (size < 4 || !reader.Contains(rva, size) || !reader.ReadU32(rva, &count)) {
    return std::string();
  }
  count = std::min<uint32_t>(count, kMaxEntries);
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t entry = uint64_t{rva} + 4 + uint64_t{i} * kPairSize;
    uint32_t key_rva = 0;
    uint32_t value_rva = 0;
    if (!reader.ReadU32(entry, &key_rva) ||
        !reader.ReadU32(entry + 4, &value_rva)) {
      return std::string();
    }
    if (NameIs(reader, key_rva, key)) {
      std::string value;
      if (reader.ReadString(value_rva, kMaxValueLength, &value)) {
        return PlainToken(value);
      }
      return std::string();
    }
  }
  return std::string();
}

// "ptype" from the annotation objects of one module.
std::string FindInObjects(Reader& reader,
                          uint32_t rva,
                          uint32_t size,
                          std::string_view key) {
  uint32_t count = 0;
  if (size < 4 || !reader.Contains(rva, size) || !reader.ReadU32(rva, &count)) {
    return std::string();
  }
  count = std::min<uint32_t>(count, kMaxEntries);
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t object = uint64_t{rva} + 4 + uint64_t{i} * kObjectSize;
    uint32_t name_rva = 0;
    uint16_t type = 0;
    uint32_t value_rva = 0;
    if (!reader.ReadU32(object, &name_rva) ||
        !reader.ReadU16(object + 4, &type) ||
        !reader.ReadU32(object + 8, &value_rva)) {
      return std::string();
    }
    if (type == kAnnotationTypeString && NameIs(reader, name_rva, key)) {
      std::string value;
      if (reader.ReadString(value_rva, kMaxValueLength, &value)) {
        return PlainToken(value);
      }
      return std::string();
    }
  }
  return std::string();
}

std::string FindInModules(Reader& reader,
                          uint32_t rva,
                          uint32_t size,
                          std::string_view key) {
  uint32_t count = 0;
  if (size < 4 || !reader.Contains(rva, size) || !reader.ReadU32(rva, &count)) {
    return std::string();
  }
  count = std::min<uint32_t>(count, kMaxModules);
  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t link = uint64_t{rva} + 4 + uint64_t{i} * kModuleLinkSize;
    uint32_t info_size = 0;
    uint32_t info_rva = 0;
    if (!reader.ReadU32(link + 4, &info_size) ||
        !reader.ReadU32(link + 8, &info_rva) || info_size < kModuleInfoSize ||
        !reader.Contains(info_rva, info_size)) {
      continue;
    }
    uint32_t objects_size = 0;
    uint32_t objects_rva = 0;
    if (!reader.ReadU32(uint64_t{info_rva} + 20, &objects_size) ||
        !reader.ReadU32(uint64_t{info_rva} + 24, &objects_rva)) {
      continue;
    }
    std::string value = FindInObjects(reader, objects_rva, objects_size, key);
    if (!value.empty()) {
      return value;
    }
  }
  return std::string();
}

}  // namespace

std::optional<DumpFacts> ReadDumpFacts(const base::FilePath& dump) {
  Reader reader(base::File(dump, base::File::FLAG_OPEN | base::File::FLAG_READ));
  if (!reader.valid()) {
    return std::nullopt;
  }

  std::array<uint8_t, kHeaderSize> header;
  if (!reader.ReadBytes(0, header) || header[0] != 'M' || header[1] != 'D' ||
      header[2] != 'M' || header[3] != 'P') {
    return std::nullopt;
  }
  const uint32_t stream_count =
      base::U32FromLittleEndian(base::span(header).subspan<8, 4>());
  const uint32_t directory_rva =
      base::U32FromLittleEndian(base::span(header).subspan<12, 4>());
  if (stream_count > kMaxStreams) {
    return std::nullopt;
  }

  DumpFacts facts;
  for (uint32_t i = 0; i < stream_count; ++i) {
    const uint64_t entry =
        uint64_t{directory_rva} + uint64_t{i} * kDirectoryEntrySize;
    uint32_t type = 0;
    uint32_t size = 0;
    uint32_t rva = 0;
    if (!reader.ReadU32(entry, &type) || !reader.ReadU32(entry + 4, &size) ||
        !reader.ReadU32(entry + 8, &rva)) {
      break;
    }
    if (type != kCrashpadInfoStreamType) {
      continue;
    }
    if (size < kInfoSize || !reader.Contains(rva, size)) {
      break;
    }
    uint32_t simple_size = 0;
    uint32_t simple_rva = 0;
    uint32_t modules_size = 0;
    uint32_t modules_rva = 0;
    if (reader.ReadU32(uint64_t{rva} + 36, &simple_size) &&
        reader.ReadU32(uint64_t{rva} + 40, &simple_rva)) {
      facts.version = FindInDictionary(reader, simple_rva, simple_size, "ver");
    }
    if (reader.ReadU32(uint64_t{rva} + 44, &modules_size) &&
        reader.ReadU32(uint64_t{rva} + 48, &modules_rva)) {
      facts.process_type =
          FindInModules(reader, modules_rva, modules_size, "ptype");
    }
    break;
  }
  return facts;
}

}  // namespace aerie_crash_reports
