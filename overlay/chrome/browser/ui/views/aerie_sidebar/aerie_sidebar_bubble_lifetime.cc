// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_bubble_lifetime.h"

#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

namespace aerie_sidebar {

namespace {

// Owns itself: it goes with the widget it watches.
class BubbleLifetime : public views::WidgetObserver {
 public:
  BubbleLifetime(views::BubbleDialogDelegate* delegate, views::Widget* widget)
      : delegate_(delegate) {
    observation_.Observe(widget);
  }

  void OnWidgetDestroying(views::Widget*) override {
    // The window keeps no observer for a bubble that is going.
    delegate_->SetAnchorView(nullptr);
  }

  void OnWidgetDestroyed(views::Widget*) override {
    // The widget calls the delegate once more after this, so it is deleted
    // later.
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, delegate_.get());
    delete this;
  }

 private:
  ~BubbleLifetime() override = default;

  raw_ptr<views::BubbleDialogDelegate> delegate_;
  base::ScopedObservation<views::Widget, views::WidgetObserver> observation_{
      this};
};

}  // namespace

void DeleteDelegateWithWidget(views::BubbleDialogDelegate* delegate,
                              views::Widget* widget) {
  new BubbleLifetime(delegate, widget);
}

}  // namespace aerie_sidebar
