// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_CRASH_REPORTS_TEST_DUMPS_H_
#define COMPONENTS_AERIE_CRASH_REPORTS_TEST_DUMPS_H_

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

namespace aerie_crash_reports {

// Builds the bytes of a small Crashpad minidump for tests: the header, a stream directory
// with a Crashpad info stream, its simple annotations (where Crashpad's handler puts
// "ver"), and one module whose annotation objects hold "ptype". `secret` goes in as an
// annotation that a reader must never return. The layout is the one a real Aerie dump
// has (checked against a dump of ./out/Default/aerie).
struct TestDumpSpec {
  std::string process_type = "browser";
  std::string version = "0.1.0";
  std::string secret = "SECRET-PAGE-TEXT";
  // Padding that stands for the memory regions of a real dump.
  size_t padding_bytes = 0;
};

std::string MakeTestDump(const TestDumpSpec& spec);

}  // namespace aerie_crash_reports

#endif  // COMPONENTS_AERIE_CRASH_REPORTS_TEST_DUMPS_H_
