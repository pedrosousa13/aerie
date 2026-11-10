// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_adblock/browser/guard_target.h"

#include "components/aerie_adblock/browser/filter_engine_service.h"
#include "components/aerie_adblock/browser/filter_list_manager.h"
#include "components/aerie_adblock/browser/page_block_counter.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace aerie_adblock {

GuardState GuardStateOfTab(content::WebContents* tab) {
  if (!tab) {
    return GuardState();
  }
  // The committed URL of the primary main frame: what the browser process
  // knows, never a URL a renderer proposes.
  const GURL& url = tab->GetPrimaryMainFrame()->GetLastCommittedURL();
  return ComputeGuardState(url, SessionKeyFor(tab->GetBrowserContext()),
                           FilterListManager::Get(),
                           PageBlockCounter::CountFor(tab));
}

GuardTarget::GuardTarget() = default;
GuardTarget::GuardTarget(const GuardTarget&) = default;
GuardTarget& GuardTarget::operator=(const GuardTarget&) = default;
GuardTarget::~GuardTarget() = default;

// static
GuardTarget GuardTarget::Capture(content::WebContents* tab) {
  GuardTarget target;
  const GuardState state = GuardStateOfTab(tab);
  if (state.visible) {
    target.tab_ = tab->GetWeakPtr();
    target.host_ = state.host;
  }
  return target;
}

std::optional<GuardState> GuardTarget::Resolve() const {
  if (empty() || !tab_) {
    return std::nullopt;
  }
  GuardState state = GuardStateOfTab(tab_.get());
  if (!state.visible || state.host != host_) {
    return std::nullopt;
  }
  return state;
}

GuardTarget::Toggle GuardTarget::SetBlockingOn(bool blocking_on) const {
  const std::optional<GuardState> state = Resolve();
  FilterListManager* manager = FilterListManager::Get();
  if (!state || !manager) {
    return Toggle::kStale;
  }
  if (state->locked) {
    return Toggle::kRefused;
  }
  // The manager watches the session: an incognito tab cannot undo a saved
  // exception, and its own changes stay in memory.
  if (manager->SetException(host_, !blocking_on, tab_->GetBrowserContext()) !=
      ExceptionResult::kOk) {
    return Toggle::kRefused;
  }
  // An excepted site loads its ads on the next load; the page that is open
  // keeps what it has. Reload so the change is visible at once.
  tab_->GetController().Reload(content::ReloadType::NORMAL, true);
  return Toggle::kApplied;
}

GuardTarget::Toggle GuardTarget::SetCosmeticHidingOn(bool hiding_on) const {
  const std::optional<GuardState> state = Resolve();
  FilterListManager* manager = FilterListManager::Get();
  if (!state || !manager) {
    return Toggle::kStale;
  }
  if (state->cosmetic_locked && hiding_on) {
    return Toggle::kRefused;
  }
  if (manager->SetCosmeticHidingOff(host_, !hiding_on,
                                    tab_->GetBrowserContext()) !=
      ExceptionResult::kOk) {
    return Toggle::kRefused;
  }
  tab_->GetController().Reload(content::ReloadType::NORMAL, true);
  return Toggle::kApplied;
}

GuardTarget::Toggle GuardTarget::SetBlockingLevel(BlockingLevel level) const {
  const std::optional<GuardState> state = Resolve();
  FilterListManager* manager = FilterListManager::Get();
  if (!state || !manager) {
    return Toggle::kStale;
  }
  content::BrowserContext* context = tab_->GetBrowserContext();
  if (state->blocking_off && state->locked) {
    return Toggle::kRefused;
  }
  // The level goes first: it is the step that can hit the table cap, and a
  // refusal must leave blocking as it was (off stays off, no reload).
  if (manager->SetBlockingLevel(host_, level, context) !=
      ExceptionResult::kOk) {
    return Toggle::kRefused;
  }
  if (state->blocking_off &&
      manager->SetException(host_, /*excepted=*/false, context) !=
          ExceptionResult::kOk) {
    return Toggle::kRefused;
  }
  tab_->GetController().Reload(content::ReloadType::NORMAL, true);
  return Toggle::kApplied;
}

}  // namespace aerie_adblock
