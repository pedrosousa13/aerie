// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_folder_group.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_row_view.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_tree.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/views/view.h"

namespace {
// A row keeps its layers this long after the motion's own length so that the
// last frame is drawn before they go. Not a design value.
constexpr int kEndSlackMs = 50;
}  // namespace

AerieSidebarFolderGroup::AerieSidebarFolderGroup(
    std::string folder_id,
    std::vector<AerieSidebarRowView*> rows,
    views::View* list,
    const PrefService* prefs,
    base::OnceClosure done)
    : folder_id_(std::move(folder_id)),
      list_(list),
      prefs_(prefs),
      done_(std::move(done)) {
  for (AerieSidebarRowView* row : rows) {
    rows_.push_back(row->GetWeakPtr());
  }
}

AerieSidebarFolderGroup::~AerieSidebarFolderGroup() {
  // Nothing here touches the rows: they may be gone already.
  if (slide_) {
    slide_->Stop();
  }
}

void AerieSidebarFolderGroup::Start(bool reveal) {
  running_ = true;
  Play(reveal, /*turn_around=*/false);
}

void AerieSidebarFolderGroup::Retarget(bool reveal) {
  if (!running_ || reveal == reveal_) {
    return;
  }
  Play(reveal, /*turn_around=*/true);
}

void AerieSidebarFolderGroup::Play(bool reveal, bool turn_around) {
  reveal_ = reveal;
  const bool reduced = aerie::motion::ShouldReduceMotion(prefs_);
  const aerie_sidebar::FolderMotion motion =
      reveal ? aerie_sidebar::FolderRevealMotion(reduced)
             : aerie_sidebar::FolderCollapseMotion(reduced);
  int longest_delay = 0;
  int index = 0;
  for (const auto& row : rows_) {
    if (!row) {
      continue;
    }
    // Only a reveal staggers, and only a fresh one: a turn-around has no delay.
    const int delay = (reveal && !turn_around)
                          ? aerie_sidebar::RevealStaggerDelayMs(index, reduced)
                          : 0;
    longest_delay = std::max(longest_delay, delay);
    row->PlayFolderFade(motion, delay, prefs_, /*start_from_current=*/turn_around);
    ++index;
  }
  if (motion.animate_height) {
    if (!slide_) {
      slide_ = std::make_unique<gfx::SlideAnimation>(this);
      slide_->SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
    }
    slide_->SetSlideDuration(base::Milliseconds(motion.height_duration_ms));
    if (!turn_around) {
      slide_->Reset(reveal ? 0.0 : 1.0);
      SetHeights(reveal ? 0.0 : 1.0);
    }
    if (reveal) {
      slide_->Show();
    } else {
      slide_->Hide();
    }
  }
  end_timer_.Start(
      FROM_HERE,
      base::Milliseconds(motion.duration_ms + longest_delay + kEndSlackMs),
      base::BindOnce(&AerieSidebarFolderGroup::End, base::Unretained(this)));
}

void AerieSidebarFolderGroup::SetHeights(double fraction) {
  for (const auto& row : rows_) {
    if (row) {
      row->SetHeightFraction(fraction);
    }
  }
  if (list_) {
    list_->InvalidateLayout();
  }
}

void AerieSidebarFolderGroup::AnimationProgressed(
    const gfx::Animation* animation) {
  SetHeights(slide_->GetCurrentValue());
}

void AerieSidebarFolderGroup::Finish() {
  if (!running_) {
    return;
  }
  End();
}

void AerieSidebarFolderGroup::End() {
  if (!running_) {
    return;
  }
  running_ = false;
  end_timer_.Stop();
  if (slide_) {
    slide_->Stop();
  }
  for (const auto& row : rows_) {
    if (row) {
      row->EndFolderMotion();
    }
  }
  if (list_) {
    list_->InvalidateLayout();
  }
  if (done_) {
    std::move(done_).Run();
  }
}
