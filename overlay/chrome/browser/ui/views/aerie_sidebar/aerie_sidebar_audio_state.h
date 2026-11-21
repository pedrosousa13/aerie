// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_AUDIO_STATE_H_
#define CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_AUDIO_STATE_H_

// What the audio mark of a tab row shows (issue #268): nothing, a speaker (the
// tab plays sound) or a crossed speaker (the tab is muted).
enum class AerieSidebarAudioState { kNone, kPlaying, kMuted };

#endif  // CHROME_BROWSER_UI_VIEWS_AERIE_SIDEBAR_AERIE_SIDEBAR_AUDIO_STATE_H_
