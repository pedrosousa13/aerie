// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_crash_reports/dump_facts.h"

#include <stdint.h>

#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/rand_util.h"
#include "components/aerie_crash_reports/test_dumps.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace aerie_crash_reports {

class DumpFactsTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }

  base::FilePath Write(const std::string& bytes) {
    base::FilePath path = dir_.GetPath().AppendASCII("report.dmp");
    EXPECT_TRUE(base::WriteFile(path, bytes));
    return path;
  }

  base::ScopedTempDir dir_;
};

TEST_F(DumpFactsTest, ReadsTheProcessTypeAndTheVersion) {
  TestDumpSpec spec;
  spec.process_type = "renderer";
  spec.version = "0.21.1";
  auto facts = ReadDumpFacts(Write(MakeTestDump(spec)));
  ASSERT_TRUE(facts.has_value());
  EXPECT_EQ(facts->process_type, "renderer");
  EXPECT_EQ(facts->version, "0.21.1");
}

TEST_F(DumpFactsTest, ReadsABrowserDumpThatIsMostlyMemory) {
  TestDumpSpec spec;
  spec.padding_bytes = 5 * 1024 * 1024;
  auto facts = ReadDumpFacts(Write(MakeTestDump(spec)));
  ASSERT_TRUE(facts.has_value());
  EXPECT_EQ(facts->process_type, "browser");
  EXPECT_EQ(facts->version, "0.1.0");
}

TEST_F(DumpFactsTest, ReturnsNothingFromTheRestOfTheDump) {
  // The dump holds the secret in a stream and in two annotations. Whatever the reader
  // returns, the secret is in none of it.
  auto facts = ReadDumpFacts(Write(MakeTestDump(TestDumpSpec())));
  ASSERT_TRUE(facts.has_value());
  EXPECT_EQ(facts->process_type.find("SECRET"), std::string::npos);
  EXPECT_EQ(facts->version.find("SECRET"), std::string::npos);
}

TEST_F(DumpFactsTest, AValueThatIsNotAPlainTokenIsDropped) {
  for (const char* hostile :
       {"<img src=x onerror=alert(1)>", "../../etc/passwd", "a b", "with\nnewline",
        "https://example.com/secret?q=page", "1.0+x", "a+b"}) {
    TestDumpSpec spec;
    spec.process_type = hostile;
    spec.version = hostile;
    auto facts = ReadDumpFacts(Write(MakeTestDump(spec)));
    ASSERT_TRUE(facts.has_value()) << hostile;
    EXPECT_EQ(facts->process_type, "") << hostile;
    EXPECT_EQ(facts->version, "") << hostile;
  }
}

TEST_F(DumpFactsTest, ALongValueIsDropped) {
  TestDumpSpec spec;
  spec.process_type = std::string(200, 'a');
  spec.version = std::string(200, '1');
  auto facts = ReadDumpFacts(Write(MakeTestDump(spec)));
  ASSERT_TRUE(facts.has_value());
  EXPECT_EQ(facts->process_type, "");
  EXPECT_EQ(facts->version, "");
}

TEST_F(DumpFactsTest, ADumpWithoutThoseAnnotationsHasEmptyFacts) {
  TestDumpSpec spec;
  spec.process_type = "";
  spec.version = "";
  auto facts = ReadDumpFacts(Write(MakeTestDump(spec)));
  ASSERT_TRUE(facts.has_value());
  EXPECT_EQ(facts->process_type, "");
  EXPECT_EQ(facts->version, "");
}

TEST_F(DumpFactsTest, NotAMinidumpGivesNothing) {
  EXPECT_FALSE(ReadDumpFacts(Write("")).has_value());
  EXPECT_FALSE(ReadDumpFacts(Write("MDM")).has_value());
  EXPECT_FALSE(ReadDumpFacts(Write(std::string(4096, 'x'))).has_value());
  EXPECT_FALSE(
      ReadDumpFacts(dir_.GetPath().AppendASCII("missing.dmp")).has_value());
}

TEST_F(DumpFactsTest, ATruncatedDumpNeverReadsPastTheEnd) {
  const std::string whole = MakeTestDump(TestDumpSpec());
  for (size_t length = 0; length < whole.size(); ++length) {
    // Any answer is fine, a crash or a hang is not. The sanitizer build checks the rest.
    auto facts = ReadDumpFacts(Write(whole.substr(0, length)));
    if (facts) {
      EXPECT_LE(facts->process_type.size(), 32u);
      EXPECT_LE(facts->version.size(), 32u);
    }
  }
}

TEST_F(DumpFactsTest, ACorruptedDumpNeverReadsPastTheEnd) {
  const std::string whole = MakeTestDump(TestDumpSpec());
  for (int round = 0; round < 2000; ++round) {
    std::string bytes = whole;
    // Flip a few bytes after the header: the rvas, sizes and counts are in there.
    for (int i = 0; i < 4; ++i) {
      bytes[base::RandGenerator(bytes.size())] =
          static_cast<char>(base::RandGenerator(256));
    }
    auto facts = ReadDumpFacts(Write(bytes));
    if (facts) {
      EXPECT_LE(facts->process_type.size(), 32u);
      EXPECT_LE(facts->version.size(), 32u);
    }
  }
}

TEST_F(DumpFactsTest, AHugeCountOfStreamsOrAnnotationsIsRefused) {
  std::string bytes = MakeTestDump(TestDumpSpec());
  // The number of streams, at offset 8.
  bytes[8] = '\xff';
  bytes[9] = '\xff';
  bytes[10] = '\xff';
  bytes[11] = '\x7f';
  EXPECT_FALSE(ReadDumpFacts(Write(bytes)).has_value());
}

}  // namespace aerie_crash_reports
