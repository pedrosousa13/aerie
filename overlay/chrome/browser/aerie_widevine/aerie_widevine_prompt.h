// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_AERIE_WIDEVINE_AERIE_WIDEVINE_PROMPT_H_
#define CHROME_BROWSER_AERIE_WIDEVINE_AERIE_WIDEVINE_PROMPT_H_

namespace content {
class RenderFrameHost;
}  // namespace content

namespace aerie_widevine {

// Called (through ContentBrowserClient::OnEncryptedMediaRequested) when a frame
// first uses encrypted media. If Widevine is off and the user has not declined
// it for this site, shows a one-time infobar that says what Widevine is and
// offers to turn it on. "Turn on" sets aerie.widevine.enabled. "No thanks"
// records the site, which is then never asked again. Closing the infobar does
// neither.
void OnEncryptedMediaRequested(content::RenderFrameHost* render_frame_host);

}  // namespace aerie_widevine

#endif  // CHROME_BROWSER_AERIE_WIDEVINE_AERIE_WIDEVINE_PROMPT_H_
