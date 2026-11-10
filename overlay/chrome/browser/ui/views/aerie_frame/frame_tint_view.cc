// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_frame/frame_tint_view.h"

#include <string>

#include "base/functional/bind.h"
#include "chrome/browser/aerie_tab_model/tab_model_service.h"
#include "chrome/browser/aerie_tab_model/tab_model_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/aerie_frame/aerie_frame_palette.h"
#include "components/prefs/pref_service.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/color/color_provider_key.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/widget/widget.h"

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
  inputs.theme = aerie_frame::kDefaultTheme;
  // Mode is the system one by default, and never follows the colour (the
  // design's "Themes"); the global setting and the Workspace override are
  // slice I.
  inputs.mode = GetWidget() && GetWidget()->GetColorMode() ==
                                   ui::ColorProviderKey::ColorMode::kDark
                    ? aerie::tokens::ColorMode::kDark
                    : aerie::tokens::ColorMode::kLight;
  inputs.colour = model_ ? model_->workspace().colour
                         : std::string(aerie_frame::kFallbackColour);
  inputs.size = size();
  if (card_ && card_->GetVisible() && card_->parent()) {
    inputs.card =
        views::View::ConvertRectToTarget(card_->parent(), this, card_->bounds());
  }
  inputs.sidebar_on_right =
      profile_->GetPrefs()->GetBoolean(prefs::kHeliumVerticalRightAligned);
  return inputs;
}

void FrameTintView::Refresh() {
  if (state_.Update(CurrentInputs())) {
    SchedulePaint();
  }
}

void FrameTintView::OnPaint(gfx::Canvas* canvas) {
  // The paint always reads the current state, so a paint that nothing asked
  // for (the first one, an expose) still draws the right frame.
  state_.Update(CurrentInputs());
  state_.RecordPaint();
  const aerie_frame::FrameTintInputs& in = state_.inputs();
  aerie_frame::PaintFrame(
      canvas, aerie_frame::ResolveFramePalette(in.theme, in.mode, in.colour),
      size(), in.sidebar_on_right, in.card);
}

void FrameTintView::PaintToBitmapForTesting(SkBitmap* bitmap) const {
  const aerie_frame::FrameTintInputs in = CurrentInputs();
  gfx::Canvas canvas(size(), 1.0f, /*is_opaque=*/true);
  aerie_frame::PaintFrame(
      &canvas, aerie_frame::ResolveFramePalette(in.theme, in.mode, in.colour),
      size(), in.sidebar_on_right, in.card);
  *bitmap = canvas.GetBitmap();
}

void FrameTintView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // A change of the colour mode arrives here.
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

void FrameTintView::OnViewIsDeleting(views::View* observed_view) {
  view_observations_.RemoveObservation(observed_view);
  if (observed_view == card_) {
    card_ = nullptr;
  }
}

void FrameTintView::OnWorkspaceChanged(const aerie_tab_model::Workspace&) {
  Refresh();
}

void FrameTintView::OnModelLoaded() {
  Refresh();
}

BEGIN_METADATA(FrameTintView)
END_METADATA
