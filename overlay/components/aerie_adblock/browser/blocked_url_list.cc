// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/blocked_url_list.h"

#include "url/gurl.h"

namespace aerie_adblock {

BlockedUrlList::BlockedUrlList() = default;
BlockedUrlList::~BlockedUrlList() = default;

void BlockedUrlList::Add(const GURL& url) {
  if (urls_.size() >= kMaxEntries) {
    return;
  }
  // GURL::spec() is ASCII (everything else is escaped), so a byte cut is a
  // character cut.
  urls_.push_back(url.spec().substr(0, kMaxUrlLength));
}

void BlockedUrlList::Clear() {
  urls_.clear();
  urls_.shrink_to_fit();
}

}  // namespace aerie_adblock
