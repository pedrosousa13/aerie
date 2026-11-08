// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

// loadTimeData keys of aerie://setup and their message IDs from
// aerie_onboarding_strings.grdp. tests/test_onboarding_page.py checks that the
// two lists match and that the page only asks for keys listed here.

#ifndef COMPONENTS_AERIE_ONBOARDING_AERIE_ONBOARDING_LOCALIZED_STRINGS_H_
#define COMPONENTS_AERIE_ONBOARDING_AERIE_ONBOARDING_LOCALIZED_STRINGS_H_

#include "chrome/grit/generated_resources.h"
#include "ui/base/webui/web_ui_util.h"

namespace aerie_onboarding {

inline constexpr webui::LocalizedString kLocalizedStrings[] = {
    {"pageTitle", IDS_AERIE_SETUP_PAGE_TITLE},
    {"buttonContinue", IDS_AERIE_SETUP_BUTTON_CONTINUE},
    {"buttonBack", IDS_AERIE_SETUP_BUTTON_BACK},
    {"stepOf", IDS_AERIE_SETUP_STEP_OF},
    {"keyboardHint", IDS_AERIE_SETUP_KEYBOARD_HINT},
    {"welcomeTitle", IDS_AERIE_SETUP_WELCOME_TITLE},
    {"welcomeBody", IDS_AERIE_SETUP_WELCOME_BODY},
    {"welcomeStart", IDS_AERIE_SETUP_WELCOME_START},
    {"importTitle", IDS_AERIE_SETUP_IMPORT_TITLE},
    {"importLoading", IDS_AERIE_SETUP_IMPORT_LOADING},
    {"importNoneFound", IDS_AERIE_SETUP_IMPORT_NONE_FOUND},
    {"importFresh", IDS_AERIE_SETUP_IMPORT_FRESH},
    {"importFreshDesc", IDS_AERIE_SETUP_IMPORT_FRESH_DESC},
    {"importWhat", IDS_AERIE_SETUP_IMPORT_WHAT},
    {"importBookmarks", IDS_AERIE_SETUP_IMPORT_BOOKMARKS},
    {"importPasswords", IDS_AERIE_SETUP_IMPORT_PASSWORDS},
    {"importHistory", IDS_AERIE_SETUP_IMPORT_HISTORY},
    {"importSearch", IDS_AERIE_SETUP_IMPORT_SEARCH},
    {"importButton", IDS_AERIE_SETUP_IMPORT_BUTTON},
    {"importSkip", IDS_AERIE_SETUP_IMPORT_SKIP},
    {"importLocal", IDS_AERIE_SETUP_IMPORT_LOCAL},
    {"importPickOne", IDS_AERIE_SETUP_IMPORT_PICK_ONE},
    {"importRunning", IDS_AERIE_SETUP_IMPORT_RUNNING},
    {"importFailed", IDS_AERIE_SETUP_IMPORT_FAILED},
    {"lookTitle", IDS_AERIE_SETUP_LOOK_TITLE},
    {"lookBody", IDS_AERIE_SETUP_LOOK_BODY},
    {"lookAppearance", IDS_AERIE_SETUP_LOOK_APPEARANCE},
    {"lookLight", IDS_AERIE_SETUP_LOOK_LIGHT},
    {"lookDark", IDS_AERIE_SETUP_LOOK_DARK},
    {"lookSystem", IDS_AERIE_SETUP_LOOK_SYSTEM},
    {"lookColour", IDS_AERIE_SETUP_LOOK_COLOUR},
    {"colorNeutral", IDS_AERIE_SETUP_COLOR_NEUTRAL},
    {"colorAlpenglow", IDS_AERIE_SETUP_COLOR_ALPENGLOW},
    {"colorSky", IDS_AERIE_SETUP_COLOR_SKY},
    {"colorMoss", IDS_AERIE_SETUP_COLOR_MOSS},
    {"colorDusk", IDS_AERIE_SETUP_COLOR_DUSK},
    {"colorSand", IDS_AERIE_SETUP_COLOR_SAND},
    {"colorGraphite", IDS_AERIE_SETUP_COLOR_GRAPHITE},
    {"previewTab", IDS_AERIE_SETUP_PREVIEW_TAB},
    {"previewTab2", IDS_AERIE_SETUP_PREVIEW_TAB2},
    {"blockTitle", IDS_AERIE_SETUP_BLOCK_TITLE},
    {"blockBody", IDS_AERIE_SETUP_BLOCK_BODY},
    {"blockAds", IDS_AERIE_SETUP_BLOCK_ADS},
    {"blockTrackers", IDS_AERIE_SETUP_BLOCK_TRACKERS},
    {"blockBoxes", IDS_AERIE_SETUP_BLOCK_BOXES},
    {"blockPerSite", IDS_AERIE_SETUP_BLOCK_PER_SITE},
    {"blockManage", IDS_AERIE_SETUP_BLOCK_MANAGE},
    {"blockCounter", IDS_AERIE_SETUP_BLOCK_COUNTER},
    {"searchTitle", IDS_AERIE_SETUP_SEARCH_TITLE},
    {"searchDescDuckDuckGo", IDS_AERIE_SETUP_SEARCH_DESC_DUCK_DUCK_GO},
    {"searchDescBrave", IDS_AERIE_SETUP_SEARCH_DESC_BRAVE},
    {"searchDescKagi", IDS_AERIE_SETUP_SEARCH_DESC_KAGI},
    {"searchDescStartpage", IDS_AERIE_SETUP_SEARCH_DESC_STARTPAGE},
    {"searchSuggest", IDS_AERIE_SETUP_SEARCH_SUGGEST},
    {"privacyTitle", IDS_AERIE_SETUP_PRIVACY_TITLE},
    {"privacyBody", IDS_AERIE_SETUP_PRIVACY_BODY},
    {"privacyOk", IDS_AERIE_SETUP_PRIVACY_OK},
    {"privacyHttps", IDS_AERIE_SETUP_PRIVACY_HTTPS},
    {"privacyHttpsDesc", IDS_AERIE_SETUP_PRIVACY_HTTPS_DESC},
    {"privacyCookies", IDS_AERIE_SETUP_PRIVACY_COOKIES},
    {"privacyCookiesDesc", IDS_AERIE_SETUP_PRIVACY_COOKIES_DESC},
    {"privacyFingerprint", IDS_AERIE_SETUP_PRIVACY_FINGERPRINT},
    {"privacyFingerprintDesc", IDS_AERIE_SETUP_PRIVACY_FINGERPRINT_DESC},
    {"privacyWebrtc", IDS_AERIE_SETUP_PRIVACY_WEBRTC},
    {"privacyWebrtcDesc", IDS_AERIE_SETUP_PRIVACY_WEBRTC_DESC},
    {"privacyPreload", IDS_AERIE_SETUP_PRIVACY_PRELOAD},
    {"privacyPreloadDesc", IDS_AERIE_SETUP_PRIVACY_PRELOAD_DESC},
    {"privacyTelemetry", IDS_AERIE_SETUP_PRIVACY_TELEMETRY},
    {"privacyTelemetryDesc", IDS_AERIE_SETUP_PRIVACY_TELEMETRY_DESC},
    {"defaultTitle", IDS_AERIE_SETUP_DEFAULT_TITLE},
    {"defaultBody", IDS_AERIE_SETUP_DEFAULT_BODY},
    {"defaultYes", IDS_AERIE_SETUP_DEFAULT_YES},
    {"defaultNo", IDS_AERIE_SETUP_DEFAULT_NO},
    {"doneTitle", IDS_AERIE_SETUP_DONE_TITLE},
    {"doneStart", IDS_AERIE_SETUP_DONE_START},
    {"summaryImported", IDS_AERIE_SETUP_SUMMARY_IMPORTED},
    {"summaryLook", IDS_AERIE_SETUP_SUMMARY_LOOK},
    {"summarySearch", IDS_AERIE_SETUP_SUMMARY_SEARCH},
    {"summaryDefault", IDS_AERIE_SETUP_SUMMARY_DEFAULT},
    {"summaryNothing", IDS_AERIE_SETUP_SUMMARY_NOTHING},
    {"summaryIsDefault", IDS_AERIE_SETUP_SUMMARY_IS_DEFAULT},
    {"summaryNotDefault", IDS_AERIE_SETUP_SUMMARY_NOT_DEFAULT},
};

}  // namespace aerie_onboarding

#endif  // COMPONENTS_AERIE_ONBOARDING_AERIE_ONBOARDING_LOCALIZED_STRINGS_H_
