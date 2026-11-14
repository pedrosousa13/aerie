// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ARCHIVE_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ARCHIVE_CONTROLLER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/aerie_tab_model/tab_archiver.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_archive_panel.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_drag_overlay.h"
#include "components/aerie_design_tokens/aerie_motion.h"
#include "components/aerie_sidebar/aerie_sidebar_palette.h"
#include "ui/gfx/geometry/rect.h"

namespace ui {
class Layer;
}  // namespace ui

class AerieSidebarArchiveButton;
class BrowserWindowInterface;
class PrefService;

namespace aerie_tab_model {
class TabModel;
}  // namespace aerie_tab_model

namespace views {
class View;
}  // namespace views

// The sidebar's side of the Archive (issue #183): the flights of the copies, the
// button's pulse, and the panel. The when and the how of archiving are
// aerie_tab_model::TabArchiver; this class only draws what it reports.
//
//   Archive moment. When the archiver is about to close a tab, a copy of its
//   row is taken (OnTabArchiving). When the tab is archived, the copy shrinks to
//   `motion-scale-archive-collapse` and flies to the button over
//   `archive-collapse` on `accelerate` while the real row collapses; where it
//   lands the button pulses. Reduced motion: the copy fades in place for 120 ms,
//   the button flashes colour with no scale.
//
//   Restore. A copy flies from the panel row to the top of Today over
//   `archive-restore` on the spring. Reduced motion: it fades.
class AerieSidebarArchiveController
    : public aerie_tab_model::TabArchiver::Observer,
      public AerieSidebarArchivePanel::Host {
 public:
  // What the view gives the controller. Everything is in the sidebar's
  // coordinates unless it says otherwise.
  class ViewHost {
   public:
    virtual views::View* SidebarView() = 0;
    virtual AerieSidebarArchiveButton* ArchiveButton() = 0;
    virtual const aerie_sidebar::SidebarPalette& Palette() const = 0;
    virtual bool Dark() const = 0;
    virtual PrefService* Prefs() const = 0;
    virtual BrowserWindowInterface* Browser() const = 0;
    virtual aerie_tab_model::TabModel* Model() const = 0;
    // True when rows move (the window is shown, no session is restoring).
    virtual bool CanAnimate() const = 0;
    // What the row of `id` shows and where it is; empty when it has no row.
    virtual std::optional<AerieSidebarDragContent> RowContent(
        const std::string& id) const = 0;
    virtual std::optional<gfx::Rect> RowBounds(const std::string& id) const = 0;
    // Where the first row of Today is, or would be.
    virtual gfx::Rect TodayTopSlot() const = 0;

   protected:
    ~ViewHost() = default;
  };

  explicit AerieSidebarArchiveController(ViewHost* host);
  AerieSidebarArchiveController(const AerieSidebarArchiveController&) = delete;
  AerieSidebarArchiveController& operator=(
      const AerieSidebarArchiveController&) = delete;
  ~AerieSidebarArchiveController() override;

  // Builds the archiver if it does not exist, and starts to listen. The view
  // calls this a few seconds after the window opens (no startup cost).
  void Attach();

  // Opens the panel, or closes it when it is open.
  void TogglePanel();
  AerieSidebarArchivePanel* panel() const { return panel_.get(); }
  // True while a copy is in the air, for tests.
  size_t flights_in_progress() const { return flights_.size(); }
  // The copy that flies for `key` ("archive:<record id>" or "restore:<record
  // id>"), its animation and its layer; null when none is in the air.
  const aerie::motion::MotionHandle* FlightMotion(const std::string& key) const;
  ui::Layer* FlightLayer(const std::string& key) const;
  // What the last restore announced to a screen reader, for tests.
  const std::u16string& last_announcement() const { return last_announcement_; }
  void Detach();

  // aerie_tab_model::TabArchiver::Observer:
  void OnTabArchiving(const std::string& record_id) override;
  void OnTabArchived(const std::string& record_id, bool animate) override;
  void OnTabArchiveFailed(const std::string& record_id) override;

  // AerieSidebarArchivePanel::Host:
  std::vector<aerie_tab_model::ArchivedRow> ArchivedRows() const override;
  std::u16string WorkspaceName(const std::string& workspace_id) const override;
  std::string ArchiveAfter() const override;
  void SetArchiveAfter(const std::string& value) override;
  base::Time Now() const override;
  const PrefService* Prefs() const override;
  void RestoreArchived(const aerie_tab_model::ArchivedRow& row,
                       const gfx::Rect& row_in_screen) override;
  void OnArchivePanelClosed(bool refocus_button) override;

 private:
  struct Flight {
    Flight();
    ~Flight();
    raw_ptr<AerieSidebarDragGhost> ghost = nullptr;
    aerie::motion::MotionHandle motion;
    base::OneShotTimer timer;
    bool pulse_on_landing = false;
  };

  void EndFlight(const std::string& key);
  Flight* StartFlight(const std::string& key,
                      const AerieSidebarDragContent& content,
                      const gfx::Rect& rect);

  const raw_ptr<ViewHost> host_;
  raw_ptr<aerie_tab_model::TabArchiver> archiver_ = nullptr;
  base::ScopedObservation<aerie_tab_model::TabArchiver,
                          aerie_tab_model::TabArchiver::Observer>
      observation_{this};
  std::map<std::string, std::unique_ptr<Flight>> flights_;
  base::WeakPtr<AerieSidebarArchivePanel> panel_;
  std::u16string last_announcement_;
  base::WeakPtrFactory<AerieSidebarArchiveController> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_ARCHIVE_CONTROLLER_H_
