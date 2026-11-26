// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_LIST_FAILURE_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_LIST_FAILURE_H_

#include <string>

namespace aerie_adblock {

// Why a filter list was refused. Mirrors aerie_adblock::ListStatus of the Rust
// bridge (adblock_engine.cc maps one to the other), plus the two failures that
// happen before the Rust side sees any bytes.
enum class ListFailure {
  kNone,
  kTooLarge,
  kTimeout,
  kNotText,
  kLooksLikeHtml,
  kNoRules,
  kUnreadable,   // The cached copy could not be read from disk.
  kDownload,     // The download failed (network error, size cap, bad status).
  kInsecureUrl,  // Not https (or a redirect left https).
  kNonPublicUrl,  // https, but the host is on the user's own network or computer.
};

// A short sentence for the settings page. Fixed text, never list content.
inline std::string ListFailureMessage(ListFailure failure) {
  switch (failure) {
    case ListFailure::kNone:
      return std::string();
    case ListFailure::kTooLarge:
      return "The list is larger than the allowed limit.";
    case ListFailure::kTimeout:
      return "The list took too long to parse.";
    case ListFailure::kNotText:
      return "The list is not text.";
    case ListFailure::kLooksLikeHtml:
      return "The address gave a web page, not a filter list.";
    case ListFailure::kNoRules:
      return "The list has no blocking rules.";
    case ListFailure::kUnreadable:
      return "The saved copy of the list could not be read.";
    case ListFailure::kDownload:
      return "The list could not be downloaded.";
    case ListFailure::kInsecureUrl:
      return "Only https addresses are allowed.";
    case ListFailure::kNonPublicUrl:
      return "This address is on your own network or computer, so Aerie does not fetch filter lists from it.";
  }
  return std::string();
}

// A fixed code for the settings page, which owns the words (a grd string per
// code). "" for kNone.
inline const char* ListFailureCode(ListFailure failure) {
  switch (failure) {
    case ListFailure::kNone:
      return "";
    case ListFailure::kTooLarge:
      return "too_large";
    case ListFailure::kTimeout:
      return "timeout";
    case ListFailure::kNotText:
      return "not_text";
    case ListFailure::kLooksLikeHtml:
      return "looks_like_html";
    case ListFailure::kNoRules:
      return "no_rules";
    case ListFailure::kUnreadable:
      return "unreadable";
    case ListFailure::kDownload:
      return "download";
    case ListFailure::kInsecureUrl:
      return "insecure_url";
    case ListFailure::kNonPublicUrl:
      return "non_public_url";
  }
  return "";
}

// The code for a text that ListFailureMessage() made (what FilterListEntry
// keeps in |error|), or "" if it is not one of them.
inline std::string ListFailureCodeForMessage(const std::string& message) {
  for (int i = static_cast<int>(ListFailure::kTooLarge);
       i <= static_cast<int>(ListFailure::kNonPublicUrl); ++i) {
    ListFailure failure = static_cast<ListFailure>(i);
    if (message == ListFailureMessage(failure)) {
      return ListFailureCode(failure);
    }
  }
  return std::string();
}

// The texts FilterListManager::AddList() gives for a list it refused for a
// reason that is not a ListFailure.
inline constexpr char kAddErrorDuplicate[] = "This list is already added.";
inline constexpr char kAddErrorTooMany[] = "Too many lists. Remove one first.";

// The code for the text AddList() gave (issue #108): a ListFailure code, or
// "duplicate" or "too_many". "" for no error or any other text. The page
// owns the words.
inline std::string AddListErrorCode(const std::string& message) {
  if (message == kAddErrorDuplicate) {
    return "duplicate";
  }
  if (message == kAddErrorTooMany) {
    return "too_many";
  }
  return ListFailureCodeForMessage(message);
}

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_LIST_FAILURE_H_
