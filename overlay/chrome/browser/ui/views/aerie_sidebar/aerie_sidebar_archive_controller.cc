// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_archive_controller.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/aerie_tab_model/tab_archiver_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_archive_button.h"
#include "chrome/grit/generated_resources.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_tab_model/archive.h"
#include "components/aerie_tab_model/tab_model.h"
#include "components/aerie_tab_model/tab_model_prefs.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/models/image_model.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/view.h"

namespace {

// A little slack so that the timer fires after the animation has ended.
constexpr int kLandingSlackMs = 30;

gfx::Point ToSidebar(views::View* sidebar, const gfx::Point& screen) {
  gfx::Point p = screen;
  views::View::ConvertPointFromScreen(sidebar, &p);
  return p;
}

}  // namespace

AerieSidebarArchiveController::Flight::Flight() = default;
AerieSidebarArchiveController::Flight::~Flight() {
  motion.Abort();
}

AerieSidebarArchiveController::AerieSidebarArchiveController(ViewHost* host)
    : host_(host) {}

AerieSidebarArchiveController::~AerieSidebarArchiveController() {
  Detach();
}

void AerieSidebarArchiveController::Attach() {
  if (archiver_) {
    return;
  }
  Profile* profile = host_->Browser()->GetProfile();
  archiver_ = aerie_tab_model::TabArchiverFactory::GetForProfile(profile);
  if (archiver_) {
    observation_.Observe(archiver_);
  }
}

void AerieSidebarArchiveController::Detach() {
  observation_.Reset();
  archiver_ = nullptr;
  if (panel_) {
    panel_->ClosePanel(/*refocus_button=*/false);
  }
  // A copy in the air goes with the controller.
  while (!flights_.empty()) {
    EndFlight(flights_.begin()->first);
  }
}

void AerieSidebarArchiveController::TogglePanel() {
  if (panel_) {
    panel_->ClosePanel(/*refocus_button=*/true);
    return;
  }
  Attach();
  panel_ = AerieSidebarArchivePanel::Show(host_->ArchiveButton(), this,
                                          host_->Palette())
               ->GetWeakPtr();
}

const aerie::motion::MotionHandle* AerieSidebarArchiveController::FlightMotion(
    const std::string& id) const {
  auto it = flights_.find(id);
  return it == flights_.end() ? nullptr : &it->second->motion;
}

AerieSidebarArchiveController::Flight* AerieSidebarArchiveController::StartFlight(
    const std::string& key,
    const AerieSidebarDragContent& content,
    const gfx::Rect& rect) {
  EndFlight(key);
  auto flight = std::make_unique<Flight>();
  flight->ghost = host_->SidebarView()->AddChildView(
      std::make_unique<AerieSidebarDragGhost>(content, host_->Palette(),
                                              host_->Dark()));
  flight->ghost->SetContentBounds(rect);
  Flight* raw = flight.get();
  flights_[key] = std::move(flight);
  return raw;
}

void AerieSidebarArchiveController::EndFlight(const std::string& key) {
  auto it = flights_.find(key);
  if (it == flights_.end()) {
    return;
  }
  std::unique_ptr<Flight> flight = std::move(it->second);
  flights_.erase(it);
  flight->timer.Stop();
  flight->motion.Abort();
  if (flight->ghost) {
    views::View* parent = flight->ghost->parent();
    if (parent) {
      parent->RemoveChildViewT(flight->ghost.get());
    }
  }
  if (flight->pulse_on_landing && host_->ArchiveButton()) {
    host_->ArchiveButton()->Pulse(host_->Prefs());
  }
}

// ---- the archive moment -----------------------------------------------------

void AerieSidebarArchiveController::OnTabArchiving(
    const std::string& record_id) {
  if (!host_->CanAnimate()) {
    return;
  }
  const auto content = host_->RowContent(record_id);
  const auto bounds = host_->RowBounds(record_id);
  if (!content || !bounds) {
    return;
  }
  StartFlight("archive:" + record_id, *content, *bounds);
}

void AerieSidebarArchiveController::OnTabArchiveFailed(
    const std::string& record_id) {
  EndFlight("archive:" + record_id);
}

