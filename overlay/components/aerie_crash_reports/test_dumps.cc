// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_crash_reports/test_dumps.h"

#include <string.h>

namespace aerie_crash_reports {

namespace {

constexpr uint32_t kCrashpadInfoStream = 0x43500001;
constexpr uint32_t kOtherStream = 0x4b6b0002;

void PutU32(std::string& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
  }
}

void PutU16(std::string& out, uint16_t value) {
  out.push_back(static_cast<char>(value & 0xff));
  out.push_back(static_cast<char>((value >> 8) & 0xff));
}

void SetU32(std::string& out, size_t at, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out[at + i] = static_cast<char>((value >> (8 * i)) & 0xff);
  }
}

// A MinidumpUTF8String: the length, the bytes, a NUL, padded to 4 bytes. Returns its RVA.
uint32_t PutString(std::string& out, const std::string& text) {
  const uint32_t rva = static_cast<uint32_t>(out.size());
  PutU32(out, static_cast<uint32_t>(text.size()));
  out += text;
  out.push_back('\0');
  while (out.size() % 4 != 0) {
    out.push_back('\0');
  }
  return rva;
}

}  // namespace

std::string MakeTestDump(const TestDumpSpec& spec) {
  std::string out;
  // MINIDUMP_HEADER: "MDMP", version, 2 streams, directory at 32, checksum, time, flags.
  out += "MDMP";
  PutU32(out, 0xa793);
  PutU32(out, 2);
  PutU32(out, 32);
  PutU32(out, 0);
  PutU32(out, 0);
  PutU32(out, 0);
  PutU32(out, 0);
  // The directory: two entries of {type, size, rva}. Sizes and rvas are filled in below.
  const size_t directory = out.size();
  for (int i = 0; i < 6; ++i) {
    PutU32(out, 0);
  }

  // A stream a reader does not care about, with the secret in it.
  const uint32_t other_rva = static_cast<uint32_t>(out.size());
  out += spec.secret;
  while (out.size() % 4 != 0) {
    out.push_back('\0');
  }
  const uint32_t other_size = static_cast<uint32_t>(out.size()) - other_rva;

  out.append(spec.padding_bytes, 'M');
  while (out.size() % 4 != 0) {
    out.push_back('\0');
  }

  // The strings and tables the Crashpad info stream points at.
  const uint32_t key_ver = PutString(out, "ver");
  const uint32_t val_ver = PutString(out, spec.version);
  const uint32_t key_secret = PutString(out, "lsb-release");
  const uint32_t val_secret = PutString(out, spec.secret);
  const uint32_t simple_rva = static_cast<uint32_t>(out.size());
  PutU32(out, 2);  // count
  PutU32(out, key_secret);
  PutU32(out, val_secret);
  PutU32(out, key_ver);
  PutU32(out, val_ver);
  const uint32_t simple_size = static_cast<uint32_t>(out.size()) - simple_rva;

  const uint32_t name_ptype = PutString(out, "ptype");
  const uint32_t value_ptype = PutString(out, spec.process_type);
  const uint32_t name_switch = PutString(out, "switch-1");
  const uint32_t value_switch = PutString(out, spec.secret);
  const uint32_t objects_rva = static_cast<uint32_t>(out.size());
  PutU32(out, 2);  // count
  PutU32(out, name_switch);
  PutU16(out, 1);  // kString
  PutU16(out, 0);
  PutU32(out, value_switch);
  PutU32(out, name_ptype);
  PutU16(out, 1);
  PutU16(out, 0);
  PutU32(out, value_ptype);
  const uint32_t objects_size = static_cast<uint32_t>(out.size()) - objects_rva;

  // MinidumpModuleCrashpadInfo: version 1, list annotations, simple annotations, objects.
  const uint32_t module_rva = static_cast<uint32_t>(out.size());
  PutU32(out, 1);
  PutU32(out, 0);
  PutU32(out, 0);
  PutU32(out, 0);
  PutU32(out, 0);
  PutU32(out, objects_size);
  PutU32(out, objects_rva);
  const uint32_t module_size = static_cast<uint32_t>(out.size()) - module_rva;

  // MinidumpModuleCrashpadInfoList: a count, then {module index, size, rva} links.
  const uint32_t modules_rva = static_cast<uint32_t>(out.size());
  PutU32(out, 1);
  PutU32(out, 0);
  PutU32(out, module_size);
  PutU32(out, module_rva);
  const uint32_t modules_size = static_cast<uint32_t>(out.size()) - modules_rva;

  // MinidumpCrashpadInfo: version, report id, client id (16 bytes each), simple
  // annotations {size, rva}, module list {size, rva}, reserved, address mask.
  const uint32_t info_rva = static_cast<uint32_t>(out.size());
  PutU32(out, 1);
  out.append(32, '\x11');
  PutU32(out, simple_size);
  PutU32(out, simple_rva);
  PutU32(out, modules_size);
  PutU32(out, modules_rva);
  PutU32(out, 0);
  PutU32(out, 0);
  PutU32(out, 0);
  const uint32_t info_size = static_cast<uint32_t>(out.size()) - info_rva;

  SetU32(out, directory + 0, kOtherStream);
  SetU32(out, directory + 4, other_size);
  SetU32(out, directory + 8, other_rva);
  SetU32(out, directory + 12, kCrashpadInfoStream);
  SetU32(out, directory + 16, info_size);
  SetU32(out, directory + 20, info_rva);
  return out;
}

}  // namespace aerie_crash_reports
