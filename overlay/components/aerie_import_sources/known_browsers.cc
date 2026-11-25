// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#include "components/aerie_import_sources/known_browsers.h"

#include <utility>

#include "base/no_destructor.h"

namespace aerie_import_sources {

KnownBrowser::KnownBrowser() = default;
KnownBrowser::KnownBrowser(const KnownBrowser&) = default;
KnownBrowser::~KnownBrowser() = default;

namespace {

KnownBrowser Make(std::vector<std::string> names,
                  std::vector<std::string> native_desktop_ids,
                  std::string flatpak_app_id = "",
                  std::string flatpak_data_folder = "",
                  user_data_importer::ImporterType type =
                      user_data_importer::TYPE_UNKNOWN) {
  KnownBrowser browser;
  browser.names = std::move(names);
  browser.native_desktop_ids = std::move(native_desktop_ids);
  browser.flatpak_app_id = std::move(flatpak_app_id);
  browser.flatpak_data_folder = std::move(flatpak_data_folder);
  browser.flatpak_importer_type = type;
  return browser;
}

}  // namespace

const std::vector<KnownBrowser>& KnownBrowsers() {
  static const base::NoDestructor<std::vector<KnownBrowser>> browsers(
      std::vector<KnownBrowser>{
          Make({"Google Chrome"}, {"google-chrome", "google-chrome-stable"},
               "com.google.Chrome", "google-chrome",
               user_data_importer::TYPE_CHROME),
          Make({"Google Chrome Beta"}, {"google-chrome-beta"}),
          Make({"Google Chrome Dev"}, {"google-chrome-unstable"}),
          Make({"Chromium"}, {"chromium", "chromium-browser"},
               "org.chromium.Chromium", "chromium",
               user_data_importer::TYPE_CHROME),
          Make({"Brave"}, {"brave-browser", "brave"}, "com.brave.Browser",
               "BraveSoftware/Brave-Browser", user_data_importer::TYPE_BRAVE),
          Make({"Microsoft Edge"}, {"microsoft-edge", "microsoft-edge-stable"},
               "com.microsoft.Edge", "microsoft-edge",
               user_data_importer::TYPE_EDGE_CHROMIUM),
          Make({"Vivaldi"}, {"vivaldi-stable", "vivaldi"},
               "com.vivaldi.Vivaldi", "vivaldi",
               user_data_importer::TYPE_VIVALDI),
          Make({"Opera"}, {"opera"}, "com.opera.Opera", "opera",
               user_data_importer::TYPE_OPERA),
          Make({"Yandex"}, {"yandex-browser", "yandex-browser-beta"}),
          Make({"NAVER Whale"}, {"naver-whale", "naver-whale-stable"}),
          Make({"Helium"}, {"helium", "net.imput.helium"}),
          // The importer names Firefox "Mozilla Firefox" when it cannot read application.ini.
          Make({"Firefox", "Mozilla Firefox"},
               {"firefox", "firefox-esr", "org.mozilla.firefox"},
               "org.mozilla.firefox"),
          Make({"Firefox Developer Edition"}, {"firefox-developer-edition"}),
          Make({"Firefox Nightly"}, {"firefox-nightly"}),
          Make({"Zen"}, {"zen", "zen-browser", "app.zen_browser.zen"},
               "app.zen_browser.zen"),
      });
  return *browsers;
}

}  // namespace aerie_import_sources
