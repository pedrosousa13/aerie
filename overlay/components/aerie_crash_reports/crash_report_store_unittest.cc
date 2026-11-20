// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_crash_reports/crash_report_store.h"

#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "components/aerie_crash_reports/test_dumps.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/crashpad/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/crashpad/util/misc/uuid.h"

namespace aerie_crash_reports {

class CrashReportStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    // The same database the crash handler writes: Crashpad's own.
    database_ = crashpad::CrashReportDatabase::Initialize(dir_.GetPath());
    ASSERT_TRUE(database_);
  }

  // Writes one report the way the crash handler does. `age_seconds` sets the age of the
  // dump file, which orders reports that Crashpad stamps in the same second.
  std::string AddReport(const TestDumpSpec& spec, int age_seconds = 0) {
    std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
    EXPECT_EQ(database_->PrepareNewCrashReport(&report),
              crashpad::CrashReportDatabase::kNoError);
    const std::string bytes = MakeTestDump(spec);
    EXPECT_TRUE(report->Writer()->Write(bytes.data(), bytes.size()));
    crashpad::UUID uuid;
    EXPECT_EQ(database_->FinishedWritingCrashReport(std::move(report), &uuid),
              crashpad::CrashReportDatabase::kNoError);
    const std::string id = uuid.ToString();
    const base::Time when = base::Time::Now() - base::Seconds(age_seconds);
    EXPECT_TRUE(base::TouchFile(DumpPath(id), when, when));
    return id;
  }

  base::FilePath DumpPath(const std::string& id) {
    return dir_.GetPath().AppendASCII("pending").AppendASCII(id + ".dmp");
  }

  static int Mode(const base::FilePath& path) {
    int mode = 0;
    EXPECT_TRUE(base::GetPosixFilePermissions(path, &mode));
    return mode;
  }

  base::ScopedTempDir dir_;
  std::unique_ptr<crashpad::CrashReportDatabase> database_;
};

TEST_F(CrashReportStoreTest, TheLimitIsTwenty) {
  EXPECT_EQ(kMaxReports, 20u);
}

TEST_F(CrashReportStoreTest, ADatabaseThatIsNotThereIsEmpty) {
  base::ScopedTempDir other;
  ASSERT_TRUE(other.CreateUniqueTempDir());
  CrashReportStore store(other.GetPath().AppendASCII("no-such-dir"));
  EXPECT_TRUE(store.List().empty());
  EXPECT_EQ(store.DeleteAll(), 0u);
  EXPECT_EQ(store.Prune(), 0u);
  EXPECT_FALSE(base::PathExists(other.GetPath().AppendASCII("no-such-dir")))
      << "listing must not create a database";
}

TEST_F(CrashReportStoreTest, ListsTimeProcessTypeAndVersion) {
  TestDumpSpec spec;
  spec.process_type = "gpu-process";
  spec.version = "0.21.1";
  const std::string id = AddReport(spec);

  CrashReportStore store(dir_.GetPath());
  std::vector<ReportSummary> reports = store.List();
  ASSERT_EQ(reports.size(), 1u);
  EXPECT_EQ(reports[0].id, id);
  EXPECT_EQ(reports[0].process_type, "gpu-process");
  EXPECT_EQ(reports[0].version, "0.21.1");
  EXPECT_LE((base::Time::Now() - reports[0].time).magnitude(), base::Minutes(1));
}

TEST_F(CrashReportStoreTest, ListsTheNewestFirst) {
  const std::string old_id = AddReport(TestDumpSpec(), 300);
  const std::string new_id = AddReport(TestDumpSpec(), 0);
  const std::string mid_id = AddReport(TestDumpSpec(), 100);
  std::vector<ReportSummary> reports = CrashReportStore(dir_.GetPath()).List();
  ASSERT_EQ(reports.size(), 3u);
  EXPECT_EQ(reports[0].id, new_id);
  EXPECT_EQ(reports[1].id, mid_id);
  EXPECT_EQ(reports[2].id, old_id);
}

TEST_F(CrashReportStoreTest, ASummaryHoldsNothingFromTheDump) {
  AddReport(TestDumpSpec());
  for (const ReportSummary& r : CrashReportStore(dir_.GetPath()).List()) {
    for (const std::string& field : {r.id, r.process_type, r.version}) {
      EXPECT_EQ(field.find("SECRET"), std::string::npos);
    }
  }
}

