// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_crash_reports/crash_report_store.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "build/build_config.h"
#include "components/aerie_crash_reports/dump_facts.h"
#include "third_party/crashpad/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/crashpad/util/misc/uuid.h"

#if BUILDFLAG(IS_POSIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace aerie_crash_reports {

namespace {

using Database = crashpad::CrashReportDatabase;

// Opens the database that is there. It never makes one: listing the reports of a profile
// that has none must not leave a "Crash Reports" folder behind.
std::unique_ptr<Database> Open(const base::FilePath& dir) {
#if BUILDFLAG(IS_POSIX)
  // A crash folder that is itself a link could point anywhere: refuse it.
  struct stat st;
  if (lstat(dir.value().c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
    return nullptr;
  }
#else
  if (!base::DirectoryExists(dir)) {
    return nullptr;
  }
#endif
  return Database::InitializeWithoutCreating(dir);
}

// Every report the database knows: the ones the handler wrote ("pending") and the ones it
// has marked ("completed"). With no upload address, nothing is ever uploaded, so a
// finished one is rare, but it is still a local dump and the user can see it and delete it.
std::vector<Database::Report> AllReports(Database& database) {
  std::vector<Database::Report> all;
  std::vector<Database::Report> part;
  if (database.GetPendingReports(&part) == Database::kNoError) {
    all.insert(all.end(), part.begin(), part.end());
  }
  part.clear();
  if (database.GetCompletedReports(&part) == Database::kNoError) {
    all.insert(all.end(), part.begin(), part.end());
  }
  return all;
}

// When the crash happened: the time the handler finished the dump. Crashpad stamps whole
// seconds, so the file time is the finer clock; it is the fallback's equal in practice.
base::Time CaptureTime(const Database::Report& report) {
  base::File::Info info;
  if (base::GetFileInfo(report.file_path, &info)) {
    return info.last_modified;
  }
  return base::Time::FromTimeT(report.creation_time);
}

struct Entry {
  Database::Report report;
  base::Time time;
};

// Newest first; ties by id so that the order is the same on every call.
std::vector<Entry> Sorted(Database& database) {
  std::vector<Entry> entries;
  for (Database::Report& report : AllReports(database)) {
    const base::Time time = CaptureTime(report);
    entries.push_back({std::move(report), time});
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    if (a.time != b.time) {
      return a.time > b.time;
    }
    return a.report.uuid.ToString() < b.report.uuid.ToString();
  });
  return entries;
}

#if BUILDFLAG(IS_POSIX)
// Follows no link: opens `path` with O_NOFOLLOW and sets the mode through the descriptor,
// so a link swapped in between cannot make this touch another file. A link, a device or any
// file of another kind is skipped.
void Tighten(const base::FilePath& path, bool directory) {
  const int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK |
                    (directory ? O_DIRECTORY : 0);
  const int fd = open(path.value().c_str(), flags);
  if (fd < 0) {
    return;
  }
  struct stat st;
  if (fstat(fd, &st) == 0 &&
      (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode)) &&
      (st.st_mode & 077) != 0) {
    fchmod(fd, directory ? 0700 : 0600);
  }
  close(fd);
}
#endif

// Dumps hold page memory: only the user may read them. Crashpad creates them 0600 in 0700
// folders; this repairs anything that something else loosened. It never follows a link.
void KeepOwnerOnly(const base::FilePath& root) {
#if BUILDFLAG(IS_POSIX)
  Tighten(root, /*directory=*/true);
  base::FileEnumerator walk(
      root, /*recursive=*/true,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
          base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath path = walk.Next(); !path.empty(); path = walk.Next()) {
    const struct stat& st = walk.GetInfo().stat();
    if (S_ISDIR(st.st_mode)) {
      Tighten(path, /*directory=*/true);
    } else if (S_ISREG(st.st_mode)) {
      Tighten(path, /*directory=*/false);
    }
  }
#endif
}

}  // namespace

CrashReportStore::CrashReportStore(const base::FilePath& database_dir)
    : database_dir_(database_dir) {}

CrashReportStore::~CrashReportStore() = default;

std::vector<ReportSummary> CrashReportStore::List() {
  std::vector<ReportSummary> summaries;
  std::unique_ptr<Database> database = Open(database_dir_);
  if (!database) {
    return summaries;
  }
  KeepOwnerOnly(database_dir_);
  for (const Entry& entry : Sorted(*database)) {
    ReportSummary summary;
    summary.id = entry.report.uuid.ToString();
    summary.time = entry.time;
    if (std::optional<DumpFacts> facts = ReadDumpFacts(entry.report.file_path)) {
      summary.process_type = std::move(facts->process_type);
      summary.version = std::move(facts->version);
    }
    summaries.push_back(std::move(summary));
  }
  return summaries;
}

size_t CrashReportStore::CountDumps() const {
  size_t count = 0;
  for (const char* sub : {"pending", "completed"}) {
    base::FileEnumerator files(database_dir_.AppendASCII(sub), false,
                               base::FileEnumerator::FILES, "*.dmp");
    for (base::FilePath p = files.Next(); !p.empty(); p = files.Next()) {
      ++count;
    }
  }
  return count;
}

bool CrashReportStore::Delete(const std::string& id) {
  crashpad::UUID uuid;
  if (!uuid.InitializeFromString(id) || uuid.ToString() != id) {
    return false;
  }
  std::unique_ptr<Database> database = Open(database_dir_);
  if (!database) {
    return false;
  }
  Database::Report report;
  if (database->LookUpCrashReport(uuid, &report) != Database::kNoError) {
    return false;
  }
  return database->DeleteReport(uuid) == Database::kNoError;
}

size_t CrashReportStore::DeleteAll() {
  return Prune(0);
}

size_t CrashReportStore::Prune(size_t max_reports) {
  std::unique_ptr<Database> database = Open(database_dir_);
  if (!database) {
    return 0;
  }
  KeepOwnerOnly(database_dir_);
  std::vector<Entry> entries = Sorted(*database);
  size_t deleted = 0;
  for (size_t i = max_reports; i < entries.size(); ++i) {
    if (database->DeleteReport(entries[i].report.uuid) == Database::kNoError) {
      ++deleted;
    }
  }
  return deleted;
}

}  // namespace aerie_crash_reports
