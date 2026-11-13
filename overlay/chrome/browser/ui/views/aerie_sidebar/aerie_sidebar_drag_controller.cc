// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_controller.h"

#include <algorithm>
#include <map>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "components/aerie_design_tokens/aerie_tokens.h"
#include "components/aerie_sidebar/aerie_sidebar_motion.h"
#include "components/aerie_tab_model/tab_model.h"
#include "ui/base/cursor/mojom/cursor_type.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/vector2d_conversions.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"

namespace {

using aerie_sidebar::DropKind;
using aerie_sidebar::DropTarget;
using aerie_tab_model::Section;

// How far an edge of a list scrolls the list while a drag rests there, and how
// often. The brief gives the 24 px edge; these two are not design values.
constexpr int kAutoScrollEdge = 24;
constexpr int kAutoScrollStepPx = 6;
constexpr int kAutoScrollIntervalMs = 16;

bool SameTarget(const DropTarget& a, const DropTarget& b) {
  return a.kind == b.kind && a.section == b.section &&
         a.container == b.container && a.index == b.index &&
         a.anchor_id == b.anchor_id && a.reason == b.reason;
}

int Spacing(const std::vector<AerieSidebarDragController::MeasuredRow>& rows) {
  return rows.size() > 1 ? rows[1].rect.y() - rows[0].rect.bottom()
                         : static_cast<int>(aerie::tokens::kSpace4);
}

const AerieSidebarDragController::MeasuredRow* FindRow(
    const std::vector<AerieSidebarDragController::MeasuredRow>& rows,
    const std::string& id) {
  for (const auto& row : rows) {
    if (row.tree.id == id) {
      return &row;
    }
  }
  return nullptr;
}

}  // namespace

AerieSidebarDragController::AerieSidebarDragController(Host* host)
    : host_(host) {}

AerieSidebarDragController::~AerieSidebarDragController() {
  scroll_timer_.Stop();
  settle_timer_.Stop();
  ghost_motion_.Abort();
  RemoveGhost();
  if (indicator_ && host_->SidebarView()) {
    host_->SidebarView()->RemoveChildViewT(indicator_.get());
    indicator_ = nullptr;
  }
}

void AerieSidebarDragController::EnsureOverlay() {
  if (!indicator_) {
    indicator_ = host_->SidebarView()->AddChildView(
        std::make_unique<AerieSidebarDropIndicator>(host_->Palette(),
                                                    host_->Dark()));
  } else {
    // The theme may have changed since the last drag.
    indicator_->SetColours(host_->Palette(), host_->Dark());
  }
}

void AerieSidebarDragController::RefreshColours() {
  if (indicator_) {
    indicator_->SetColours(host_->Palette(), host_->Dark());
  }
  if (ghost_) {
    ghost_->SetColours(host_->Palette(), host_->Dark());
  }
}

void AerieSidebarDragController::RemoveGhost() {
  if (ghost_) {
    host_->SidebarView()->RemoveChildViewT(ghost_.get());
    ghost_ = nullptr;
  }
}

aerie_sidebar::DropGeometry AerieSidebarDragController::BuildGeometry(
    const MeasuredLayout& layout) const {
  aerie_sidebar::DropGeometry geometry;
  auto fill = [](Section section, const std::vector<MeasuredRow>& rows,
                 const gfx::Rect& viewport, aerie_sidebar::DropList* list) {
    list->section = section;
    list->top = viewport.y();
    list->bottom = viewport.bottom();
    for (const MeasuredRow& row : rows) {
      if (row.rect.bottom() <= viewport.y() || row.rect.y() >= viewport.bottom()) {
        continue;
      }
      list->rows.push_back({row.tree, row.rect.y(), row.rect.height()});
    }
  };
  fill(Section::kPinned, layout.pinned, layout.pinned_viewport,
       &geometry.pinned);
  fill(Section::kToday, layout.today, layout.today_viewport, &geometry.today);
  auto to_rect = [](const gfx::Rect& r) {
    return aerie_sidebar::DropRect{r.x(), r.y(), r.width(), r.height()};
  };
  geometry.favorites_divider = to_rect(layout.favorites_divider);
  geometry.pinned_divider = to_rect(layout.pinned_divider);
  geometry.favorites_area = {layout.favorites_area.x(), layout.favorites_area.y(),
                             layout.favorites_area.width(),
                             layout.favorites_area.height()};
  for (const auto& [id, rect] : layout.tiles) {
    geometry.tiles.push_back({rect.x(), rect.y(), rect.width(), rect.height()});
    geometry.tile_ids.push_back(id);
  }
  return geometry;
}

