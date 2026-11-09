// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_BLOCKED_URL_LIST_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_BLOCKED_URL_LIST_H_

#include <stddef.h>

#include <string>
#include <vector>

class GURL;

namespace aerie_adblock {

// The URLs the Filter Engine blocked on the page of one tab, for the list in
// the Aerie Guard panel (issue #110).
//
// Bounded: it keeps the first kMaxEntries URLs and drops later ones (the
// count in PageBlockCounter still counts them all), and each URL is cut to
// kMaxUrlLength characters. Memory only: nothing here is logged or written to
// disk. A URL is the spec of the request, which is ASCII, so a cut never splits
// a character.
class BlockedUrlList {
 public:
  static constexpr size_t kMaxEntries = 200;
  static constexpr size_t kMaxUrlLength = 256;

  BlockedUrlList();
  ~BlockedUrlList();

  // Adds |url| unless the list is full.
  void Add(const GURL& url);
  void Clear();

  const std::vector<std::string>& urls() const { return urls_; }

 private:
  std::vector<std::string> urls_;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_BLOCKED_URL_LIST_H_
