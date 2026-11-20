// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_CRASH_REPORTS_CRASH_REPORT_STORE_H_
#define COMPONENTS_AERIE_CRASH_REPORTS_CRASH_REPORT_STORE_H_

#include <stddef.h>

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/time/time.h"

namespace aerie_crash_reports {

// The most reports Aerie keeps. The oldest beyond this are deleted (issue #262).
inline constexpr size_t kMaxReports = 20;

// What the Settings page may know about one report. There is no field for the contents
// of the dump and no path: the page names a report by `id` only.
struct ReportSummary {
  // The report's UUID, as Crashpad writes it ("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx").
  std::string id;
  base::Time time;
  std::string process_type;
  std::string version;
};

// The local crash reports in one Crashpad database directory (`<user data>/Crash Reports`).
// Every report here was written by the crash handler on this machine and stays here: this
// class has no upload and the build has no upload address (patches/crash/0001, 0002).
//
// All calls do file I/O: call them off the UI thread.
class CrashReportStore {
 public:
  explicit CrashReportStore(const base::FilePath& database_dir);
  ~CrashReportStore();

  // Every report, newest first. It also makes sure that no dump or database file is
  // readable by anyone but the user (mode 0600, directories 0700); Crashpad already
  // writes them so, and this repairs a file that something else loosened. A missing or
  // unreadable database gives an empty list.
  std::vector<ReportSummary> List();

  // Deletes one report. False when `id` is not a report UUID of this database, or the
  // report cannot be deleted. Other reports are never touched.
  bool Delete(const std::string& id);

  // Deletes every report. Returns how many were deleted.
  size_t DeleteAll();

  // Deletes the oldest reports so that at most `max_reports` are left. Returns how many
  // were deleted.
  size_t Prune(size_t max_reports = kMaxReports);

 private:
  const base::FilePath database_dir_;
};

}  // namespace aerie_crash_reports

#endif  // COMPONENTS_AERIE_CRASH_REPORTS_CRASH_REPORT_STORE_H_