gfx::Transform AerieSidebarDragController::LiftTransform(float scale) const {
  const float cx = ghost_->width() / 2.f;
  const float cy = ghost_->height() / 2.f;
  gfx::Transform transform;
  transform.Translate(cx, cy);
  transform.Scale(scale, scale);
  transform.Translate(-cx, -cy);
  return transform;
}

bool AerieSidebarDragController::Begin(const std::string& id, bool is_folder) {
  if (active_ || !host_->CanBeginDrag()) {
    return false;
  }
  // A drag that starts on the row that is still settling picks the settling
  // layer up where it is (one animation per layer); any other settle ends.
  const bool resume = settling_ && ghost_ && id == id_;
  if (!resume) {
    FinishSettle();
  }
  EnsureOverlay();
  gfx::Rect rect;
  AerieSidebarDragContent content;
  if (!host_->RectFor(id, &rect) || !host_->DragContentFor(id, &content)) {
    return false;
  }
  const MeasuredLayout layout = host_->Measure();
  block_ids_.clear();
  block_height_ = 0;
  source_is_tile_ = false;
  bool found = false;
  for (Section section : {Section::kPinned, Section::kToday}) {
    const auto& rows = section == Section::kPinned ? layout.pinned : layout.today;
    for (size_t i = 0; i < rows.size() && !found; ++i) {
      if (rows[i].tree.id != id) {
        continue;
      }
      found = true;
      source_section_ = section;
      size_t last = i;
      while (is_folder && last + 1 < rows.size() &&
             rows[last + 1].tree.level > rows[i].tree.level) {
        ++last;
      }
      for (size_t k = i; k <= last; ++k) {
        block_ids_.push_back(rows[k].tree.id);
      }
      block_height_ = rows[last].rect.bottom() - rows[i].rect.y() + Spacing(rows);
    }
  }
  if (!found) {
    for (const auto& [tile_id, tile_rect] : layout.tiles) {
      if (tile_id == id) {
        found = true;
        source_is_tile_ = true;
        source_section_ = Section::kFavorite;
        block_ids_.push_back(id);
      }
    }
  }
  if (!found) {
    return false;
  }
  id_ = id;
  is_folder_ = is_folder;
  source_rect_ = rect;
  grab_ = gfx::Vector2d();
  grab_set_ = false;
  target_ = DropTarget();
  target_valid_ = false;
  refusal_shown_ = false;
  shift_by_id_.clear();

  resume_shift_ = gfx::Vector2d();
  float resume_scale = 1.f;
  if (resume) {
    // Take the layer from where the settle has brought it: the scale it has
    // now, and how far its centre is from the place it lands on.
    const gfx::Transform now = ghost_->layer()->transform();
    resume_scale = now.To2dScale().x();
    const gfx::Vector2dF t = now.To2dTranslation();
    const float cx = ghost_->width() / 2.f;
    const float cy = ghost_->height() / 2.f;
    resume_shift_ = gfx::ToRoundedVector2d(
        gfx::Vector2dF(t.x() - cx * (1.f - resume_scale),
                       t.y() - cy * (1.f - resume_scale)));
    ghost_motion_.Abort();
    settle_timer_.Stop();
    settling_ = false;
    settle_ids_.clear();
    gfx::Rect at = ghost_->content_bounds();
    at.Offset(resume_shift_);
    ghost_->SetContentBounds(at);
    ghost_->layer()->SetTransform(LiftTransform(resume_scale));
  } else {
    ghost_ = host_->SidebarView()->AddChildView(
        std::make_unique<AerieSidebarDragGhost>(content, host_->Palette(),
                                                host_->Dark()));
    ghost_->SetContentBounds(rect);
  }
  for (const std::string& hidden : block_ids_) {
    host_->SetDragHidden(hidden, true);
  }
  const bool reduced = aerie::motion::ShouldReduceMotion(host_->Prefs());
  const aerie_sidebar::DragMotion motion = aerie_sidebar::RowDragMotion(reduced);
  if (motion.animate_lift) {
    aerie::motion::LayerMotion lifted;
    lifted.transform = LiftTransform(motion.lift_scale);
    ghost_motion_.Abort();
    ghost_motion_ = aerie::motion::AnimateLayer(
        ghost_->layer(), lifted, motion.lift_duration, motion.lift_easing,
        host_->Prefs());
  }
  active_ = true;
  host_->SetDragCursor(id_, ui::Cursor(ui::mojom::CursorType::kGrabbing));
  return true;
}

