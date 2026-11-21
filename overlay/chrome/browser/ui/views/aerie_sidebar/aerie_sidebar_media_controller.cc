// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_media_controller.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/browser/ui/views/aerie_sidebar/aerie_sidebar_media_card.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/media_session/public/cpp/media_metadata.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "url/gurl.h"

namespace {

// The host of `contents`' page without a leading "www.", for the line under the
// title. A page without a host (a file, an internal page) shows its scheme.
std::u16string SiteOf(content::WebContents* contents) {
  const GURL& url = contents->GetLastCommittedURL();
  std::string host(url.host());
  if (base::StartsWith(host, "www.")) {
    host.erase(0, 4);
  }
  if (host.empty()) {
    host = std::string(url.scheme());
  }
  return base::UTF8ToUTF16(host);
}

}  // namespace

// What one tab's media session says, kept up to date from the session's own
// observer notifications.
class AerieSidebarMediaController::TabMedia
    : public content::WebContentsObserver,
      public media_session::mojom::MediaSessionObserver {
 public:
  TabMedia(AerieSidebarMediaController* owner, content::WebContents* contents)
      : content::WebContentsObserver(contents), owner_(owner) {
    if (content::MediaSession* session =
            content::MediaSession::GetIfExists(contents)) {
      Attach(session);
    }
  }
  TabMedia(const TabMedia&) = delete;
  TabMedia& operator=(const TabMedia&) = delete;
  ~TabMedia() override = default;

  content::WebContents* contents() const { return web_contents(); }
  // The page still holds a session that the user may control: it plays or is
  // paused, and is not a short sound.
  bool holds_session() const {
    return web_contents() && controllable_ && !inactive_;
  }
  bool playing() const { return holds_session() && playing_; }
  bool next_enabled() const { return next_enabled_; }
  int started_at() const { return started_at_; }

  std::u16string Title() const {
    if (metadata_ && !metadata_->title.empty()) {
      return metadata_->title;
    }
    return web_contents() ? web_contents()->GetTitle() : std::u16string();
  }

  // content::WebContentsObserver:
  void MediaSessionCreated(content::MediaSession* session) override {
    Attach(session);
  }
  void TitleWasSet(content::NavigationEntry*) override { Changed(); }
  void PrimaryPageChanged(content::Page&) override { Changed(); }
  void WebContentsDestroyed() override {
    receiver_.reset();
    Observe(nullptr);
    Changed();
  }

  // media_session::mojom::MediaSessionObserver:
  void MediaSessionInfoChanged(
      media_session::mojom::MediaSessionInfoPtr info) override {
    const bool was_playing = playing();
    controllable_ = info && info->is_controllable;
    inactive_ = !info || info->state ==
                             media_session::mojom::MediaSessionInfo::
                                 SessionState::kInactive;
    playing_ = info && info->playback_state ==
                           media_session::mojom::MediaPlaybackState::kPlaying;
    if (playing() && !was_playing) {
      started_at_ = ++owner_->play_clock_;
    }
    Changed();
  }
  void MediaSessionMetadataChanged(
      const std::optional<media_session::MediaMetadata>& metadata) override {
    metadata_ = metadata;
    Changed();
  }
  void MediaSessionActionsChanged(
      const std::vector<media_session::mojom::MediaSessionAction>& actions)
      override {
    next_enabled_ = std::ranges::find(
                        actions,
                        media_session::mojom::MediaSessionAction::kNextTrack) !=
                    actions.end();
    Changed();
  }
  void MediaSessionImagesChanged(
      const base::flat_map<media_session::mojom::MediaSessionImageType,
                           std::vector<media_session::MediaImage>>& images)
      override {}
  void MediaSessionPositionChanged(
      const std::optional<media_session::MediaPosition>& position) override {}

 private:
  void Attach(content::MediaSession* session) {
    receiver_.reset();
    session->AddObserver(receiver_.BindNewPipeAndPassRemote());
    receiver_.set_disconnect_handler(base::BindOnce(
        &TabMedia::OnDisconnected, base::Unretained(this)));
  }
  void OnDisconnected() {
    receiver_.reset();
    inactive_ = true;
    playing_ = false;
    Changed();
  }
  void Changed() { owner_->OnTabMediaChanged(); }

  const raw_ptr<AerieSidebarMediaController> owner_;
  mojo::Receiver<media_session::mojom::MediaSessionObserver> receiver_{this};
  std::optional<media_session::MediaMetadata> metadata_;
  bool controllable_ = false;
  bool inactive_ = true;
  bool playing_ = false;
  bool next_enabled_ = false;
  int started_at_ = 0;
};

