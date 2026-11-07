// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/page_block_counter.h"

#include <algorithm>

#include "base/metrics/histogram_functions.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"

namespace aerie_adblock {

namespace {
constexpr int kHistogramMax = 100;  // Larger counts land in the top bucket.
}  // namespace

WEB_CONTENTS_USER_DATA_KEY_IMPL(PageBlockCounter);

PageBlockCounter::PageBlockCounter(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<PageBlockCounter>(*web_contents) {}

PageBlockCounter::~PageBlockCounter() = default;

// static
void PageBlockCounter::RecordBlocked(content::GlobalRenderFrameHostId frame_id) {
  content::RenderFrameHost* frame = content::RenderFrameHost::FromID(frame_id);
  content::WebContents* web_contents =
      frame ? content::WebContents::FromRenderFrameHost(frame) : nullptr;
  if (!web_contents) {
    return;
  }
  PageBlockCounter::CreateForWebContents(web_contents);
  PageBlockCounter::FromWebContents(web_contents)->Increment();
}

// static
int PageBlockCounter::CountFor(content::WebContents* web_contents) {
  PageBlockCounter* counter =
      web_contents ? PageBlockCounter::FromWebContents(web_contents) : nullptr;
  return counter ? counter->count_ : 0;
}

// static
void PageBlockCounter::AddObserverFor(content::WebContents* web_contents,
                                      Observer* observer) {
  PageBlockCounter::CreateForWebContents(web_contents);
  PageBlockCounter::FromWebContents(web_contents)->observers_.AddObserver(observer);
}

// static
void PageBlockCounter::RemoveObserverFor(content::WebContents* web_contents,
                                         Observer* observer) {
  if (PageBlockCounter* counter = PageBlockCounter::FromWebContents(web_contents)) {
    counter->observers_.RemoveObserver(observer);
  }
}

void PageBlockCounter::Increment() {
  ++count_;
  for (Observer& observer : observers_) {
    observer.OnPageBlockedCountChanged(web_contents(), count_);
  }
}

void PageBlockCounter::RecordAndReset() {
  if (count_ > 0) {
    base::UmaHistogramExactLinear("Aerie.Adblock.PageBlockedCount",
                                  std::min(count_, kHistogramMax),
                                  kHistogramMax + 1);
  }
  if (count_ != 0) {
    count_ = 0;
    for (Observer& observer : observers_) {
      observer.OnPageBlockedCountChanged(web_contents(), 0);
    }
  }
}

void PageBlockCounter::PrimaryPageChanged(content::Page& page) {
  RecordAndReset();
}

void PageBlockCounter::WebContentsDestroyed() {
  // Record without telling observers: the tab is going away.
  if (count_ > 0) {
    base::UmaHistogramExactLinear("Aerie.Adblock.PageBlockedCount",
                                  std::min(count_, kHistogramMax),
                                  kHistogramMax + 1);
    count_ = 0;
  }
}

}  // namespace aerie_adblock