void AerieSidebarArchiveController::OnTabArchived(const std::string& record_id,
                                                  bool animate) {
  const std::string key = "archive:" + record_id;
  auto it = flights_.find(key);
  if (it == flights_.end()) {
    return;
  }
  if (!animate || !host_->ArchiveButton()) {
    EndFlight(key);
    return;
  }
  Flight* flight = it->second.get();
  const PrefService* prefs = host_->Prefs();
  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const aerie_sidebar::ArchiveMotion plan =
      aerie_sidebar::ArchiveTabMotion(reduced);
  aerie::motion::LayerMotion target;
  if (plan.animate_transform) {
    // Shrink about the middle of the copy and move that middle onto the
    // middle of the button.
    views::View* sidebar = host_->SidebarView();
    gfx::Rect button = host_->ArchiveButton()->GetBoundsInScreen();
    const gfx::Point to = ToSidebar(sidebar, button.CenterPoint());
    const gfx::Point from =
        flight->ghost->origin() + flight->ghost->content_bounds().CenterPoint().OffsetFromOrigin();
    const gfx::Point centre = flight->ghost->content_bounds().CenterPoint();
    gfx::Transform transform;
    transform.Translate(to.x() - from.x(), to.y() - from.y());
    transform.Translate(centre.x(), centre.y());
    transform.Scale(plan.end_scale, plan.end_scale);
    transform.Translate(-centre.x(), -centre.y());
    target.transform = transform;
  }
  // The copy fades out as it lands (and, under reduced motion, in place).
  target.opacity = 0.f;
  flight->motion = aerie::motion::AnimateLayer(
      flight->ghost->layer(), target, plan.fly_duration, plan.fly_easing,
      prefs);
  flight->pulse_on_landing = true;
  flight->timer.Start(
      FROM_HERE, base::Milliseconds(plan.fly_ms + kLandingSlackMs),
      base::BindOnce(&AerieSidebarArchiveController::EndFlight,
                     weak_factory_.GetWeakPtr(), key));
}

// ---- the panel --------------------------------------------------------------

std::vector<aerie_tab_model::ArchivedRow>
AerieSidebarArchiveController::ArchivedRows() const {
  aerie_tab_model::TabModel* model = host_->Model();
  return model ? aerie_tab_model::ArchivedRows(*model)
               : std::vector<aerie_tab_model::ArchivedRow>();
}

std::u16string AerieSidebarArchiveController::WorkspaceName(
    const std::string&) const {
  // One Workspace today (M2); the records carry the id for when there are more.
  aerie_tab_model::TabModel* model = host_->Model();
  return model ? base::UTF8ToUTF16(model->workspace().name) : std::u16string();
}

std::string AerieSidebarArchiveController::ArchiveAfter() const {
  const std::string value =
      host_->Prefs()->GetString(aerie_tab_model::kArchiveAfter);
  return value.empty() ? aerie_tab_model::kDefaultArchiveAfter : value;
}

void AerieSidebarArchiveController::SetArchiveAfter(const std::string& value) {
  host_->Prefs()->SetString(aerie_tab_model::kArchiveAfter, value);
}

base::Time AerieSidebarArchiveController::Now() const {
  return base::Time::Now();
}

const PrefService* AerieSidebarArchiveController::Prefs() const {
  return host_->Prefs();
}

void AerieSidebarArchiveController::RestoreArchived(
    const aerie_tab_model::ArchivedRow& row,
    const gfx::Rect& row_in_screen) {
  Attach();
  if (!archiver_) {
    return;
  }
  const std::u16string title =
      base::UTF8ToUTF16(row.title.empty() ? row.url : row.title);
  if (!archiver_->Restore(row.id, host_->Browser())) {
    return;
  }
  host_->SidebarView()->GetViewAccessibility().AnnounceText(
      l10n_util::GetStringFUTF16(IDS_AERIE_SIDEBAR_ARCHIVE_RESTORED, title));
  if (!host_->CanAnimate()) {
    return;
  }
  views::View* sidebar = host_->SidebarView();
  const gfx::Rect slot = host_->TodayTopSlot();
  AerieSidebarDragContent content;
  content.title = title;
  content.icon = ui::ImageModel::FromVectorIcon(
      vector_icons::kGlobeIcon, host_->Palette().text_muted,
      aerie::tokens::kSizeIcon);
  const std::string key = "restore:" + row.id;
  Flight* flight = StartFlight(key, content, slot);
  const PrefService* prefs = host_->Prefs();
  const bool reduced = aerie::motion::ShouldReduceMotion(prefs);
  const aerie_sidebar::ArchiveRestoreMotion plan =
      aerie_sidebar::RestoreTabMotion(reduced);
  aerie::motion::LayerMotion target;
  if (plan.animate_transform) {
    const gfx::Point from = ToSidebar(sidebar, row_in_screen.origin());
    flight->ghost->layer()->SetTransform(gfx::Transform::MakeTranslation(
        from.x() - slot.x(), from.y() - slot.y()));
    target.transform = gfx::Transform();
  } else {
    target.opacity = 0.f;
  }
  flight->motion = aerie::motion::AnimateLayer(
      flight->ghost->layer(), target, plan.duration, plan.easing, prefs);
  flight->timer.Start(
      FROM_HERE, base::Milliseconds(plan.duration_ms + kLandingSlackMs),
      base::BindOnce(&AerieSidebarArchiveController::EndFlight,
                     weak_factory_.GetWeakPtr(), key));
}

void AerieSidebarArchiveController::OnArchivePanelClosed(bool refocus_button) {
  panel_.reset();
  if (refocus_button && host_->ArchiveButton()) {
    host_->ArchiveButton()->RequestFocus();
  }
}
