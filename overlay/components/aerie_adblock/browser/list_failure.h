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
  }
  return std::string();
}

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_LIST_FAILURE_H_
