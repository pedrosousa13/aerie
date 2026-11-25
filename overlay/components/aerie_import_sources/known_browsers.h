// Copyright 2026 The Aerie Authors
// You can use, redistribute, and/or modify this source code under
// the terms of the GPL-3.0 license that can be found in the LICENSE file.

#ifndef COMPONENTS_AERIE_IMPORT_SOURCES_KNOWN_BROWSERS_H_
#define COMPONENTS_AERIE_IMPORT_SOURCES_KNOWN_BROWSERS_H_

#include <string>
#include <vector>

#include "components/user_data_importer/common/importer_type.h"

namespace aerie_import_sources {

// What Aerie knows about one browser, in one place: the names the import step lists it under,
// where its desktop entry is, and where a Flatpak install keeps its data.
struct KnownBrowser {
  KnownBrowser();
  KnownBrowser(const KnownBrowser&);
  ~KnownBrowser();

  // The names the importer gives its rows (the first one is the one Aerie uses for a row it adds).
  std::vector<std::string> names;
  // .desktop file ids (without ".desktop") of native installs, most likely first.
  std::vector<std::string> native_desktop_ids;
  // The Flatpak app id, which is also the id of its .desktop file. Empty: no known Flatpak.
  std::string flatpak_app_id;
  // Where a Chromium-family Flatpak keeps its user data, under <home>/.var/app/<id>/config.
  // Empty when Chromium's own importer list already finds the install (Firefox, Zen) or there
  // is no Flatpak.
  std::string flatpak_data_folder;
  user_data_importer::ImporterType flatpak_importer_type =
      user_data_importer::TYPE_UNKNOWN;
};

const std::vector<KnownBrowser>& KnownBrowsers();

}  // namespace aerie_import_sources

#endif  // COMPONENTS_AERIE_IMPORT_SOURCES_KNOWN_BROWSERS_H_