void AerieSidebarDragController::PlaceGhost(const gfx::Point& p) {
  if (!ghost_) {
    return;
  }
  if (!grab_set_) {
    grab_ = p - (source_rect_.origin() + resume_shift_);
    grab_set_ = true;
  }
  gfx::Rect rect = source_rect_;
  rect.set_y(p.y() - grab_.y());
  if (!source_is_tile_) {
    rect.set_x(source_rect_.x() + resume_shift_.x());
  }
  if (source_is_tile_) {
    rect.set_x(p.x() - grab_.x());
  }
  const gfx::Rect bounds = host_->SidebarView()->GetLocalBounds();
  rect.set_y(std::clamp(rect.y(), bounds.y(),
                        std::max(bounds.y(), bounds.bottom() - rect.height())));
  if (source_is_tile_) {
    rect.set_x(std::clamp(rect.x(), bounds.x(),
                          std::max(bounds.x(), bounds.right() - rect.width())));
  }
  // The pointer moves the bounds. That is a property set, not an animation, so
  // the dragged row tracks the pointer under reduced motion as well.
  ghost_->SetContentBounds(rect);
}

void AerieSidebarDragController::Update(const gfx::Point& widget_point) {
  if (!active_) {
    return;
  }
  views::View* sidebar = host_->SidebarView();
  gfx::Point p = widget_point;
  views::View::ConvertPointFromWidget(sidebar, &p);
  last_point_ = p;
  Reevaluate();
}

void AerieSidebarDragController::Reevaluate() {
  const gfx::Point p = last_point_;
  PlaceGhost(p);
  const MeasuredLayout layout = host_->Measure();
  const bool inside = host_->SidebarView()->GetLocalBounds().Contains(p);
  DropTarget target;
  if (inside) {
    aerie_sidebar::DragItem item;
    item.kind = is_folder_ ? aerie_tab_model::ItemKind::kFolder
                           : aerie_tab_model::ItemKind::kTab;
    item.id = id_;
    target = aerie_sidebar::ComputeDropTarget(*host_->Model(), p.x(), p.y(),
                                              BuildGeometry(layout), item);
  }
  const bool changed = !target_valid_ || !SameTarget(target, target_);
  target_ = target;
  target_valid_ = true;
  if (changed) {
    ShowFeedback(layout);
  }
  UpdateAutoScroll(p, layout);
}

bool AerieSidebarDragController::InBlock(const std::string& id) const {
  return std::find(block_ids_.begin(), block_ids_.end(), id) !=
         block_ids_.end();
}

std::map<std::string, int> AerieSidebarDragController::ComputeShifts(
    const MeasuredLayout& layout,
    int* insertion_index) const {
  std::map<std::string, int> shifts;
  *insertion_index = -1;
  const bool places = target_.kind == DropKind::kBefore ||
                      target_.kind == DropKind::kAfter ||
                      target_.kind == DropKind::kFirstChild;
  if (!places || target_.section == Section::kFavorite) {
    return shifts;
  }
  const auto& target_rows =
      target_.section == Section::kPinned ? layout.pinned : layout.today;
  int t = 0;
  if (!target_.anchor_id.empty()) {
    for (size_t i = 0; i < target_rows.size(); ++i) {
      if (target_rows[i].tree.id == target_.anchor_id) {
        t = static_cast<int>(i) + (target_.kind == DropKind::kBefore ? 0 : 1);
      }
    }
  }
  *insertion_index = t;
  if (aerie::motion::ShouldReduceMotion(host_->Prefs())) {
    return shifts;
  }
  int height = block_height_;
  if (height == 0 && !target_rows.empty()) {
    height = target_rows[0].rect.height() + Spacing(target_rows);
  }
  for (Section section : {Section::kPinned, Section::kToday}) {
    const auto& rows = section == Section::kPinned ? layout.pinned : layout.today;
    int last_block = -1;
    for (size_t i = 0; i < rows.size(); ++i) {
      if (InBlock(rows[i].tree.id)) {
        last_block = static_cast<int>(i);
      }
    }
    for (size_t i = 0; i < rows.size(); ++i) {
      if (InBlock(rows[i].tree.id)) {
        continue;
      }
      int shift = 0;
      if (last_block >= 0 && static_cast<int>(i) > last_block) {
        shift -= height;
      }
      if (section == target_.section && static_cast<int>(i) >= t) {
        shift += height;
      }
      if (shift != 0) {
        shifts[rows[i].tree.id] = shift;
      }
    }
  }
  return shifts;
}

