// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_ADBLOCK_BROWSER_SESSION_KEY_H_
#define COMPONENTS_AERIE_ADBLOCK_BROWSER_SESSION_KEY_H_

#include <functional>

#include "base/check.h"

namespace aerie_adblock {

// Which profile a request, a page or an exception belongs to, as far as
// per-site blocking exceptions care (issue #50).
//
// Either the regular profile (whose exceptions are saved to disk and hold for
// every profile), or one off-the-record session (an incognito window's
// profile), whose exceptions live in memory only and hold for that session
// alone. The off-the-record value is the profile itself as an opaque pointer:
// it is only compared, never dereferenced.
//
// There is no default constructor and no conversion from a pointer. Code that
// takes a SessionKey must be handed one, so a forgotten argument is a compile
// error instead of a silent "regular profile". Make one with Regular() or
// OffTheRecord(); in production code, with SessionKeyFor()
// (filter_engine_service.h).
class SessionKey {
 public:
  static SessionKey Regular() { return SessionKey(nullptr); }
  // |profile| identifies the session. Must not be null.
  static SessionKey OffTheRecord(const void* profile) {
    CHECK(profile);
    return SessionKey(profile);
  }

  bool is_regular() const { return profile_ == nullptr; }
  bool is_off_the_record() const { return profile_ != nullptr; }

  friend bool operator==(const SessionKey&, const SessionKey&) = default;
  friend bool operator<(const SessionKey& a, const SessionKey& b) {
    return std::less<const void*>()(a.profile_, b.profile_);
  }

 private:
  explicit SessionKey(const void* profile) : profile_(profile) {}

  const void* profile_;
};

}  // namespace aerie_adblock

#endif  // COMPONENTS_AERIE_ADBLOCK_BROWSER_SESSION_KEY_H_
