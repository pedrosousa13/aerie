// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FRAME_START_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FRAME_START_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/compositor/compositor_animation_observer.h"

namespace ui {
class Compositor;
}
namespace views {
class View;
}

namespace aerie_sidebar {

// Runs one task at the start of the compositor's next main frame (issue #237).
//
// A key press or a click lands anywhere inside a vsync. The layout, paint and
// raster it causes then run from that moment, and a frame that began at the
// start of the vsync is on screen a whole vsync late (15 to 25 ms against a
// 16.7 ms budget). The sidebar's start of an animation is not urgent to the
// millisecond: it waits for the next begin-frame and does its first-frame work
// there, so that the work and the draw fit in one vsync. The wait is at most
// one vsync.
//
// Posting again replaces the task that is waiting. A window that gets no
// frames (hidden, minimised) must not swallow the task, so a timer runs it
// anyway after `kFallback`.
class AerieFrameStart : public ui::CompositorAnimationObserver {
 public:
  static constexpr base::TimeDelta kFallback = base::Milliseconds(100);

  AerieFrameStart();
  AerieFrameStart(const AerieFrameStart&) = delete;
  AerieFrameStart& operator=(const AerieFrameStart&) = delete;
  ~AerieFrameStart() override;

  // Returns false, and does nothing, when there is no compositor: the caller
  // runs `task` at once.
  bool Post(ui::Compositor* compositor, base::OnceClosure task);
  // The compositor of the view's widget.
  bool Post(views::View* view, base::OnceClosure task);
  void Cancel();
  bool pending() const { return !task_.is_null(); }

  // ui::CompositorAnimationObserver:
  void OnAnimationStep(base::TimeTicks timestamp) override;
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;

 private:
  void Run();

  raw_ptr<ui::Compositor> compositor_ = nullptr;
  base::OnceClosure task_;
  base::OneShotTimer fallback_;
};

}  // namespace aerie_sidebar

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_FRAME_START_H_