void AerieSidebarDragController::ShowFeedback(const MeasuredLayout& layout) {
  int insertion = -1;
  const std::map<std::string, int> wanted = ComputeShifts(layout, &insertion);
  const bool refused = target_.kind == DropKind::kRefused;
  // Rows: new shifts, and the ones that no longer shift go back.
  for (const auto& [id, dy] : wanted) {
    auto it = shift_by_id_.find(id);
    if (it == shift_by_id_.end() || it->second != dy) {
      host_->ShiftRow(id, dy);
    }
  }
  for (const auto& [id, dy] : shift_by_id_) {
    if (!wanted.count(id)) {
      host_->ShiftRow(id, 0);
    }
  }
  shift_by_id_ = wanted;

  host_->SetDragCursor(
      id_, ui::Cursor(refused ? ui::mojom::CursorType::kNotAllowed
                              : ui::mojom::CursorType::kGrabbing));
  if (refused) {
    host_->ShowRefusal(target_.reason, /*on_drop=*/false);
    refusal_shown_ = true;
  } else if (refusal_shown_) {
    host_->HideRefusal();
    refusal_shown_ = false;
  }

  if (target_.on_divider && target_.kind != DropKind::kNone) {
    // Over a divider: it lights up (or goes red) and no row shifts.
    const gfx::Rect divider = target_.section == Section::kFavorite
                                  ? layout.favorites_divider
                                  : layout.pinned_divider;
    indicator_->Show(refused ? AerieSidebarDropIndicator::Mode::kRefused
                             : AerieSidebarDropIndicator::Mode::kLine,
                     gfx::Rect(divider.x(),
                               divider.CenterPoint().y() - kDropMarkHalf,
                               divider.width(), kDropMarkThickness));
    return;
  }

  const auto& rows =
      target_.section == Section::kPinned ? layout.pinned : layout.today;
  auto shift_of = [&](const std::string& id) {
    auto it = shift_by_id_.find(id);
    return it == shift_by_id_.end() ? 0 : it->second;
  };
  switch (target_.kind) {
    case DropKind::kNone:
      indicator_->Hide();
      return;
    case DropKind::kRefused: {
      gfx::Rect rect = layout.favorites_area;
      if (const MeasuredRow* anchor = FindRow(layout.pinned, target_.anchor_id)) {
        rect = anchor->rect;
      } else if (const MeasuredRow* a2 = FindRow(layout.today, target_.anchor_id)) {
        rect = a2->rect;
      }
      if (rect.IsEmpty()) {
        indicator_->Hide();
      } else {
        indicator_->Show(AerieSidebarDropIndicator::Mode::kRefused, rect);
      }
      return;
    }
    case DropKind::kInto: {
      const MeasuredRow* anchor = FindRow(rows, target_.anchor_id);
      if (anchor) {
        indicator_->Show(AerieSidebarDropIndicator::Mode::kOutline, anchor->rect);
      } else {
        indicator_->Hide();
      }
      return;
    }
    case DropKind::kFavoritesSlot: {
      gfx::Rect bar(layout.favorites_area.x() + kDropMarkThickness,
                    layout.favorites_area.y(), kDropMarkThickness,
                    layout.favorites_area.height());
      int index = 0;
      for (const auto& [tile_id, rect] : layout.tiles) {
        if (tile_id == id_) {
          continue;
        }
        if (index == target_.index) {
          bar = gfx::Rect(rect.x() - kDropMarkThickness - kDropMarkGap,
                          rect.y(), kDropMarkThickness, rect.height());
          break;
        }
        bar = gfx::Rect(rect.right() + kDropMarkGap, rect.y(),
                        kDropMarkThickness, rect.height());
        ++index;
      }
      indicator_->Show(AerieSidebarDropIndicator::Mode::kLine, bar);
      return;
    }
    case DropKind::kBefore:
    case DropKind::kAfter:
    case DropKind::kFirstChild: {
      const gfx::Rect viewport = target_.section == Section::kPinned
                                     ? layout.pinned_viewport
                                     : layout.today_viewport;
      const int spacing = Spacing(rows);
      int y = viewport.y() + kDropMarkThickness;
      gfx::Rect base = viewport;
      int p = insertion - 1;
      while (p >= 0 && InBlock(rows[p].tree.id)) {
        --p;
      }
      int n = insertion;
      while (n >= 0 && n < static_cast<int>(rows.size()) &&
             InBlock(rows[n].tree.id)) {
        ++n;
      }
      if (p >= 0) {
        y = rows[p].rect.bottom() + shift_of(rows[p].tree.id) +
            spacing / 2 - kDropMarkHalf;
        base = rows[p].rect;
      } else if (n >= 0 && n < static_cast<int>(rows.size())) {
        y = rows[n].rect.y() + shift_of(rows[n].tree.id) -
            spacing / 2 - kDropMarkHalf;
        base = rows[n].rect;
      }
      int level = 0;
      if (!target_.container.empty()) {
        if (const MeasuredRow* c = FindRow(rows, target_.container)) {
          level = c->tree.level + 1;
        }
      }
      const int indent = level * aerie::tokens::kSizeIndent +
                         static_cast<int>(aerie::tokens::kSpace8);
      indicator_->Show(AerieSidebarDropIndicator::Mode::kLine,
                       gfx::Rect(base.x() + indent, y,
                                 std::max(0, base.width() - indent -
                                                 static_cast<int>(
                                                     aerie::tokens::kSpace8)),
                                 kDropMarkThickness));
      return;
    }
  }
}

