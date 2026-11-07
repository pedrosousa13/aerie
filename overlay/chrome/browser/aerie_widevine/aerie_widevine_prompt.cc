// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "chrome/browser/aerie_widevine/aerie_widevine_prompt.h"

#include <memory>
#include <string>
#include <utility>

#include "chrome/browser/browser_process.h"
#include "chrome/browser/infobars/confirm_infobar_creator.h"
#include "components/aerie_widevine/widevine_opt_in.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/infobars/core/infobar.h"
#include "components/infobars/core/infobar_delegate.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace aerie_widevine {

namespace {

// The strings are plain text here, not grd messages: a grd message of Aerie's
// would need its own translation ids, and this prompt is English only for now.
class WidevinePromptDelegate : public ConfirmInfoBarDelegate {
 public:
  explicit WidevinePromptDelegate(std::string host) : host_(std::move(host)) {}

  WidevinePromptDelegate(const WidevinePromptDelegate&) = delete;
  WidevinePromptDelegate& operator=(const WidevinePromptDelegate&) = delete;

  // ConfirmInfoBarDelegate:
  infobars::InfoBarDelegate::InfoBarIdentifier GetIdentifier() const override {
    return AERIE_WIDEVINE_PROMPT_INFOBAR_DELEGATE;
  }

  std::u16string GetMessageText() const override {
    return u"This site wants to play protected (DRM) content. Widevine is a "
           u"component made by Google that decodes it. If you turn it on, "
           u"Aerie downloads it from Google and checks Google for updates. "
           u"Aerie contacts Google for nothing else, and not for this until "
           u"you agree. You can change this in Settings.";
  }

  int GetButtons() const override { return BUTTON_OK | BUTTON_CANCEL; }

  std::u16string GetButtonLabel(InfoBarButton button) const override {
    return button == BUTTON_OK ? u"Turn on Widevine" : u"No thanks";
  }

  bool Accept() override {
    SetEnabled(g_browser_process->local_state(), true);
    return true;
  }

  bool Cancel() override {
    RecordDecline(g_browser_process->local_state(), host_);
    return true;
  }

 private:
  std::string host_;
};

bool HasPrompt(infobars::InfoBarManager* manager) {
  for (const auto& infobar : manager->infobars()) {
    if (infobar->GetIdentifier() ==
        infobars::InfoBarDelegate::AERIE_WIDEVINE_PROMPT_INFOBAR_DELEGATE) {
      return true;
    }
  }
  return false;
}

}  // namespace

void OnEncryptedMediaRequested(content::RenderFrameHost* render_frame_host) {
  if (!render_frame_host || !g_browser_process) {
    return;
  }
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  if (!web_contents) {
    return;
  }
  // The site is the top-level page, also when the request came from a frame.
  const GURL url = web_contents->GetLastCommittedURL();
  if (!url.SchemeIsHTTPOrHTTPS() ||
      !ShouldPrompt(g_browser_process->local_state(), url.host())) {
    return;
  }
  infobars::ContentInfoBarManager* manager =
      infobars::ContentInfoBarManager::FromWebContents(web_contents);
  if (!manager || HasPrompt(manager)) {
    return;
  }
  manager->AddInfoBar(CreateConfirmInfoBar(
      std::make_unique<WidevinePromptDelegate>(std::string(url.host()))));
}

}  // namespace aerie_widevine
