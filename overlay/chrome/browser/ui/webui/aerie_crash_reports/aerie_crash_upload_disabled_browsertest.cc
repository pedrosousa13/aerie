// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests that no build of Aerie can upload a crash report (issue #262): there is
// no upload address, the reporting mode and the database's upload switch stay off whatever
// a caller or a preference says, a request to upload one report does nothing, and a
// pending report is not discarded at startup. The Bench's zero_traffic check does the same
// on a real crash of the shipped binary (docs/crash-reports.md).

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/raw_ptr.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/crash_upload_list/helium_crash_reporting.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/aerie_crash_reports/crash_report_store.h"
#include "components/aerie_crash_reports/test_dumps.h"
#include "components/crash/core/app/crash_reporter_client.h"
#include "components/crash/core/app/crashpad.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/crashpad/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/crashpad/client/settings.h"
#include "third_party/crashpad/crashpad/util/misc/uuid.h"

namespace aerie_crash_reports {

class AerieCrashUploadDisabledBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    database_dir_ = temp_dir_.GetPath().AppendASCII("Crash Reports");
    database_ = crashpad::CrashReportDatabase::Initialize(database_dir_);
    ASSERT_TRUE(database_);
    // Start with uploads switched ON, so that every test below sees the code turn them off
    // and not an off that was already there.
    ASSERT_TRUE(database_->GetSettings()->SetUploadsEnabled(true));
    ASSERT_TRUE(UploadsEnabled());
    // The browser's own database, for the code that reads it (crashpad.cc): a temporary
    // one, so the test never touches the real <user data>/Crash Reports.
    previous_ = crash_reporter::internal::GetCrashReportDatabase();
    crash_reporter::internal::SetCrashReportDatabaseForTesting(
        database_.get(), &database_dir_);
  }

  void TearDownOnMainThread() override {
    crash_reporter::internal::SetCrashReportDatabaseForTesting(previous_,
                                                               nullptr);
    InProcessBrowserTest::TearDownOnMainThread();
  }

  std::string AddReport(int age_seconds = 0) {
    std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
    EXPECT_EQ(database_->PrepareNewCrashReport(&report),
              crashpad::CrashReportDatabase::kNoError);
    const std::string bytes = MakeTestDump(TestDumpSpec());
    EXPECT_TRUE(report->Writer()->Write(bytes.data(), bytes.size()));
    crashpad::UUID uuid;
    EXPECT_EQ(database_->FinishedWritingCrashReport(std::move(report), &uuid),
              crashpad::CrashReportDatabase::kNoError);
    const base::Time when = base::Time::Now() - base::Seconds(age_seconds);
    EXPECT_TRUE(base::TouchFile(DumpPath(uuid.ToString()), when, when));
    return uuid.ToString();
  }

  base::FilePath DumpPath(const std::string& id) {
    return database_dir_.AppendASCII("pending").AppendASCII(id + ".dmp");
  }

  bool UploadsEnabled() {
    bool enabled = true;
    EXPECT_TRUE(database_->GetSettings()->GetUploadsEnabled(&enabled));
    return enabled;
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath database_dir_;
  std::unique_ptr<crashpad::CrashReportDatabase> database_;
  raw_ptr<crashpad::CrashReportDatabase> previous_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(AerieCrashUploadDisabledBrowserTest, ThereIsNoUploadAddress) {
  // The embedder's client is not reachable from outside the component, and
  // Linux's ChromeCrashReporterClient does not override the address. So the
  // base class's answer is the one the crash handler is started with.
  class BareClient : public crash_reporter::CrashReporterClient {};
  BareClient client;
  EXPECT_EQ(client.GetUploadUrl(), "");
}

IN_PROC_BROWSER_TEST_F(AerieCrashUploadDisabledBrowserTest, NoPreferenceTurnsOnTheReportingMode) {
  PrefService* local_state = g_browser_process->local_state();
  for (int stored : {-1, 0, 1, 2, 3, 99}) {
    local_state->SetInteger(prefs::kHeliumCrashReportingMode, stored);
    helium::InitializeCrashReportingMode();
    EXPECT_EQ(helium::GetCrashReportingMode(),
              helium::CrashReportingMode::kDisabled)
        << stored;
    EXPECT_FALSE(UploadsEnabled()) << stored;
  }
  for (helium::CrashReportingMode mode :
       {helium::CrashReportingMode::kAsk,
        helium::CrashReportingMode::kAutomatic}) {
    helium::SetCrashReportingMode(mode);
    EXPECT_EQ(helium::GetCrashReportingMode(),
              helium::CrashReportingMode::kDisabled);
    EXPECT_FALSE(UploadsEnabled());
  }
}

IN_PROC_BROWSER_TEST_F(AerieCrashUploadDisabledBrowserTest, NoCallerTurnsOnTheDatabaseUploadSwitch) {
  crash_reporter::SetUploadConsentFromUser(true);
  EXPECT_FALSE(UploadsEnabled());
  crash_reporter::SetUploadConsent(true);
  EXPECT_FALSE(UploadsEnabled());
  crash_reporter::internal::SetUploadConsentFromUserImpl(true);
  EXPECT_FALSE(UploadsEnabled());
}

IN_PROC_BROWSER_TEST_F(AerieCrashUploadDisabledBrowserTest, ARequestToUploadOneReportDoesNothing) {
  const std::string id = AddReport();
  crash_reporter::RequestSingleCrashUpload(id);
  crashpad::UUID uuid;
  ASSERT_TRUE(uuid.InitializeFromString(id));
  crashpad::CrashReportDatabase::Report report;
  ASSERT_EQ(database_->LookUpCrashReport(uuid, &report),
            crashpad::CrashReportDatabase::kNoError);
  EXPECT_FALSE(report.upload_explicitly_requested);
  EXPECT_FALSE(report.uploaded);
  EXPECT_EQ(report.upload_attempts, 0);
}

IN_PROC_BROWSER_TEST_F(AerieCrashUploadDisabledBrowserTest, AReportIsNotDiscardedAtStartup) {
  const std::string id = AddReport();
  helium::InitializeCrashReportingMode();
  base::ThreadPoolInstance::Get()->FlushForTesting();
  EXPECT_TRUE(base::PathExists(DumpPath(id)));
  EXPECT_EQ(CrashReportStore(database_dir_).List().size(), 1u);
}

IN_PROC_BROWSER_TEST_F(AerieCrashUploadDisabledBrowserTest, StartupKeepsTheNewestTwenty) {
  std::vector<std::string> ids;  // oldest first
  for (int i = 0; i < 23; ++i) {
    ids.push_back(AddReport((23 - i) * 60));
  }
  helium::InitializeCrashReportingMode();
  base::ThreadPoolInstance::Get()->FlushForTesting();
  EXPECT_EQ(CrashReportStore(database_dir_).List().size(), 20u);
  for (int i = 0; i < 3; ++i) {
    EXPECT_FALSE(base::PathExists(DumpPath(ids[i]))) << i;
  }
  EXPECT_TRUE(base::PathExists(DumpPath(ids[3])));
}

}  // namespace aerie_crash_reports