void AerieSidebarDragController::ClearFeedback() {
  if (indicator_) {
    indicator_->Hide();
  }
  if (refusal_shown_) {
    host_->HideRefusal();
    refusal_shown_ = false;
  }
}

void AerieSidebarDragController::ResetShifts() {
  for (const auto& [id, dy] : shift_by_id_) {
    host_->ResetRowShift(id);
  }
  shift_by_id_.clear();
}

void AerieSidebarDragController::UpdateAutoScroll(
    const gfx::Point& p,
    const MeasuredLayout& layout) {
  int dy = 0;
  Section section = Section::kToday;
  const std::pair<const gfx::Rect*, Section> lists[] = {
      {&layout.pinned_viewport, Section::kPinned},
      {&layout.today_viewport, Section::kToday}};
  for (const auto& [viewport, which] : lists) {
    if (viewport->IsEmpty() || p.x() < viewport->x() ||
        p.x() >= viewport->right()) {
      continue;
    }
    if (p.y() >= viewport->y() && p.y() < viewport->y() + kAutoScrollEdge) {
      dy = -kAutoScrollStepPx;
      section = which;
    } else if (p.y() <= viewport->bottom() &&
               p.y() > viewport->bottom() - kAutoScrollEdge) {
      dy = kAutoScrollStepPx;
      section = which;
    }
  }
  scroll_dy_ = dy;
  scroll_section_ = section;
  if (dy == 0) {
    scroll_timer_.Stop();
  } else if (!scroll_timer_.IsRunning()) {
    scroll_timer_.Start(
        FROM_HERE, base::Milliseconds(kAutoScrollIntervalMs),
        base::BindRepeating(&AerieSidebarDragController::TickAutoScroll,
                            base::Unretained(this)));
  }
}

void AerieSidebarDragController::TickAutoScroll() {
  if (!active_ || scroll_dy_ == 0) {
    scroll_timer_.Stop();
    return;
  }
  if (host_->ScrollList(scroll_section_, scroll_dy_)) {
    Reevaluate();
  }
}

void AerieSidebarDragController::Unhide() {
  for (const std::string& id : block_ids_) {
    host_->SetDragHidden(id, false);
  }
}

