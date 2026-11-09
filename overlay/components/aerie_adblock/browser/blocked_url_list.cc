// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/blocked_url_list.h"

#include "url/gurl.h"

namespace aerie_adblock {

namespace {
size_t g_max_entries = BlockedUrlList::kMaxEntries;
size_t g_max_url_length = BlockedUrlList::kMaxUrlLength;
}  // namespace

BlockedUrlList::ScopedLimitsForTesting::ScopedLimitsForTesting(
    size_t max_entries,
    size_t max_url_length) {
  g_max_entries = max_entries;
  g_max_url_length = max_url_length;
}

BlockedUrlList::ScopedLimitsForTesting::~ScopedLimitsForTesting() {
  g_max_entries = kMaxEntries;
  g_max_url_length = kMaxUrlLength;
}

BlockedUrlList::BlockedUrlList() = default;
BlockedUrlList::~BlockedUrlList() = default;

void BlockedUrlList::Add(const GURL& url) {
  if (urls_.size() >= g_max_entries) {
    return;
  }
  // GURL::spec() is ASCII (everything else is escaped), so a byte cut is a
  // character cut.
  urls_.push_back(url.spec().substr(0, g_max_url_length));
}

void BlockedUrlList::Clear() {
  urls_.clear();
  urls_.shrink_to_fit();
}

}  // namespace aerie_adblock
