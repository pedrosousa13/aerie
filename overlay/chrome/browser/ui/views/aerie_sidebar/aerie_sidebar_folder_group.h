// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_GROUP_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_GROUP_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "ui/gfx/animation/animation_delegate.h"

class AerieSidebarRowView;
class PrefService;

namespace gfx {
class SlideAnimation;
}  // namespace gfx

namespace views {
class View;
}  // namespace views

// The motion of one folder opening or shutting (issue #181, docs/sidebar-
// design.md, "Folder reveal and collapse"). It owns what the rows of the group
// share: one gfx::SlideAnimation for the height of the container (the layout
// exception, a clipped, single list), and the end of the motion. Each row fades
// its own content with its own layer (AnimateLayer). Reveal: the rows fade in
// `motion-timing-stagger` apart for the first `motion-timing-stagger-cap`
// rows, while the height grows. Collapse: the rows fade out together while the
// height shrinks, and the sidebar removes them when it is over. Reduced motion:
// opacity only, no stagger, the height does not animate.
//
// A new toggle of the same folder while the motion runs turns it around from
// the values it has reached (Retarget). Anything else that changes the rows
// ends the motion at once (Finish).
class AerieSidebarFolderGroup : public gfx::AnimationDelegate {
 public:
  // `rows` are in order, top to bottom. `list` is the view that lays them out.
  // `done` runs once when the motion is over, from Finish too.
  AerieSidebarFolderGroup(std::string folder_id,
                          std::vector<AerieSidebarRowView*> rows,
                          views::View* list,
                          const PrefService* prefs,
                          base::OnceClosure done);
  AerieSidebarFolderGroup(const AerieSidebarFolderGroup&) = delete;
  AerieSidebarFolderGroup& operator=(const AerieSidebarFolderGroup&) = delete;
  ~AerieSidebarFolderGroup() override;

  void Start(bool reveal);
  void Retarget(bool reveal);
  // Puts every row at rest at once and runs `done`.
  void Finish();

  const std::string& folder_id() const { return folder_id_; }
  bool reveal() const { return reveal_; }
  bool running() const { return running_; }
  // The container height animation; null under reduced motion.
  const gfx::SlideAnimation* height_animation() const { return slide_.get(); }
  size_t row_count() const { return rows_.size(); }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;

 private:
  void Play(bool reveal, bool turn_around);
  void SetHeights(double fraction);
  void End();

  const std::string folder_id_;
  std::vector<base::WeakPtr<AerieSidebarRowView>> rows_;
  const raw_ptr<views::View> list_;
  const raw_ptr<const PrefService> prefs_;
  base::OnceClosure done_;
  bool reveal_ = true;
  bool running_ = false;
  std::unique_ptr<gfx::SlideAnimation> slide_;
  base::OneShotTimer end_timer_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FOLDER_GROUP_H_
