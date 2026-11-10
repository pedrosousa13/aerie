// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/guard_state.h"

#include "base/strings/string_number_conversions.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "url/gurl.h"

namespace aerie_adblock {

std::u16string GuardBadgeLabel(int count) {
  if (count <= 0) {
    return std::u16string();
  }
  return count > 99 ? u"99+" : base::NumberToString16(count);
}

std::u16string GuardBadgeLabel(int count, bool show_count) {
  if (!show_count) {
    return std::u16string();
  }
  return GuardBadgeLabel(count);
}

GuardState ComputeGuardState(const GURL& committed_url,
                             SessionKey session,
                             const FilterListManager* manager,
                             int blocked_count) {
  GuardState state;
  if (!committed_url.is_valid() || !committed_url.SchemeIsHTTPOrHTTPS() ||
      !committed_url.has_host()) {
    return state;
  }
  state.visible = true;
  state.host = std::string(committed_url.host());
  if (manager) {
    state.blocking_off = manager->HasException(state.host, session);
    state.locked = manager->IsExceptionLocked(state.host, session);
    state.cosmetic_off = manager->IsCosmeticHidingOff(state.host, session);
    state.cosmetic_locked =
        manager->IsCosmeticHidingLocked(state.host, session);
    state.level = manager->EffectiveBlockingLevel(state.host, session);
  }
  state.count =
      state.blocking_off ? 0 : (blocked_count > 0 ? blocked_count : 0);
  return state;
}

}  // namespace aerie_adblock
