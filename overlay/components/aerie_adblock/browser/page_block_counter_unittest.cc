// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/page_block_counter.h"

#include "base/test/metrics/histogram_tester.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace aerie_adblock {
namespace {

constexpr char kHistogram[] = "Aerie.Adblock.PageBlockedCount";

class PageBlockCounterTest : public content::RenderViewHostTestHarness,
                             public PageBlockCounter::Observer {
 protected:
  void OnPageBlockedCountChanged(content::WebContents*, int count) override {
    seen_.push_back(count);
  }
  content::GlobalRenderFrameHostId MainFrame() {
    return main_rfh()->GetGlobalId();
  }
  std::vector<int> seen_;
};

TEST_F(PageBlockCounterTest, CountsBlockedRequestsOfTheTabsFrames) {
  NavigateAndCommit(GURL("https://news.test/"));
  EXPECT_EQ(0, PageBlockCounter::CountFor(web_contents()));
  PageBlockCounter::AddObserverFor(web_contents(), this);
  for (int i = 0; i < 3; ++i) {
    PageBlockCounter::RecordBlocked(MainFrame());
  }
  EXPECT_EQ(3, PageBlockCounter::CountFor(web_contents()));
  EXPECT_EQ((std::vector<int>{1, 2, 3}), seen_);
  PageBlockCounter::RemoveObserverFor(web_contents(), this);
}

TEST_F(PageBlockCounterTest, AGoneFrameOrNoFrameIsIgnored) {
  NavigateAndCommit(GURL("https://news.test/"));
  PageBlockCounter::RecordBlocked(content::GlobalRenderFrameHostId());
  PageBlockCounter::RecordBlocked({12345, 6789});
  EXPECT_EQ(0, PageBlockCounter::CountFor(web_contents()));
}

TEST_F(PageBlockCounterTest, ANewDocumentStartsAtZeroAndRecordsTheOldCount) {
  base::HistogramTester histograms;
  NavigateAndCommit(GURL("https://news.test/"));
  PageBlockCounter::RecordBlocked(MainFrame());
  PageBlockCounter::RecordBlocked(MainFrame());
  NavigateAndCommit(GURL("https://other.test/"));
  EXPECT_EQ(0, PageBlockCounter::CountFor(web_contents()));
  histograms.ExpectUniqueSample(kHistogram, 2, 1);
}

TEST_F(PageBlockCounterTest, ASameDocumentNavigationKeepsTheCount) {
  NavigateAndCommit(GURL("https://news.test/"));
  PageBlockCounter::RecordBlocked(MainFrame());
  NavigateAndCommit(GURL("https://news.test/#section"));
  EXPECT_EQ(1, PageBlockCounter::CountFor(web_contents()));
}

TEST_F(PageBlockCounterTest, APageWithNothingBlockedRecordsNothing) {
  base::HistogramTester histograms;
  NavigateAndCommit(GURL("https://news.test/"));
  NavigateAndCommit(GURL("https://other.test/"));
  histograms.ExpectTotalCount(kHistogram, 0);
}

TEST_F(PageBlockCounterTest, ClosingTheTabRecordsTheCount) {
  base::HistogramTester histograms;
  NavigateAndCommit(GURL("https://news.test/"));
  PageBlockCounter::RecordBlocked(MainFrame());
  DeleteContents();
  histograms.ExpectUniqueSample(kHistogram, 1, 1);
}

}  // namespace
}  // namespace aerie_adblock
