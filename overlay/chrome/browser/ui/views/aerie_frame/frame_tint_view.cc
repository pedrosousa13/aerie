// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_frame/frame_tint_view.h"

#include <string>

#include "base/functional/bind.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/aerie_look/aerie_look.h"
#include "chrome/browser/ui/views/frame/browser_frame_view.h"
#include "chrome/common/pref_names.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "components/prefs/pref_service.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider_key.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"

FrameTintView::FrameTintView(Profile* profile) : profile_(profile) {
  // Its own opaque layer, made with the window: the tint is not created later
  // and nothing is decoded or uploaded but one frame of gradient fills.
  SetPaintToLayer(ui::LAYER_TEXTURED);
  layer()->SetFillsBoundsOpaquely(true);
  SetCanProcessEventsWithinSubtree(false);
  GetViewAccessibility().SetIsIgnored(true);

  if (auto* service =
          aerie_tab_model::TabModelServiceFactory::GetForProfile(profile_)) {
    model_ = service->model();
    model_->AddObserver(this);
  }
  // The effective look and what a change of it does (issue #185).
  look_controller_ =
      std::make_unique<SidebarLookController>(profile_, model_, this);
  pref_registrar_.Init(profile_->GetPrefs());
  pref_registrar_.Add(prefs::kHeliumVerticalRightAligned,
                      base::BindRepeating(&FrameTintView::Refresh,
                                          base::Unretained(this)));
}

FrameTintView::~FrameTintView() {
  if (model_) {
    model_->RemoveObserver(this);
  }
}

void FrameTintView::SetCard(views::View* card) {
  if (card_) {
    view_observations_.RemoveObservation(card_);
  }
  card_ = card;
  if (card_) {
    view_observations_.AddObservation(card_);
  }
  Refresh();
}

aerie_frame::FrameTintInputs FrameTintView::CurrentInputs() const {
  aerie_frame::FrameTintInputs inputs;
  const aerie::tokens::EffectiveLook look = look_controller_->look();
  inputs.theme = look.theme;
  inputs.mode = look.mode;
  if (look.system_colours && GetWidget()) {
    inputs.system_colours =
        aerie_look::ReadSystemColours(*GetWidget()->GetColorProvider());
  }
  inputs.colour = model_ ? model_->workspace().colour
                         : std::string(aerie_frame::kFallbackColour);
  inputs.size = size();
  if (card_ && card_->GetVisible() && card_->parent()) {
    inputs.card =
        views::View::ConvertRectToTarget(card_->parent(), this, card_->bounds());
  }
  inputs.window_corners = WindowCorners();
  inputs.sidebar_on_right =
      profile_->GetPrefs()->GetBoolean(prefs::kHeliumVerticalRightAligned);
  return inputs;
}

void FrameTintView::SetWindowCornersForTesting(
    std::optional<gfx::RoundedCornersF> corners) {
  if (corners) {
    SetWindowCornersSourceForTesting(
        base::BindRepeating([](gfx::RoundedCornersF c) { return c; }, *corners));
  } else {
    SetWindowCornersSourceForTesting({});
  }
}

void FrameTintView::SetWindowCornersSourceForTesting(
    base::RepeatingCallback<gfx::RoundedCornersF()> source) {
  window_corners_for_testing_ = std::move(source);
  Refresh();
}

gfx::RoundedCornersF FrameTintView::WindowCorners() const {
  if (window_corners_for_testing_) {
    return window_corners_for_testing_.Run();
  }
  // The corners the frame view draws for the window now: the OS radius while
  // the window is restored, none when it is maximised, fullscreen or tiled.
  // The same call Helium's own window-edge fills read (issue #249).
  if (const views::Widget* widget = GetWidget(); widget && widget->non_client_view()) {
    if (const auto* frame_view = static_cast<const BrowserFrameView*>(
            widget->non_client_view()->frame_view())) {
      return frame_view->GetWindowRoundedCorners();
    }
  }
  return gfx::RoundedCornersF();
}

void FrameTintView::Refresh() {
  if (state_.Update(CurrentInputs())) {
    // Opaque only while the corners are square: a rounded window leaves them
    // transparent.
    layer()->SetFillsBoundsOpaquely(
        aerie_frame::IsSquare(state_.inputs().window_corners));
    SchedulePaint();
  }
}

void FrameTintView::OnWidgetShowStateChanged(views::Widget*) {
  Refresh();
}

void FrameTintView::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) {
  Refresh();
}

void FrameTintView::OnPaint(gfx::Canvas* canvas) {
  // The paint always reads the current state, so a paint that nothing asked
  // for (the first one, an expose) still draws the right frame.
  state_.Update(CurrentInputs());
  state_.RecordPaint();
  const aerie_frame::FrameTintInputs& in = state_.inputs();
  aerie_frame::PaintFrame(canvas, aerie_frame::PaletteOf(in), size(),
                          in.sidebar_on_right, in.card, in.window_corners);
}

void FrameTintView::PaintToBitmapForTesting(SkBitmap* bitmap) const {
  const aerie_frame::FrameTintInputs in = CurrentInputs();
  gfx::Canvas canvas(size(), 1.0f,
                     /*is_opaque=*/aerie_frame::IsSquare(in.window_corners));
  aerie_frame::PaintFrame(&canvas, aerie_frame::PaletteOf(in), size(),
                          in.sidebar_on_right, in.card, in.window_corners);
  *bitmap = canvas.GetBitmap();
}

void FrameTintView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // A change of the colour mode arrives here.
  Refresh();
}

void FrameTintView::AddedToWidget() {
  views::View::AddedToWidget();
  widget_observation_.Observe(GetWidget());
  Refresh();
  look_controller_->OnAddedToWidget();
}

void FrameTintView::RemovedFromWidget() {
  widget_observation_.Reset();
  look_controller_->OnRemovedFromWidget();
  views::View::RemovedFromWidget();
}

views::Widget* FrameTintView::GetLookWidget() {
  return GetWidget();
}

void FrameTintView::OnEffectiveLookChanged() {
  Refresh();
}

void FrameTintView::ViewHierarchyChanged(
    const views::ViewHierarchyChangedDetails& details) {
  views::View::ViewHierarchyChanged(details);
  if (details.child == this && details.is_add && details.parent) {
    view_observations_.AddObservation(details.parent);
    SetBoundsRect(details.parent->GetLocalBounds());
  }
}

void FrameTintView::OnViewBoundsChanged(views::View* observed_view) {
  if (observed_view == parent()) {
    SetBoundsRect(parent()->GetLocalBounds());
  }
  // The card moves with the layout; the layout has run when its bounds change.
  Refresh();
}

void FrameTintView::OnViewLayoutInvalidated(views::View* observed_view) {
  // A change of the tiled state invalidates the layout of the client view (the
  // BrowserView) and of the frame view, even when the bounds stay the same
  // (BrowserDesktopWindowTreeHostLinux::OnWindowTiledStateChanged), and the
  // corners depend on it.
  if (observed_view == parent()) {
    Refresh();
  }
}

void FrameTintView::OnViewIsDeleting(views::View* observed_view) {
  view_observations_.RemoveObservation(observed_view);
  if (observed_view == card_) {
    card_ = nullptr;
  }
}

void FrameTintView::OnWorkspaceChanged(const aerie_tab_model::Workspace&) {
  look_controller_->OnWorkspaceChanged();
  Refresh();
}

void FrameTintView::OnModelLoaded() {
  look_controller_->OnWorkspaceChanged();
  Refresh();
}

BEGIN_METADATA(FrameTintView)
END_METADATA