TEST_F(CrashReportStoreTest, DeleteRemovesOneReportAndItsFile) {
  const std::string keep = AddReport(TestDumpSpec(), 10);
  const std::string drop = AddReport(TestDumpSpec(), 0);
  CrashReportStore store(dir_.GetPath());
  EXPECT_TRUE(store.Delete(drop));
  EXPECT_FALSE(base::PathExists(DumpPath(drop)));
  std::vector<ReportSummary> left = store.List();
  ASSERT_EQ(left.size(), 1u);
  EXPECT_EQ(left[0].id, keep);
  EXPECT_TRUE(base::PathExists(DumpPath(keep)));
}

TEST_F(CrashReportStoreTest, DeleteRefusesAnIdThatIsNotAReportOfTheDatabase) {
  const std::string id = AddReport(TestDumpSpec());
  const base::FilePath canary = dir_.GetPath().AppendASCII("settings.dat");
  ASSERT_TRUE(base::WriteFile(canary, "kept"));
  CrashReportStore store(dir_.GetPath());
  for (const char* bad : {"", "x", "../settings.dat", "../../etc/passwd",
                          "00000000-0000-0000-0000-000000000000",
                          "pending/", "*"}) {
    EXPECT_FALSE(store.Delete(bad)) << bad;
  }
  EXPECT_TRUE(base::PathExists(DumpPath(id)));
  EXPECT_TRUE(base::PathExists(canary));
  EXPECT_EQ(store.List().size(), 1u);
}

TEST_F(CrashReportStoreTest, DeleteAllRemovesEveryReport) {
  std::vector<std::string> ids;
  for (int i = 0; i < 5; ++i) {
    ids.push_back(AddReport(TestDumpSpec(), i));
  }
  CrashReportStore store(dir_.GetPath());
  EXPECT_EQ(store.DeleteAll(), 5u);
  EXPECT_TRUE(store.List().empty());
  for (const std::string& id : ids) {
    EXPECT_FALSE(base::PathExists(DumpPath(id)));
  }
}

TEST_F(CrashReportStoreTest, PruneKeepsTheTwentyNewestAndDeletesTheOldest) {
  std::vector<std::string> ids;  // ids[0] is the oldest
  for (int i = 0; i < 23; ++i) {
    ids.push_back(AddReport(TestDumpSpec(), (23 - i) * 60));
  }
  CrashReportStore store(dir_.GetPath());
  EXPECT_EQ(store.Prune(), 3u);
  std::vector<ReportSummary> left = store.List();
  ASSERT_EQ(left.size(), 20u);
  for (int i = 0; i < 3; ++i) {
    EXPECT_FALSE(base::PathExists(DumpPath(ids[i]))) << "report " << i;
  }
  for (int i = 3; i < 23; ++i) {
    EXPECT_TRUE(base::PathExists(DumpPath(ids[i]))) << "report " << i;
  }
  EXPECT_EQ(left.front().id, ids[22]);
  EXPECT_EQ(left.back().id, ids[3]);
}

TEST_F(CrashReportStoreTest, PruneLeavesAShortListAlone) {
  for (int i = 0; i < 20; ++i) {
    AddReport(TestDumpSpec(), i);
  }
  CrashReportStore store(dir_.GetPath());
  EXPECT_EQ(store.Prune(), 0u);
  EXPECT_EQ(store.List().size(), 20u);
}

TEST_F(CrashReportStoreTest, PruneTakesALimitOfItsOwn) {
  for (int i = 0; i < 6; ++i) {
    AddReport(TestDumpSpec(), (6 - i) * 60);
  }
  CrashReportStore store(dir_.GetPath());
  EXPECT_EQ(store.Prune(2), 4u);
  EXPECT_EQ(store.List().size(), 2u);
  EXPECT_EQ(store.Prune(0), 2u);
  EXPECT_TRUE(store.List().empty());
}

TEST_F(CrashReportStoreTest, TheHandlerWritesDumpsReadableByTheUserOnly) {
  const std::string id = AddReport(TestDumpSpec());
  EXPECT_EQ(Mode(DumpPath(id)) & 0777, 0600);
  EXPECT_EQ(Mode(dir_.GetPath()) & 0777, 0700);
}