void AerieSidebarDragController::End(const gfx::Point& widget_point,
                                     bool cancelled,
                                     bool outside) {
  if (!active_) {
    return;
  }
  scroll_timer_.Stop();
  active_ = false;
  host_->SetDragCursor(id_, ui::Cursor());
  ResetShifts();
  if (indicator_) {
    indicator_->Hide();
  }
  // A release outside the window drops nowhere, whatever the last move showed.
  const DropTarget target = (outside && !cancelled) ? DropTarget() : target_;
  auto settle_back = [&] {
    gfx::Rect dest = source_rect_;
    host_->RectFor(id_, &dest);
    SettleTo(dest);
  };

  if (cancelled || target.kind == DropKind::kNone) {
    if (!cancelled && outside && !is_folder_ && !source_is_tile_) {
      Unhide();
      RemoveGhost();
      HideRefusalIfShown();
      host_->MoveToNewWindow(id_);
      return;
    }
    HideRefusalIfShown();
    settle_back();
    return;
  }
  if (target.kind == DropKind::kRefused) {
    host_->ShowRefusal(target.reason, /*on_drop=*/true);
    refusal_shown_ = false;
    settle_back();
    return;
  }
  HideRefusalIfShown();
  aerie_sidebar::MoveRequest request;
  request.kind = is_folder_ ? aerie_tab_model::ItemKind::kFolder
                            : aerie_tab_model::ItemKind::kTab;
  request.id = id_;
  request.section = target.section;
  request.container = target.container;
  request.index = target.index;
  const aerie_sidebar::MoveResult result = host_->ApplyMove(request);
  switch (result) {
    case aerie_sidebar::MoveResult::kMoved: {
      // The rows of the new places are new views; keep them out of sight until
      // the drag layer lands on them.
      for (const std::string& id : block_ids_) {
        host_->SetDragHidden(id, true);
      }
      gfx::Rect dest = source_rect_;
      if (!host_->RectFor(id_, &dest) && !target.container.empty()) {
        // Into a shut folder: the layer lands on the folder's row.
        host_->RectFor(target.container, &dest);
      }
      SettleTo(dest);
      return;
    }
    case aerie_sidebar::MoveResult::kRefusedFavoritesFull:
      host_->ShowRefusal(aerie_sidebar::RefusalReason::kFavoritesFull,
                         /*on_drop=*/true);
      break;
    case aerie_sidebar::MoveResult::kRefusedTooDeep:
      host_->ShowRefusal(aerie_sidebar::RefusalReason::kTooDeep,
                         /*on_drop=*/true);
      break;
    case aerie_sidebar::MoveResult::kUnchanged:
    case aerie_sidebar::MoveResult::kInvalid:
      break;
  }
  settle_back();
}

void AerieSidebarDragController::HideRefusalIfShown() {
  if (refusal_shown_) {
    host_->HideRefusal();
    refusal_shown_ = false;
  }
}

void AerieSidebarDragController::SettleTo(const gfx::Rect& dest) {
  const bool reduced = aerie::motion::ShouldReduceMotion(host_->Prefs());
  const aerie_sidebar::DragMotion motion = aerie_sidebar::RowDragMotion(reduced);
  if (!ghost_ || !motion.animate_settle) {
    // The drop is instant: nothing is left to wait for.
    Unhide();
    RemoveGhost();
    return;
  }
  const gfx::Rect current = ghost_->content_bounds();
  ghost_motion_.Abort();
  ghost_->SetContentBounds(dest);
  gfx::Transform start = gfx::Transform::MakeTranslation(
      current.x() - dest.x(), current.y() - dest.y());
  if (motion.animate_lift) {
    start.PreConcat(LiftTransform(motion.lift_scale));
  }
  ghost_->layer()->SetTransform(start);
  aerie::motion::LayerMotion rest;
  rest.transform = gfx::Transform();
  ghost_motion_ = aerie::motion::AnimateLayerForMs(
      ghost_->layer(), rest, motion.settle_duration_ms,
      motion.settle_reduced_duration_ms, motion.settle_easing, host_->Prefs());
  settling_ = true;
  settle_ids_ = block_ids_;
  settle_timer_.Start(
      FROM_HERE,
      base::Milliseconds(motion.settle_duration_ms + kSettleSlackMs),
      base::BindOnce(&AerieSidebarDragController::FinishSettle,
                     base::Unretained(this)));
}

void AerieSidebarDragController::FinishSettle() {
  if (!settling_) {
    return;
  }
  settling_ = false;
  settle_timer_.Stop();
  ghost_motion_.Abort();
  RemoveGhost();
  for (const std::string& id : settle_ids_) {
    host_->SetDragHidden(id, false);
  }
  settle_ids_.clear();
}

void AerieSidebarDragController::Cancel() {
  scroll_timer_.Stop();
  if (active_) {
    active_ = false;
    host_->SetDragCursor(id_, ui::Cursor());
    ResetShifts();
    ClearFeedback();
    ghost_motion_.Abort();
    Unhide();
    RemoveGhost();
    target_ = DropTarget();
    target_valid_ = false;
    block_ids_.clear();
    return;
  }
  FinishSettle();
}