AerieSidebarMediaController::AerieSidebarMediaController(
    BrowserWindowInterface* browser,
    AerieSidebarMediaCard* card)
    : browser_(browser), card_(card) {
  browser_->GetTabStripModel()->AddObserver(this);
  SyncTabs();
}

AerieSidebarMediaController::~AerieSidebarMediaController() {
  browser_->GetTabStripModel()->RemoveObserver(this);
}

content::WebContents* AerieSidebarMediaController::shown_contents() const {
  return shown_ ? shown_->contents() : nullptr;
}

void AerieSidebarMediaController::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange&,
    const TabStripSelectionChange&) {
  SyncTabs();
}

void AerieSidebarMediaController::SyncTabs() {
  TabStripModel* model = browser_->GetTabStripModel();
  std::erase_if(tabs_, [&](const std::unique_ptr<TabMedia>& media) {
    const bool gone =
        !media->contents() ||
        model->GetIndexOfWebContents(media->contents()) ==
            TabStripModel::kNoTab;
    if (gone && shown_ == media.get()) {
      shown_ = nullptr;
    }
    return gone;
  });
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (!contents ||
        std::ranges::any_of(tabs_, [&](const std::unique_ptr<TabMedia>& m) {
          return m->contents() == contents;
        })) {
      continue;
    }
    tabs_.push_back(std::make_unique<TabMedia>(this, contents));
  }
  Refresh();
}

AerieSidebarMediaController::TabMedia* AerieSidebarMediaController::Pick()
    const {
  TabMedia* best = nullptr;
  for (const auto& media : tabs_) {
    if (media->playing() && (!best || media->started_at() > best->started_at())) {
      best = media.get();
    }
  }
  if (best) {
    return best;
  }
  return shown_ && shown_->holds_session() ? shown_.get() : nullptr;
}

void AerieSidebarMediaController::OnTabMediaChanged() {
  Refresh();
}

void AerieSidebarMediaController::Refresh() {
  const PrefService* prefs = browser_->GetProfile()->GetPrefs();
  shown_ = Pick();
  if (!shown_) {
    card_->Dismiss(prefs);
    return;
  }
  AerieSidebarMediaCard::Content content;
  content.title = shown_->Title();
  content.site = SiteOf(shown_->contents());
  content.playing = shown_->playing();
  content.next_enabled = shown_->next_enabled();
  card_->SetContent(content);
  card_->Appear(prefs);
}

void AerieSidebarMediaController::PlayPause() {
  content::WebContents* contents = shown_contents();
  if (!contents) {
    return;
  }
  content::MediaSession* session = content::MediaSession::GetIfExists(contents);
  if (!session) {
    return;
  }
  if (shown_->playing()) {
    session->Suspend(content::MediaSession::SuspendType::kUI);
  } else {
    session->Resume(content::MediaSession::SuspendType::kUI);
  }
}

void AerieSidebarMediaController::NextTrack() {
  content::WebContents* contents = shown_contents();
  if (!contents || !shown_->next_enabled()) {
    return;
  }
  if (content::MediaSession* session =
          content::MediaSession::GetIfExists(contents)) {
    session->NextTrack();
  }
}

void AerieSidebarMediaController::GoToTab() {
  content::WebContents* contents = shown_contents();
  if (!contents) {
    return;
  }
  TabStripModel* model = browser_->GetTabStripModel();
  const int index = model->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab) {
    model->ActivateTabAt(
        index, TabStripUserGestureDetails(
                   TabStripUserGestureDetails::GestureType::kOther));
  }
}
