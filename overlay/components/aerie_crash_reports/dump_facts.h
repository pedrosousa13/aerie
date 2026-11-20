// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_CRASH_REPORTS_DUMP_FACTS_H_
#define COMPONENTS_AERIE_CRASH_REPORTS_DUMP_FACTS_H_

#include <optional>
#include <string>

namespace base {
class FilePath;
}

namespace aerie_crash_reports {

// The only two things Aerie reads out of a crash dump (issue #262, docs/crash-reports.md).
// A dump holds page memory, URLs, the command line and more, so this is a short list on
// purpose: the Settings page shows no other part of a dump.
struct DumpFacts {
  // "browser", "renderer", "gpu-process", ... Empty when the dump does not say.
  std::string process_type;
  // The version the crashing build reported. Empty when the dump does not say.
  std::string version;
};

// Reads `process_type` and `version` from a Crashpad minidump without loading it: the
// header, the stream directory and the annotation tables, each read at an offset and
// checked against the file size. Nothing else is read, kept or returned. A value that is
// not a short plain token (letters, digits, '.', '-', '_', '+') is dropped, so a hostile
// file cannot put markup, a path or a long text on the page.
//
// Returns nullopt when the file cannot be opened or is not a minidump.
std::optional<DumpFacts> ReadDumpFacts(const base::FilePath& dump);

}  // namespace aerie_crash_reports

#endif  // COMPONENTS_AERIE_CRASH_REPORTS_DUMP_FACTS_H_
