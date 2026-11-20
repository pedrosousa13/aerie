// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// Browser tests of the Crash reports page's handler (issue #262): list, delete one, delete
// all, open the folder, driven through its messages on a real Crashpad database in a
// temporary folder. The reports are synthetic minidumps that hold a secret; the answer
// must never carry it, nor a path.

#include "chrome/browser/ui/webui/aerie_crash_reports/aerie_crash_reports_handler.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/aerie_crash_reports/crash_report_store.h"
#include "components/aerie_crash_reports/test_dumps.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_web_ui.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/crashpad/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/crashpad/util/misc/uuid.h"

namespace aerie_crash_reports {

class AerieCrashReportsBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    database_dir_ = temp_dir_.GetPath().AppendASCII("Crash Reports");
    database_ = crashpad::CrashReportDatabase::Initialize(database_dir_);
    ASSERT_TRUE(database_);
    web_ui_ = std::make_unique<content::TestWebUI>();
    web_ui_->set_web_contents(
        browser()->tab_strip_model()->GetActiveWebContents());
    handler_ = std::make_unique<AerieCrashReportsHandler>(
        database_dir_,
        base::BindRepeating(&AerieCrashReportsBrowserTest::OpenFolder,
                            base::Unretained(this)));
    handler_->set_web_ui(web_ui_.get());
    handler_->RegisterMessages();
  }

  void TearDownOnMainThread() override {
    handler_->set_web_ui(nullptr);
    handler_.reset();
    web_ui_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  bool OpenFolder(const base::FilePath& folder) {
    opened_.push_back(folder);
    return open_succeeds_;
  }

  // Writes one report the way the crash handler does; returns its id.
  std::string AddReport(const TestDumpSpec& spec, int age_seconds = 0) {
    std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
    EXPECT_EQ(database_->PrepareNewCrashReport(&report),
              crashpad::CrashReportDatabase::kNoError);
    const std::string bytes = MakeTestDump(spec);
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

  // Sends `name` with a callback id and what follows, and waits for the page's answer.
  // `rejected` says whether the page got a rejection (its text is in `*text`).
  base::Value Send(const std::string& name,
                   std::vector<base::Value> args = {},
                   bool* rejected = nullptr,
                   std::string* text = nullptr) {
    const std::string id = "cb" + std::to_string(++calls_);
    base::ListValue list;
    list.Append(id);
    for (base::Value& arg : args) {
      list.Append(std::move(arg));
    }
    web_ui_->HandleReceivedMessage(name, list);
    base::Value answer;
    EXPECT_TRUE(base::test::RunUntil([&] {
      for (const auto& call : web_ui_->call_data()) {
        const base::Value* arg1 = call->arg1();
        if (call->function_name() == "cr.webUIResponse" && arg1 &&
            arg1->is_string() && arg1->GetString() == id) {
          const bool ok = call->arg2() && call->arg2()->is_bool() &&
                          call->arg2()->GetBool();
          if (rejected) {
            *rejected = !ok;
          }
          if (call->arg3()) {
            answer = call->arg3()->Clone();
            if (text && call->arg3()->is_string()) {
              *text = call->arg3()->GetString();
            }
          }
          return true;
        }
      }
      return false;
    }));
    return answer;
  }

  static std::vector<std::string> Ids(const base::Value& answer) {
    std::vector<std::string> ids;
    const base::ListValue* reports = answer.GetDict().FindList("reports");
    if (reports) {
      for (const base::Value& r : *reports) {
        ids.push_back(*r.GetDict().FindString("id"));
      }
    }
    return ids;
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath database_dir_;
  std::unique_ptr<crashpad::CrashReportDatabase> database_;
  std::unique_ptr<content::TestWebUI> web_ui_;
  std::unique_ptr<AerieCrashReportsHandler> handler_;
  std::vector<base::FilePath> opened_;
  bool open_succeeds_ = true;
  int calls_ = 0;
};

IN_PROC_BROWSER_TEST_F(AerieCrashReportsBrowserTest, ListsTimeProcessTypeAndVersion) {
  TestDumpSpec spec;
  spec.process_type = "renderer";
  spec.version = "0.21.1";
  const std::string id = AddReport(spec);

  base::Value answer = Send("aerieCrashReports.list");
  ASSERT_TRUE(answer.is_dict());
  EXPECT_THAT(answer.GetDict().FindBool("ok"), testing::Optional(true));
  EXPECT_THAT(answer.GetDict().FindInt("limit"), testing::Optional(20));
  const base::ListValue* reports = answer.GetDict().FindList("reports");
  ASSERT_TRUE(reports);
  ASSERT_EQ(reports->size(), 1u);
  const base::DictValue& report = (*reports)[0].GetDict();
  EXPECT_EQ(*report.FindString("id"), id);
  EXPECT_EQ(*report.FindString("processType"), "renderer");
  EXPECT_EQ(*report.FindString("version"), "0.21.1");
  const double time = report.FindDouble("time").value_or(0);
  EXPECT_LT(
      (base::Time::Now() - base::Time::FromMillisecondsSinceUnixEpoch(time))
          .magnitude(),
      base::Minutes(1));
}

IN_PROC_BROWSER_TEST_F(AerieCrashReportsBrowserTest, ANewestFirstListWithNoMoreThanTheLimit) {
  std::vector<std::string> ids;  // oldest first
  for (int i = 0; i < 22; ++i) {
    ids.push_back(AddReport(TestDumpSpec(), (22 - i) * 60));
  }
  base::Value answer = Send("aerieCrashReports.list");
  std::vector<std::string> listed = Ids(answer);
  ASSERT_EQ(listed.size(), 20u);
  EXPECT_EQ(listed.front(), ids[21]);
  EXPECT_EQ(listed.back(), ids[2]);
  EXPECT_FALSE(base::PathExists(DumpPath(ids[0])));
  EXPECT_FALSE(base::PathExists(DumpPath(ids[1])));
}

IN_PROC_BROWSER_TEST_F(AerieCrashReportsBrowserTest, TheAnswerHoldsNothingFromTheDumpAndNoPath) {
  AddReport(TestDumpSpec());
  base::Value answer = Send("aerieCrashReports.list");
  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(answer, &json));
  EXPECT_EQ(json.find("SECRET"), std::string::npos) << json;
  EXPECT_EQ(json.find(temp_dir_.GetPath().AsUTF8Unsafe()), std::string::npos)
      << json;
  EXPECT_EQ(json.find("Crash Reports"), std::string::npos) << json;
  EXPECT_EQ(json.find(".dmp"), std::string::npos) << json;
  // Only these four fields, for each report.
  const base::DictValue& report =
      (*answer.GetDict().FindList("reports"))[0].GetDict();
  EXPECT_EQ(report.size(), 4u);
  EXPECT_EQ(answer.GetDict().size(), 3u);
}

IN_PROC_BROWSER_TEST_F(AerieCrashReportsBrowserTest, DeleteRemovesOneReport) {
  const std::string keep = AddReport(TestDumpSpec(), 60);
  const std::string drop = AddReport(TestDumpSpec(), 0);
  std::vector<base::Value> args;
  args.emplace_back(drop);
  base::Value answer = Send("aerieCrashReports.delete", std::move(args));
  EXPECT_THAT(answer.GetDict().FindBool("ok"), testing::Optional(true));
  EXPECT_THAT(Ids(answer), testing::ElementsAre(keep));
  EXPECT_FALSE(base::PathExists(DumpPath(drop)));
  EXPECT_TRUE(base::PathExists(DumpPath(keep)));
}

IN_PROC_BROWSER_TEST_F(AerieCrashReportsBrowserTest, DeleteOfAnIdThatIsNotAReportChangesNothing) {
  const std::string id = AddReport(TestDumpSpec());
  for (const char* bad : {"", "../settings.dat", "../../../etc/passwd",
                          "00000000-0000-0000-0000-000000000000", "*"}) {
    std::vector<base::Value> args;
    args.emplace_back(bad);
    base::Value answer = Send("aerieCrashReports.delete", std::move(args));
    EXPECT_THAT(answer.GetDict().FindBool("ok"), testing::Optional(false))
        << bad;
    EXPECT_THAT(Ids(answer), testing::ElementsAre(id)) << bad;
  }
  EXPECT_TRUE(base::PathExists(DumpPath(id)));
}

IN_PROC_BROWSER_TEST_F(AerieCrashReportsBrowserTest, DeleteAllRemovesEveryReport) {
  std::vector<std::string> ids;
  for (int i = 0; i < 4; ++i) {
    ids.push_back(AddReport(TestDumpSpec(), i * 10));
  }
  base::Value answer = Send("aerieCrashReports.deleteAll");
  EXPECT_THAT(answer.GetDict().FindBool("ok"), testing::Optional(true));
  EXPECT_TRUE(Ids(answer).empty());
  for (const std::string& id : ids) {
    EXPECT_FALSE(base::PathExists(DumpPath(id)));
  }
}

IN_PROC_BROWSER_TEST_F(AerieCrashReportsBrowserTest, OpenFolderAsksTheFileManagerForTheDatabaseFolder) {
  AddReport(TestDumpSpec());
  base::Value answer = Send("aerieCrashReports.openFolder");
  EXPECT_THAT(answer.GetDict().FindBool("ok"), testing::Optional(true));
  ASSERT_EQ(opened_.size(), 1u);
  EXPECT_EQ(opened_[0], database_dir_);
  EXPECT_EQ(Ids(answer).size(), 1u);
}

IN_PROC_BROWSER_TEST_F(AerieCrashReportsBrowserTest, OpenFolderSaysSoWhenItFails) {
  open_succeeds_ = false;
  base::Value answer = Send("aerieCrashReports.openFolder");
  EXPECT_THAT(answer.GetDict().FindBool("ok"), testing::Optional(false));
}

IN_PROC_BROWSER_TEST_F(AerieCrashReportsBrowserTest, TheMessagesTakeNoPathAndAWrongArgumentIsRejected) {
  AddReport(TestDumpSpec());
  bool rejected = false;
  std::string text;

  std::vector<base::Value> two;
  two.emplace_back("a");
  two.emplace_back("b");
  Send("aerieCrashReports.delete", std::move(two), &rejected, &text);
  EXPECT_TRUE(rejected);
  EXPECT_EQ(text, "bad_argument");

  std::vector<base::Value> number;
  number.emplace_back(7);
  Send("aerieCrashReports.delete", std::move(number), &rejected, &text);
  EXPECT_TRUE(rejected);
  EXPECT_EQ(text, "bad_argument");

  Send("aerieCrashReports.delete", {}, &rejected, &text);
  EXPECT_TRUE(rejected);

  std::vector<base::Value> extra;
  extra.emplace_back("/etc");
  Send("aerieCrashReports.openFolder", std::move(extra), &rejected, &text);
  EXPECT_TRUE(rejected);
  EXPECT_EQ(text, "bad_argument");
  EXPECT_TRUE(opened_.empty());

  // Nothing was deleted by any of that.
  EXPECT_EQ(Ids(Send("aerieCrashReports.list")).size(), 1u);
}

}  // namespace aerie_crash_reports
