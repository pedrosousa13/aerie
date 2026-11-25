// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// The download bubble's summary for a blocked extension download (issue #94).
//
// Helium stops an extension download from the dummy origin (the origin it
// uses while its services are off) with FILE_BLOCKED, and its patch gave the
// bubble a summary that named Helium services, with a button to a settings page
// that Aerie removed. patches/services/0009 swaps in an Aerie summary and no
// button. The Bench cannot reach this branch (a plain 404 download does not),
// so this test is the guard: a download from the dummy host that fails with
// FILE_BLOCKED must get the Aerie summary, no wording that names Helium and
// no link; any other host keeps Chromium's summary.

#include <string>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/ui/download/download_bubble_security_view_info.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/download/public/common/download_interrupt_reasons.h"
#include "components/download/public/common/mock_download_item.h"
#include "components/helium_services/helium_services_helpers.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/test/browser_test.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRefOfCopy;

class AerieDownloadBubbleSummaryBrowserTest : public InProcessBrowserTest {
 protected:
  // The summary the bubble shows for a download from `url` that was blocked.
  std::u16string SummaryOfBlockedDownloadFrom(const GURL& url,
                                              DownloadBubbleSecurityViewInfo& info) {
    NiceMock<download::MockDownloadItem> item;
    ON_CALL(item, GetGuid()).WillByDefault(ReturnRefOfCopy(std::string("id")));
    ON_CALL(item, GetURL()).WillByDefault(ReturnRefOfCopy(url));
    ON_CALL(item, GetState())
        .WillByDefault(Return(download::DownloadItem::INTERRUPTED));
    ON_CALL(item, GetLastReason())
        .WillByDefault(
            Return(download::DOWNLOAD_INTERRUPT_REASON_FILE_BLOCKED));
    content::DownloadItemUtils::AttachInfoForTesting(
        &item, browser()->GetProfile(), nullptr);
    DownloadItemModel model(&item);
    info.InitializeForDownload(model);
    std::u16string summary = info.warning_summary();
    info.Reset();  // stop observing `item` before it goes away
    return summary;
  }
};

IN_PROC_BROWSER_TEST_F(AerieDownloadBubbleSummaryBrowserTest,
                       ABlockedExtensionDownloadGetsTheAerieSummaryAndNoLink) {
  DownloadBubbleSecurityViewInfo info;
  const std::u16string summary =
      SummaryOfBlockedDownloadFrom(helium::GetDummyURL(), info);
  EXPECT_EQ(summary,
            l10n_util::GetStringUTF16(
                IDS_AERIE_DOWNLOAD_BUBBLE_EXTENSION_DOWNLOADS_OFF));
  EXPECT_FALSE(base::ToLowerASCII(summary).find(u"helium") !=
               std::u16string::npos)
      << base::UTF16ToUTF8(summary);
  EXPECT_FALSE(info.has_primary_button());
  EXPECT_FALSE(info.has_secondary_button());
  EXPECT_FALSE(info.learn_more_link().has_value());
}

IN_PROC_BROWSER_TEST_F(AerieDownloadBubbleSummaryBrowserTest,
                       ABlockedDownloadFromAnotherHostKeepsChromiumsSummary) {
  DownloadBubbleSecurityViewInfo info;
  const std::u16string summary = SummaryOfBlockedDownloadFrom(
      GURL("https://example.com/file.bin"), info);
  EXPECT_EQ(summary,
            l10n_util::GetStringUTF16(
                IDS_DOWNLOAD_BUBBLE_INTERRUPTED_SUBPAGE_SUMMARY_BLOCKED_ORGANIZATION));
}
