// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_AERIE_CREATED_WINDOW_INFO_H_
#define CONTENT_PUBLIC_BROWSER_AERIE_CREATED_WINDOW_INFO_H_

#include <utility>

#include "base/supports_user_data.h"
#include "content/public/common/referrer.h"

namespace content {

// What the renderer asked for when it made a window with no opener (a link
// with target=_blank, a form with target=_blank): the referrer it chose for the
// first navigation, and whether that navigation is a form sent with POST.
// WebContentsImpl::CreateNewWindow puts it on the new contents as user data
// before the delegate sees the contents (patches/sidebar/0053), because the
// load itself starts only after the delegate has added the contents to a tab
// strip. "Links open" (issue #141) reads it to leave forms alone and to pass
// the referrer on.
inline constexpr char kAerieCreatedWindowInfoKey[] = "aerie_created_window_info";

class AerieCreatedWindowInfo : public base::SupportsUserData::Data {
 public:
  AerieCreatedWindowInfo(Referrer referrer, bool is_form_post)
      : referrer(std::move(referrer)), is_form_post(is_form_post) {}
  ~AerieCreatedWindowInfo() override = default;

  const Referrer referrer;
  const bool is_form_post;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_AERIE_CREATED_WINDOW_INFO_H_
