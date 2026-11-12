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

AerieSidebarFolderGroup::AerieSidebarFolderGroup(
    std::string folder_id,
    std::vector<raw_ptr<AerieSidebarRowView>> rows,
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
  // Destroyed in the middle of a motion: every row is put at rest and the
  // sidebar removes the rows that were collapsing, with no help from Finish.
  if (running_) {
    End();
  }
}

void AerieSidebarFolderGroup::Start(bool reveal) {
  running_ = true;
  reduced_ = aerie::motion::ShouldReduceMotion(prefs_);
  Play(reveal, /*turn_around=*/false);
}

void AerieSidebarFolderGroup::Retarget(bool reveal) {
  if (!running_ || reveal == reveal_) {
    return;
  }
  // The motion began under the other setting: the slide and the timers do not
  // carry over, so the rows go to the end state at once.
  if (aerie::motion::ShouldReduceMotion(prefs_) != reduced_) {
    reveal_ = reveal;
    End();
    return;
  }
  Play(reveal, /*turn_around=*/true);
}

void AerieSidebarFolderGroup::Play(bool reveal, bool turn_around) {
  reveal_ = reveal;
  height_timer_.Stop();
  const aerie_sidebar::FolderMotion motion =
      reveal ? aerie_sidebar::FolderRevealMotion(reduced_)
             : aerie_sidebar::FolderCollapseMotion(reduced_);
  int longest_delay = 0;
  int index = 0;
  for (const auto& row : rows_) {
    if (!row) {
      continue;
    }
    // Only a reveal staggers, and only a fresh one: a turn-around has no delay.
    const int delay =
        (reveal && !turn_around)
            ? aerie_sidebar::RevealStaggerDelayMs(index, reduced_)
            : 0;
    longest_delay = std::max(longest_delay, delay);
    row->PlayFolderFade(motion, delay, prefs_,
                        /*start_from_current=*/turn_around);
    ++index;
  }
  if (motion.animate_height) {
    if (!slide_) {
      slide_ = std::make_unique<gfx::SlideAnimation>(this);
      slide_->SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN);
    }
    if (!turn_around) {
      slide_->Reset(reveal ? 0.0 : 1.0);
      SetHeights(reveal ? 0.0 : 1.0);
    } else {
      // Hold the height where it is; the phase below moves it on.
      slide_->Reset(slide_->GetCurrentValue());
    }
    if (motion.height_delay_ms <= 0) {
      StartHeight(reveal, motion.height_duration_ms);
    } else {
      height_timer_.Start(
          FROM_HERE, base::Milliseconds(motion.height_delay_ms),
          base::BindOnce(&AerieSidebarFolderGroup::StartHeight,
                         base::Unretained(this), reveal,
                         motion.height_duration_ms));
    }
  }
  end_timer_.Start(
      FROM_HERE,
      base::Milliseconds(motion.total_ms + longest_delay +
                         AerieSidebarRowView::kDropLayersSlackMs),
      base::BindOnce(&AerieSidebarFolderGroup::End, base::Unretained(this)));
}

void AerieSidebarFolderGroup::StartHeight(bool reveal, int duration_ms) {
  slide_->SetSlideDuration(base::Milliseconds(duration_ms));
  if (reveal) {
    slide_->Show();
  } else {
    slide_->Hide();
  }
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
  running_ = false;
  end_timer_.Stop();
  height_timer_.Stop();
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
