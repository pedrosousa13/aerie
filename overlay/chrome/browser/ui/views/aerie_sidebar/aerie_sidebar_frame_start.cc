// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_frame_start.h"

#include <utility>

#include "base/location.h"
#include "ui/compositor/compositor.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace aerie_sidebar {

AerieFrameStart::AerieFrameStart() {
  // A step that waits for the window to draw is not a stuck animation.
  set_check_active_duration(false);
}

AerieFrameStart::~AerieFrameStart() {
  Cancel();
}

bool AerieFrameStart::Post(ui::Compositor* compositor, base::OnceClosure task) {
  if (!compositor) {
    return false;
  }
  Cancel();
  task_ = std::move(task);
  compositor_ = compositor;
  compositor_->AddAnimationObserver(this);
  fallback_.Start(FROM_HERE, kFallback,
                  base::BindOnce(&AerieFrameStart::Run, base::Unretained(this)));
  return true;
}

bool AerieFrameStart::Post(views::View* view, base::OnceClosure task) {
  views::Widget* const widget = view ? view->GetWidget() : nullptr;
  return Post(widget ? widget->GetCompositor() : nullptr, std::move(task));
}

void AerieFrameStart::Cancel() {
  fallback_.Stop();
  if (compositor_) {
    compositor_->RemoveAnimationObserver(this);
    compositor_ = nullptr;
  }
  task_.Reset();
}

void AerieFrameStart::OnAnimationStep(base::TimeTicks) {
  Run();
}

void AerieFrameStart::OnCompositingShuttingDown(ui::Compositor*) {
  Cancel();
}

void AerieFrameStart::Run() {
  base::OnceClosure task = std::move(task_);
  Cancel();
  if (task) {
    std::move(task).Run();
  }
}

}  // namespace aerie_sidebar