TEST_F(CrashReportStoreTest, ListingTightensADumpSomethingElseLoosened) {
  const std::string id = AddReport(TestDumpSpec());
  ASSERT_TRUE(base::SetPosixFilePermissions(DumpPath(id), 0644));
  ASSERT_TRUE(base::SetPosixFilePermissions(
      dir_.GetPath().AppendASCII("pending"), 0755));
  CrashReportStore(dir_.GetPath()).List();
  EXPECT_EQ(Mode(DumpPath(id)) & 0777, 0600);
  EXPECT_EQ(Mode(dir_.GetPath().AppendASCII("pending")) & 0777, 0700);
}

TEST_F(CrashReportStoreTest, PruneRepairsLooseModesToo) {
  const std::string id = AddReport(TestDumpSpec());
  ASSERT_TRUE(base::SetPosixFilePermissions(DumpPath(id), 0644));
  CrashReportStore(dir_.GetPath()).Prune();
  EXPECT_EQ(Mode(DumpPath(id)) & 0777, 0600);
}

TEST_F(CrashReportStoreTest, RepairNeverFollowsALink) {
  const std::string id = AddReport(TestDumpSpec());
  base::ScopedTempDir outside;
  ASSERT_TRUE(outside.CreateUniqueTempDir());
  const base::FilePath target = outside.GetPath().AppendASCII("target.txt");
  ASSERT_TRUE(base::WriteFile(target, "x"));
  ASSERT_TRUE(base::SetPosixFilePermissions(target, 0644));
  ASSERT_TRUE(base::CreateSymbolicLink(
      target, dir_.GetPath().AppendASCII("pending").AppendASCII("link.dmp")));
  ASSERT_TRUE(base::CreateSymbolicLink(
      outside.GetPath(), dir_.GetPath().AppendASCII("attachments").AppendASCII("dirlink")));
  ASSERT_TRUE(base::SetPosixFilePermissions(outside.GetPath(), 0755));
  CrashReportStore store(dir_.GetPath());
  store.List();
  store.Prune();
  EXPECT_EQ(Mode(target) & 0777, 0644);
  EXPECT_EQ(Mode(outside.GetPath()) & 0777, 0755);
  EXPECT_EQ(Mode(DumpPath(id)) & 0777, 0600);
}

TEST_F(CrashReportStoreTest, ACrashFolderThatIsALinkIsRefused) {
  AddReport(TestDumpSpec());
  base::ScopedTempDir holder;
  ASSERT_TRUE(holder.CreateUniqueTempDir());
  const base::FilePath link = holder.GetPath().AppendASCII("Crash Reports");
  ASSERT_TRUE(base::CreateSymbolicLink(dir_.GetPath(), link));
  CrashReportStore store(link);
  EXPECT_TRUE(store.List().empty());
  EXPECT_EQ(store.Prune(0), 0u);
  EXPECT_EQ(CrashReportStore(dir_.GetPath()).List().size(), 1u);
}

TEST_F(CrashReportStoreTest, AReportThatCannotBeDeletedStaysAndIsNotCounted) {
  if (geteuid() == 0) {
    GTEST_SKIP() << "root can delete from a read-only folder";
  }
  const std::string id = AddReport(TestDumpSpec());
  const base::FilePath pending = dir_.GetPath().AppendASCII("pending");
  ASSERT_TRUE(base::SetPosixFilePermissions(pending, 0500));
  CrashReportStore store(dir_.GetPath());
  EXPECT_FALSE(store.Delete(id));
  EXPECT_EQ(store.DeleteAll(), 0u);
  EXPECT_EQ(store.Prune(0), 0u);
  // Crashpad cannot even open its lock files in a read-only folder, so List() is empty; the
  // count from the folders still shows the dump.
  EXPECT_EQ(store.CountDumps(), 1u);
  EXPECT_TRUE(base::PathExists(DumpPath(id)));
  ASSERT_TRUE(base::SetPosixFilePermissions(pending, 0700));
  EXPECT_EQ(store.List().size(), 1u);
}

}  // namespace aerie_crash_reports
