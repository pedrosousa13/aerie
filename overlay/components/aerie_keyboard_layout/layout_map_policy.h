// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_KEYBOARD_LAYOUT_LAYOUT_MAP_POLICY_H_
#define COMPONENTS_AERIE_KEYBOARD_LAYOUT_LAYOUT_MAP_POLICY_H_

// Whether navigator.keyboard.getLayoutMap() is refused while fingerprinting
// protection is on (issue #203). The layout gives away the user's region. A
// refused call gets a promise rejected with NotSupportedError, the outcome of a
// browser without the API. lock() and unlock() are not part of this decision.
// Pure code: no Blink, no content, no base, so it has unit tests. The keyboard
// lock service only decides when to call it (patches/hardening/0029).
namespace aerie_keyboard_layout {

// What decides whether a request is refused.
struct Policy {
  // Whether protection is on for the site of the top page: the HeliumNoise
  // feature and the global switch are on and the site has no per-site off. The
  // browser decides this with ContentBrowserClient::HeliumNoiseEnabled, which
  // answers from the feature and the global pref alone for a top page with an
  // opaque origin (a sandboxed or data: page has no site to switch off), so
  // such a page is protected whenever protection is on.
  bool site_protected;
};

// Protection off, globally or for the site, gives back the real map.
bool ShouldRefuseLayoutMap(const Policy& policy);

}  // namespace aerie_keyboard_layout

#endif  // COMPONENTS_AERIE_KEYBOARD_LAYOUT_LAYOUT_MAP_POLICY_H_
