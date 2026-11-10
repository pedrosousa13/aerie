// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_BLOCKING_LEVEL_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_BLOCKING_LEVEL_H_

#include <cstdint>
#include <optional>
#include <string_view>

namespace aerie_adblock {

// How far network blocking goes on a page (issue #109). "Off" is not a level:
// it is the per-site exception that already exists.
//
// The two levels follow Brave Shields. In brave-core
// (AdBlockEngineWrapper::ShouldStartRequest), a request that is first party to
// the page gets no verdict from the default lists unless Aggressive is on, and
// that includes their `$important` rules; a third-party request is judged the
// same in both.
//
// Browser-side only. The level is decided from the browser's own state about
// the page's site (FilterListManager) and passed to the engine as an argument.
// No renderer can set it.
enum class BlockingLevel : uint8_t {
  // First-party and third-party requests that match a rule are blocked. What
  // Aerie did before the levels existed, and the default.
  kAggressive,
  // Requests to the page's own site (same registrable domain) are not blocked
  // by built-in lists, `$important` rules included; custom lists still judge
  // them. Third-party requests are blocked as in Aggressive.
  kStandard,
};

inline constexpr BlockingLevel kDefaultBlockingLevel =
    BlockingLevel::kAggressive;

// The names used in the saved state and in messages from the Settings page.
inline constexpr std::string_view BlockingLevelName(BlockingLevel level) {
  return level == BlockingLevel::kStandard ? "standard" : "aggressive";
}

inline constexpr std::optional<BlockingLevel> BlockingLevelFromName(
    std::string_view name) {
  if (name == "aggressive") {
    return BlockingLevel::kAggressive;
  }
  if (name == "standard") {
    return BlockingLevel::kStandard;
  }
  return std::nullopt;
}

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_BLOCKING_LEVEL_H_
