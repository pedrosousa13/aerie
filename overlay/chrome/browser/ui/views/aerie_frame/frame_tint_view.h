// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_FRAME_FRAME_TINT_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_FRAME_FRAME_TINT_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/scoped_multi_source_observation.h"
#include "components/aerie_frame/aerie_frame_paint.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"

class Profile;
class SkBitmap;

// The window frame of Aerie (issue #178): the Workspace colour as a gradient
// at the theme's tint strength, a shade and a soft glow at the corner nearest
// the sidebar, and the shadow of the page card. It is the lowest child of the
// BrowserView and has its own opaque layer. The sidebar and the top row draw on
// it, because the Helium fills that used to cover it paint nothing while it is
// on (patches/sidebar/0031 and 0032).
//
// It paints once, and again only when what the paint depends on changes: the
// Workspace colour, the colour mode, the window size, the card's box or the
// side of the sidebar (aerie_frame::FrameTintState). Nothing about it is
// animated: no ui::LayerAnimator element, filter, blur or shadow is ever set
// on its layer.
class FrameTintView : public views::View,
                      public views::ViewObserver,
                      public aerie_tab_model::TabModelObserver {
  METADATA_HEADER(FrameTintView, views::View)

 public:
  explicit FrameTintView(Profile* profile);
  FrameTintView(const FrameTintView&) = delete;
  FrameTintView& operator=(const FrameTintView&) = delete;
  ~FrameTintView() override;

  // The page card (the contents container): its box decides where the shadow
  // is painted. Called once the container exists.
  void SetCard(views::View* card);

  // How many times OnPaint ran.
  int paint_count() const { return state_.paint_count(); }
  const aerie_frame::FrameTintInputs& inputs_for_testing() const {
    return state_.inputs();
  }

  // Paints exactly what OnPaint paints into `bitmap`, which is made `size()`
  // big, without touching the paint counter. For tests.
  void PaintToBitmapForTesting(SkBitmap* bitmap) const;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;
  void ViewHierarchyChanged(const views::ViewHierarchyChangedDetails& details) override;

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override;
  void OnViewIsDeleting(views::View* observed_view) override;

  // aerie_tab_model::TabModelObserver:
  void OnWorkspaceChanged(const aerie_tab_model::Workspace& workspace) override;
  void OnModelLoaded() override;

 private:
  aerie_frame::FrameTintInputs CurrentInputs() const;
  // Compares what the paint depends on with the last paint and schedules one
  // paint if it differs.
  void Refresh();

  const raw_ptr<Profile> profile_;
  raw_ptr<aerie_tab_model::TabModel> model_ = nullptr;
  raw_ptr<views::View> card_ = nullptr;
  aerie_frame::FrameTintState state_;
  PrefChangeRegistrar pref_registrar_;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      view_observations_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_FRAME_FRAME_TINT_VIEW_H_
